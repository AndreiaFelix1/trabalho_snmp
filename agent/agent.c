#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>

#include "../common/protocol.h"
#include "../common/mib.h"
#include "../common/tls.h"
#include "metrics.h"

static double cpu_limit = 80.0;
static double memory_limit = 90.0;
static char agent_name[64] = "Agent";
static int tls_enabled = 0;

static int send_line(int fd, SSL *ssl, const char *line) {
    if (ssl) return SSL_write(ssl, line, (int)strlen(line)) <= 0 ? -1 : 0;
    return (send(fd, line, strlen(line), 0) < 0) ? -1 : 0;
}

static int connect_to_manager(const char *host, int port, SSL_CTX *tls_client, SSL **out_ssl) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

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
    if (tls_client) {
        *out_ssl = tls_connect(tls_client, fd);
        if (!*out_ssl) {
            close(fd);
            return -1;
        }
    }
    return fd;
}

static void send_trap(const char *trap_host, int trap_port,
                      const char *oid, double value, double threshold,
                      SSL_CTX *tls_client) {
    if (!trap_host || trap_port <= 0) return;

    SSL *ssl = NULL;
    int fd = connect_to_manager(trap_host, trap_port, tls_client, &ssl);
    if (fd < 0) return;

    char msg[BUFFER_SIZE];
    snprintf(msg, sizeof(msg), "%s %s %s %.2f %.2f\n",
             MSG_TRAP, agent_name, oid, value, threshold);
    send_line(fd, ssl, msg);

    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    close(fd);
}

static int metric_value(const char *oid, char *value, size_t size) {
    if (strcmp(oid, OID_CPU) == 0) {
        snprintf(value, size, "%.2f", get_cpu_usage());
        return 0;
    }
    if (strcmp(oid, OID_MEMORY) == 0) {
        snprintf(value, size, "%.2f", get_memory_usage());
        return 0;
    }
    if (strcmp(oid, OID_UPTIME) == 0) {
        snprintf(value, size, "%lld", get_uptime());
        return 0;
    }
    if (strcmp(oid, OID_CONNECTIONS) == 0) {
        snprintf(value, size, "%lld", get_active_connections());
        return 0;
    }
    if (strcmp(oid, OID_NET_RX) == 0) {
        snprintf(value, size, "%lld", get_network_rx());
        return 0;
    }
    if (strcmp(oid, OID_NET_TX) == 0) {
        snprintf(value, size, "%lld", get_network_tx());
        return 0;
    }
    if (strcmp(oid, OID_NET_PKTS_RX) == 0) {
        snprintf(value, size, "%lld", get_network_packets_rx());
        return 0;
    }
    if (strcmp(oid, OID_NET_PKTS_TX) == 0) {
        snprintf(value, size, "%lld", get_network_packets_tx());
        return 0;
    }
    return -1;
}

