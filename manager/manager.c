#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <signal.h>

#include "../common/protocol.h"
#include "../common/mib.h"
#include "../common/tls.h"

typedef struct {
    char host[64];
    int port;
    char name[64];
} Agent;

typedef struct {
    char name[64];
    char status[16];
    double cpu;
    double memory;
    long long uptime;
    long long rx;
    long long tx;
    long long latency_us;
} AgentState;

static Agent agents[128];
static AgentState states[128];
static int agent_count = 0;
static int interval_sec = 5;
static volatile int running = 1;
static int tls_enabled = 0;
static int trap_port = DEFAULT_TRAP_PORT;
static SSL_CTX *tls_client_ctx_global = NULL;
static SSL_CTX *tls_server_ctx_global = NULL;

static int connect_agent(const char *host, int port, SSL **out_ssl) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct timeval tv;
    tv.tv_sec = SOCKET_TIMEOUT_SEC;
    tv.tv_usec = 0;

    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    *out_ssl = NULL;
    if (tls_enabled) {
        *out_ssl = tls_connect(tls_client_ctx_global, fd);
        if (!*out_ssl) {
            close(fd);
            return -1;
        }
    }

    return fd;
}

static int send_line(int fd, SSL *ssl, const char *line) {
    if (ssl) return SSL_write(ssl, line, (int)strlen(line)) <= 0 ? -1 : 0;
    return send(fd, line, strlen(line), 0) < 0 ? -1 : 0;
}

static int receive_line(int fd, SSL *ssl, char *buffer, size_t size) {
    memset(buffer, 0, size);
    ssize_t n = ssl ? SSL_read(ssl, buffer, (int)size - 1)
                    : recv(fd, buffer, size - 1, 0);
    if (n <= 0) return -1;
    buffer[n] = '\0';
    buffer[strcspn(buffer, "\r\n")] = '\0';
    return 0;
}

