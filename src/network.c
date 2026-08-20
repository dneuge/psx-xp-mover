#include <stdio.h>
#include <string.h>
#include <errno.h>

//#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
//#include <ifaddrs.h>
#include <unistd.h>

//static const int SOCKOPT_ENABLE_VALUE = 1;
//#define SOCKOPT_ENABLE_SIZE sizeof(int)

#define RESOLVED_ADDRESSES_T_REAL_TYPE struct addrinfo*

#include "network.h"

bool initialize_os_network_apis() {
    // nothing to do for this target system
    return true;
}

bool resolve_addresses(resolved_addresses_t *resolved_addresses, char *hostname) {
    if (!hostname || !resolved_addresses) {
        return false;
    }

    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int err = getaddrinfo(hostname, NULL, &hints, &res);
    if (err) {
        printf("[XPMover] address lookup failed: %s\n", gai_strerror(err));
        return false;
    }

    if (!res) {
        printf("[XPMover] address lookup did not yield any results\n");
        return false;
    }

    free_addresses(resolved_addresses);
    *resolved_addresses = res;

    return true;
}

void free_addresses(resolved_addresses_t *resolved_addresses) {
    if (!resolved_addresses || !*resolved_addresses) {
        return;
    }

    freeaddrinfo(*resolved_addresses);
    *resolved_addresses = NULL;
}

bool connect_tcp_client(socket_t *out_sd, char *hostname, int port, resolved_addresses_t *resolved_addresses) {
    if (!out_sd || !hostname || !resolved_addresses) {
        printf("[XPMover] connect_tcp_client missing parameters out_sd=%p, hostname=%p, resolved_addresses=%p\n", out_sd, hostname, resolved_addresses);
        return false;
    }

    printf("[XPMover] connecting to %s:%d\n", hostname, port);

    if (!resolve_addresses(resolved_addresses, hostname) || !(*resolved_addresses)) {
        printf("[XPMover] address not resolved, unable to connect\n");
        return false;
    }

    int sd = -1;

    for (struct addrinfo *resolved_address = *resolved_addresses; resolved_address; resolved_address = resolved_address->ai_next) {
        if (resolved_address->ai_socktype != SOCK_STREAM) {
            continue;
        }

        int ip_version = 0;
        if (resolved_address->ai_family == AF_INET) {
            ((struct sockaddr_in*)resolved_address->ai_addr)->sin_port = ntohs(port);
            ip_version = 4;
        } else if (resolved_address->ai_family == AF_INET6) {
            ((struct sockaddr_in6*)resolved_address->ai_addr)->sin6_port = ntohs(port);
            ip_version = 6;
        } else {
            continue;
        }

        printf("[XPMover] connecting via IPv%d...\n", ip_version);
        sd = socket(resolved_address->ai_family, resolved_address->ai_socktype, resolved_address->ai_protocol);
        if (sd == -1) {
            printf("[XPMover] failed to create socket: %d %s\n", errno, strerror(errno));
        } else {
            if (connect(sd, resolved_address->ai_addr, resolved_address->ai_addrlen) == 0) {
                *out_sd = sd;
                return true;
            }

            printf("[XPMover] socket failed to connect: %d %s\n", errno, strerror(errno));
            close_network_socket(sd);
        }
    }

    return false;
}

ssize_t read_network(socket_t sd, char *buffer, size_t buffer_size) {
    return read(sd, buffer, buffer_size);
}

void write_network_string(socket_t sd, char *s) {
    write(sd, s, strlen(s));
}

void close_network_socket(socket_t sd) {
    close(sd);
}
