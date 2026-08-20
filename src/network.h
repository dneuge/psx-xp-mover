#ifndef XPMOVER_NETWORK_H
#define XPMOVER_NETWORK_H

#include <stdbool.h>
#include <stdlib.h>

#ifdef RESOLVED_ADDRESSES_T_REAL_TYPE
typedef RESOLVED_ADDRESSES_T_REAL_TYPE resolved_addresses_t;
#else
typedef void* resolved_addresses_t;
#endif

typedef int socket_t;

bool initialize_os_network_apis();

bool resolve_addresses(resolved_addresses_t *resolved_addresses, char *hostname);
void free_addresses(resolved_addresses_t *resolved_addresses);

bool connect_tcp_client(socket_t *out_sd, char *hostname, int port, resolved_addresses_t *resolved_addresses);
ssize_t read_network(socket_t sd, char *buffer, size_t buffer_size);
void write_network_string(socket_t sd, char *s);
void close_network_socket(socket_t sd);

#endif //XPMOVER_NETWORK_H
