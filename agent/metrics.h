#ifndef METRICS_H
#define METRICS_H

double get_cpu_usage(void);
double get_memory_usage(void);
long long get_uptime(void);
long long get_network_rx(void);
long long get_network_tx(void);
long long get_network_packets_rx(void);
long long get_network_packets_tx(void);
long long get_active_connections(void);

#endif