static void handle_client(int client_fd, SSL *ssl,
                          const char *trap_host, int trap_port,
                          SSL_CTX *tls_client) {
    char buffer[BUFFER_SIZE];
    int authenticated = 0;

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t n = ssl ? SSL_read(ssl, buffer, sizeof(buffer) - 1)
                        : recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;

        buffer[strcspn(buffer, "\r\n")] = '\0';

        char command[32] = {0};
        char arg1[64] = {0};
        char arg2[64] = {0};

        int parts = sscanf(buffer, "%31s %63s %63s", command, arg1, arg2);

        if (parts < 1) {
            send_line(client_fd, ssl, "ERROR INVALID_REQUEST\n");
            continue;
        }

        if (strcmp(command, MSG_AUTH) == 0) {
            if (parts >= 2 && strcmp(arg1, AUTH_TOKEN) == 0) {
                authenticated = 1;
                send_line(client_fd, ssl, "AUTH_OK\n");
            } else {
                send_line(client_fd, ssl, "ERROR UNAUTHORIZED\n");
            }
            continue;
        }

        if (!authenticated) {
            send_line(client_fd, ssl, "ERROR UNAUTHORIZED\n");
            continue;
        }

        if (strcmp(command, MSG_PING) == 0) {
            send_line(client_fd, ssl, "PONG\n");
            continue;
        }

        if (strcmp(command, MSG_GET) == 0) {
            char value[128];

            if (parts < 2 || metric_value(arg1, value, sizeof(value)) != 0) {
                if (parts >= 2) {
                    char response[BUFFER_SIZE];
                    snprintf(response, sizeof(response),
                             "ERROR %s UNKNOWN_OID\n", arg1);
                    send_line(client_fd, ssl, response);
                } else {
                    send_line(client_fd, ssl, "ERROR INVALID_REQUEST\n");
                }
                continue;
            }

            char response[BUFFER_SIZE];
            snprintf(response, sizeof(response),
                     "RESPONSE %s %s\n", arg1, value);
            send_line(client_fd, ssl, response);

            if (strcmp(arg1, OID_CPU) == 0) {
                double v = atof(value);
                if (v >= cpu_limit)
                    send_trap(trap_host, trap_port, arg1, v, cpu_limit, tls_client);
            } else if (strcmp(arg1, OID_MEMORY) == 0) {
                double v = atof(value);
                if (v >= memory_limit)
                    send_trap(trap_host, trap_port, arg1, v, memory_limit, tls_client);
            }

            continue;
        }

        if (strcmp(command, MSG_SET) == 0) {
            if (parts < 3) {
                send_line(client_fd, ssl, "ERROR INVALID_REQUEST\n");
                continue;
            }

            double new_value = atof(arg2);

            if (strcmp(arg1, OID_CPU_LIMIT) == 0) {
                if (new_value <= 0 || new_value > 100) {
                    send_line(client_fd, ssl, "ERROR INVALID_VALUE\n");
                } else {
                    cpu_limit = new_value;
                    send_line(client_fd, ssl, "SET_OK 3.1\n");
                }
            } else if (strcmp(arg1, OID_MEMORY_LIMIT) == 0) {
                if (new_value <= 0 || new_value > 100) {
                    send_line(client_fd, ssl, "ERROR INVALID_VALUE\n");
                } else {
                    memory_limit = new_value;
                    send_line(client_fd, ssl, "SET_OK 3.2\n");
                }
            } else {
                send_line(client_fd, ssl, "ERROR UNKNOWN_OID\n");
            }

            continue;
        }

        send_line(client_fd, ssl, "ERROR UNKNOWN_COMMAND\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Uso: %s <porta> <nome> [trap_host] [trap_port] [--tls]\n", argv[0]);
        printf("Exemplo: %s 5001 Agent1 127.0.0.1 6000 --tls\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    strncpy(agent_name, argv[2], sizeof(agent_name) - 1);

    const char *trap_host = NULL;
    int trap_port = 0;

    if (argc >= 5) {
        trap_host = argv[3];
        trap_port = atoi(argv[4]);
    }

    if (argc >= 6 && strcmp(argv[5], "--tls") == 0)
        tls_enabled = 1;

    signal(SIGPIPE, SIG_IGN);

    SSL_CTX *tls_server = NULL;
    SSL_CTX *tls_client = NULL;

    if (tls_enabled) {
        tls_init();
        tls_server = tls_server_context(TLS_AGENT_CERT, TLS_AGENT_KEY);
        tls_client = tls_client_context(TLS_AGENT_CERT, TLS_AGENT_KEY);
        if (!tls_server || !tls_client) {
            fprintf(stderr, "Falha ao inicializar TLS. Execute ./generate_certs.sh primeiro.\n");
            return 1;
        }
        /* O Agent autentica o Manager por certificado assinado pela mesma CA. */
        SSL_CTX_set_verify(tls_server, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
        if (SSL_CTX_load_verify_locations(tls_server, TLS_CA_FILE, NULL) <= 0) {
            fprintf(stderr, "Falha ao carregar CA para TLS.\n");
            return 1;
        }
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Agent %s iniciado na porta %d.\n", agent_name, port);
    printf("Threshold CPU: %.1f%% | Memoria: %.1f%%\n",
           cpu_limit, memory_limit);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr,
                               &client_len);

        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        SSL *ssl = NULL;
        if (tls_enabled) {
            ssl = tls_accept(tls_server, client_fd);
            if (!ssl) {
                close(client_fd);
                continue;
            }
        }

        handle_client(client_fd, ssl, trap_host, trap_port, tls_client);

        if (ssl) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
        close(client_fd);
    }

    close(server_fd);
    if (tls_server) SSL_CTX_free(tls_server);
    if (tls_client) SSL_CTX_free(tls_client);
    return 0;
}
