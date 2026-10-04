#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "metrics.h"

static int read_cpu(unsigned long long *total, unsigned long long *idle) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;

    unsigned long long user, nice, system, idle_v, iowait;
    unsigned long long irq, softirq, steal;

    int ok = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                    &user, &nice, &system, &idle_v, &iowait,
                    &irq, &softirq, &steal);
    fclose(f);

    if (ok != 8) return -1;

    *idle = idle_v + iowait;
    *total = user + nice + system + idle_v + iowait +
             irq + softirq + steal;
    return 0;
}

double get_cpu_usage(void) {
    unsigned long long total1, idle1, total2, idle2;

    if (read_cpu(&total1, &idle1) != 0) return -1.0;
    struct timespec ts = {0, 100000000};
    nanosleep(&ts, NULL);
    if (read_cpu(&total2, &idle2) != 0) return -1.0;

    unsigned long long total_delta = total2 - total1;
    unsigned long long idle_delta = idle2 - idle1;

    if (total_delta == 0) return 0.0;

    return 100.0 * (1.0 - ((double)idle_delta / total_delta));
}

double get_memory_usage(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1.0;

    long long total = 0;
    long long available = 0;
    char key[64];
    long long value;
    char unit[16];

    while (fscanf(f, "%63s %lld %15s", key, &value, unit) == 3) {
        if (strcmp(key, "MemTotal:") == 0)
            total = value;
        else if (strcmp(key, "MemAvailable:") == 0)
            available = value;
    }

    fclose(f);

    if (total <= 0) return -1.0;

    return 100.0 * ((double)(total - available) / total);
}

long long get_uptime(void) {
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return -1;

    double seconds;
    int ok = fscanf(f, "%lf", &seconds);
    fclose(f);

    if (ok != 1) return -1;
    return (long long)seconds;
}

static long long network_value(int field) {
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) return -1;

    char line[512];
    long long total = 0;

    while (fgets(line, sizeof(line), f)) {
        if (!strchr(line, ':')) continue;

        char iface[64];
        unsigned long long rx_bytes, rx_packets;
        unsigned long long tx_bytes, tx_packets;
        unsigned long long dummy[12];

        int n = sscanf(line, " %63[^:]: %llu %llu %llu %llu %llu %llu %llu %llu "
                             "%llu %llu %llu %llu %llu %llu %llu %llu",
                       iface,
                       &rx_bytes, &rx_packets,
                       &dummy[0], &dummy[1], &dummy[2], &dummy[3], &dummy[4], &dummy[5],
                       &tx_bytes, &tx_packets,
                       &dummy[6], &dummy[7], &dummy[8], &dummy[9], &dummy[10], &dummy[11]);

        if (n >= 11) {
            if (strcmp(iface, "lo") == 0) continue;
            if (field == 0) total += (long long)rx_bytes;
            else if (field == 1) total += (long long)tx_bytes;
            else if (field == 2) total += (long long)rx_packets;
            else if (field == 3) total += (long long)tx_packets;
        }
    }

    fclose(f);
    return total;
}

long long get_network_rx(void) { return network_value(0); }
long long get_network_tx(void) { return network_value(1); }
long long get_network_packets_rx(void) { return network_value(2); }
long long get_network_packets_tx(void) { return network_value(3); }

long long get_active_connections(void) {
    FILE *f = fopen("/proc/net/tcp", "r");
    if (!f) return -1;

    char line[512];
    long long count = 0;

    fgets(line, sizeof(line), f); /* header */

    while (fgets(line, sizeof(line), f)) {
        unsigned int local_ip, remote_ip, state;
        unsigned int local_port, remote_port;

        int n = sscanf(line,
                       " %*d: %x:%x %x:%x %x",
                       &local_ip, &local_port,
                       &remote_ip, &remote_port,
                       &state);

        if (n == 5 && state == 0x01)
            count++;
    }

    fclose(f);
    return count;
}