static int authenticate(int fd, SSL *ssl) {
    char msg[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    snprintf(msg, sizeof(msg), "AUTH %s\n", AUTH_TOKEN);

    if (send_line(fd, ssl, msg) != 0) return -1;
    if (receive_line(fd, ssl, response, sizeof(response)) != 0) return -1;

    return strcmp(response, "AUTH_OK") == 0 ? 0 : -1;
}

static double now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static int get_metric(int fd, SSL *ssl, const char *oid, char *value, size_t size,
                      long long *latency_us) {
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    snprintf(request, sizeof(request), "GET %s\n", oid);

    double start = now_ms();

    if (send_line(fd, ssl, request) != 0) return -1;
    if (receive_line(fd, ssl, response, sizeof(response)) != 0) return -1;

    double end = now_ms();
    if (latency_us) *latency_us = (long long)((end - start) * 1000.0);

    char response_type[32];
    char response_oid[32];
    char response_value[128];

    if (sscanf(response, "%31s %31s %127s",
               response_type, response_oid, response_value) != 3)
        return -1;

    if (strcmp(response_type, MSG_RESPONSE) != 0)
        return -1;

    if (strcmp(response_oid, oid) != 0)
        return -1;

    strncpy(value, response_value, size - 1);
    value[size - 1] = '\0';

    return 0;
}

static int set_remote(int index, const char *oid, double value) {
    if (index < 0 || index >= agent_count) return -1;

    SSL *ssl = NULL;
    int fd = connect_agent(agents[index].host, agents[index].port, &ssl);
    if (fd < 0) return -1;

    if (authenticate(fd, ssl) != 0) {
        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
        close(fd);
        return -1;
    }

    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    snprintf(request, sizeof(request), "SET %s %.2f\n", oid, value);

    if (send_line(fd, ssl, request) != 0 ||
        receive_line(fd, ssl, response, sizeof(response)) != 0) {
        close(fd);
        return -1;
    }

    printf("Agent %s -> %s\n", agents[index].name, response);
    if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
    close(fd);
    return strncmp(response, "SET_OK", 6) == 0 ? 0 : -1;
}

static void write_history(int index, const char *status,
                          double cpu, double memory, long long uptime,
                          long long rx, long long tx, long long latency_us) {
    FILE *f = fopen("logs/metrics.csv", "a");
    if (!f) return;

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(f, "%s,%s,%s,%s,%.2f,%.2f,%lld,%lld,%lld,%lld\n",
            timestamp,
            agents[index].name,
            agents[index].host,
            status,
            cpu, memory, uptime, rx, tx, latency_us);

    fclose(f);
}

static void *trap_server(void *arg) {
    (void)arg;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return NULL;

    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(trap_port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(server_fd);
        return NULL;
    }

    if (listen(server_fd, 10) < 0) {
        close(server_fd);
        return NULL;
    }

    while (running) {
        struct timeval tv = {1, 0};
        setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_in client;
        socklen_t len = sizeof(client);

        int fd = accept(server_fd, (struct sockaddr *)&client, &len);
        if (fd < 0) continue;

        SSL *ssl = NULL;
        if (tls_enabled) {
            ssl = tls_accept(tls_server_ctx_global, fd);
            if (!ssl) {
                close(fd);
                continue;
            }
        }

        char buffer[BUFFER_SIZE];
        if (receive_line(fd, ssl, buffer, sizeof(buffer)) == 0) {
            char type[32], agent[64], oid[32];
            double value, threshold;

            if (sscanf(buffer, "%31s %63s %31s %lf %lf",
                       type, agent, oid, &value, &threshold) == 5 &&
                strcmp(type, MSG_TRAP) == 0) {

                printf("\n[TRAP] %s | OID %s | valor %.2f | limite %.2f\n",
                       agent, oid, value, threshold);

                FILE *f = fopen("logs/alerts.log", "a");
                if (f) {
                    time_t now = time(NULL);
                    fprintf(f, "%ld,%s,%s,%.2f,%.2f\n",
                            now, agent, oid, value, threshold);
                    fclose(f);
                }
            }
        }

        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
        close(fd);
    }

    close(server_fd);
    return NULL;
}

static void draw_dashboard(void) {
    printf("\033[2J\033[H");
    printf("===============================================================\n");
    printf("                    MINI-SNMP MONITOR                         \n");
    printf("===============================================================\n");
    printf("%-12s %-10s %-8s %-8s %-10s %-10s %-10s\n",
           "Agent", "Status", "CPU", "MEM", "RX", "TX", "Latency");
    printf("---------------------------------------------------------------\n");

    for (int i = 0; i < agent_count; i++) {
        if (strcmp(states[i].status, "ONLINE") == 0) {
            printf("%-12s %-10s %6.2f%% %6.2f%% %-10lld %-10lld %6lld us\n",
                   states[i].name,
                   states[i].status,
                   states[i].cpu,
                   states[i].memory,
                   states[i].rx,
                   states[i].tx,
                   states[i].latency_us);
        } else {
            printf("%-12s %-10s\n",
                   states[i].name,
                   states[i].status);
        }
    }

    printf("---------------------------------------------------------------\n");
    printf("Intervalo: %d s | Timeout: %d s | Trap port: %d\n",
           interval_sec, SOCKET_TIMEOUT_SEC, trap_port);
    printf("Pressione Ctrl+C para encerrar.\n");
}

static void poll_agent(int index) {
    SSL *ssl = NULL;
    int fd = connect_agent(agents[index].host, agents[index].port, &ssl);

    states[index].cpu = 0;
    states[index].memory = 0;
    states[index].uptime = 0;
    states[index].rx = 0;
    states[index].tx = 0;
    states[index].latency_us = 0;

    if (fd < 0) {
        strcpy(states[index].status, "OFFLINE");
        write_history(index, "OFFLINE", 0, 0, 0, 0, 0, 0);
        return;
    }

    if (authenticate(fd, ssl) != 0) {
        strcpy(states[index].status, "AUTH_ERR");
        close(fd);
        write_history(index, "AUTH_ERR", 0, 0, 0, 0, 0, 0);
        return;
    }

    char value[128];
    long long latency = 0;

    if (get_metric(fd, ssl, OID_CPU, value, sizeof(value), &latency) != 0) {
        strcpy(states[index].status, "TIMEOUT");
        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
        close(fd);
        write_history(index, "TIMEOUT", 0, 0, 0, 0, 0, latency);
        return;
    }
    states[index].cpu = atof(value);
    states[index].latency_us = latency;

    if (get_metric(fd, ssl, OID_MEMORY, value, sizeof(value), NULL) != 0) {
        strcpy(states[index].status, "ERROR");
        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
        close(fd);
        write_history(index, "ERROR", states[index].cpu, 0, 0, 0, 0, latency);
        return;
    }
    states[index].memory = atof(value);

    if (get_metric(fd, ssl, OID_UPTIME, value, sizeof(value), NULL) == 0)
        states[index].uptime = atoll(value);

    if (get_metric(fd, ssl, OID_NET_RX, value, sizeof(value), NULL) == 0)
        states[index].rx = atoll(value);

    if (get_metric(fd, ssl, OID_NET_TX, value, sizeof(value), NULL) == 0)
        states[index].tx = atoll(value);

    strcpy(states[index].status, "ONLINE");

    write_history(index, "ONLINE",
                   states[index].cpu,
                   states[index].memory,
                   states[index].uptime,
                   states[index].rx,
                   states[index].tx,
                   states[index].latency_us);

    if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
    close(fd);
}

static void on_signal(int sig) {
    (void)sig;
    running = 0;
}

static int load_agents(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    char line[256];

    while (fgets(line, sizeof(line), f) && agent_count < 128) {
        if (line[0] == '#' || line[0] == '\n') continue;

        char host[64], name[64];
        int port;

        if (sscanf(line, "%63s %d %63s", host, &port, name) == 3) {
            strncpy(agents[agent_count].host, host,
                    sizeof(agents[agent_count].host) - 1);
            strncpy(agents[agent_count].name, name,
                    sizeof(agents[agent_count].name) - 1);
            agents[agent_count].port = port;

            strncpy(states[agent_count].name, name,
                    sizeof(states[agent_count].name) - 1);

            agent_count++;
        }
    }

    fclose(f);
    return agent_count;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s <arquivo_agents> [intervalo] [--tls] [--trap-port PORT]\n", argv[0]);
        printf("Exemplo: %s agents.conf 5 --tls --trap-port 6000\n", argv[0]);
        return 1;
    }

    if (argc >= 3)
        interval_sec = atoi(argv[2]);

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--tls") == 0) {
            tls_enabled = 1;
        } else if (strcmp(argv[i], "--trap-port") == 0 && i + 1 < argc) {
            trap_port = atoi(argv[++i]);
        }
    }

    if (interval_sec < 1) interval_sec = 1;

    if (tls_enabled) {
        tls_init();
        tls_client_ctx_global = tls_client_context(TLS_MANAGER_CERT, TLS_MANAGER_KEY);
        tls_server_ctx_global = tls_server_context(TLS_MANAGER_CERT, TLS_MANAGER_KEY);

        if (!tls_client_ctx_global || !tls_server_ctx_global) {
            fprintf(stderr, "Falha ao inicializar TLS. Execute ./generate_certs.sh primeiro.\n");
            return 1;
        }

        SSL_CTX_set_verify(tls_server_ctx_global,
                           SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
        if (SSL_CTX_load_verify_locations(tls_server_ctx_global, TLS_CA_FILE, NULL) <= 0) {
            fprintf(stderr, "Falha ao carregar CA para TLS.\n");
            return 1;
        }
    }

    if (load_agents(argv[1]) <= 0) {
        fprintf(stderr, "Nenhum Agent encontrado em %s\n", argv[1]);
        return 1;
    }

    /* Configuracao remota opcional:
       ./manager agents.conf 5 --set 0 3.1 75 */
    if (argc >= 7 && strcmp(argv[3], "--set") == 0) {
        int index = atoi(argv[4]);
        const char *oid = argv[5];
        double value = atof(argv[6]);

        if (set_remote(index, oid, value) != 0) {
            fprintf(stderr, "Falha ao configurar remotamente o Agent.\n");
            return 1;
        }

        printf("Configuracao remota aplicada.\n");
    }

    /* Garante que a pasta de logs exista. */
    if (mkdir("logs", 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Falha ao criar a pasta logs.\n");
        return 1;
    }

    /* Garante um CSV válido mesmo se existir um arquivo antigo sem cabeçalho. */
    const char *csv_header =
        "timestamp,agent,host,status,cpu,memory,uptime,rx,tx,latency_us\n";
    FILE *history = fopen("logs/metrics.csv", "r");
    int rewrite_header = 0;

    if (!history) {
        rewrite_header = 1;
    } else {
        char first_line[512] = {0};
        if (!fgets(first_line, sizeof(first_line), history) ||
            strncmp(first_line, "timestamp,agent,host,status,", strlen("timestamp,agent,host,status,")) != 0) {
            rewrite_header = 1;
        }
        fclose(history);
    }

    if (rewrite_header) {
        history = fopen("logs/metrics.csv", "w");
        if (!history) {
            fprintf(stderr, "Falha ao criar logs/metrics.csv.\n");
            return 1;
        }
        fputs(csv_header, history);
        fclose(history);
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    pthread_t trap_thread;
    pthread_create(&trap_thread, NULL, trap_server, NULL);

    printf("Mini-SNMP Manager iniciado.\n");
    printf("Agents configurados: %d\n", agent_count);

    while (running) {
        for (int i = 0; i < agent_count; i++)
            poll_agent(i);

        draw_dashboard();

        for (int s = 0; s < interval_sec && running; s++)
            sleep(1);
    }

    running = 0;
    pthread_join(trap_thread, NULL);

    if (tls_client_ctx_global) SSL_CTX_free(tls_client_ctx_global);
    if (tls_server_ctx_global) SSL_CTX_free(tls_server_ctx_global);

    printf("\nManager encerrado.\n");
    return 0;
}
