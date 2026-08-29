#include <stdio.h>
#include <string.h>
#include <errno.h>

#if defined(TARGET_LINUX) || defined(TARGET_MACOS)
//#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
//#include <ifaddrs.h>
#include <unistd.h>

//static const int SOCKOPT_ENABLE_VALUE = 1;
//#define SOCKOPT_ENABLE_SIZE sizeof(int)
#elif TARGET_WINDOWS
// Windows API
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

// // Microsoft API docs:
// // [sdk-api] docs/sdk-api-src/content/winsock2/nf-winsock2-shutdown.md
//
//#define SHUT_RD SD_RECEIVE

//static const char SOCKOPT_ENABLE_VALUE = 1;
//#define SOCKOPT_ENABLE_SIZE sizeof(char)
#else
#error "OS-specific early parts of network.c are missing; target OS is not supported"
#endif

#define RESOLVED_ADDRESSES_T_REAL_TYPE struct addrinfo*

#include "logger.h"

#include "network.h"

// suppress SIGPIPE on Linux which has MSG_NOSIGNAL
// macOS has a socket option instead, Windows doesn't know the signal at all
#ifdef TARGET_LINUX
#define NETWORK_SEND_FLAGS (MSG_NOSIGNAL)
#else
#define NETWORK_SEND_FLAGS (0)
#endif

#define MAX_TCP_PORT (65535)
#define MAX_CONNECTION_HOSTNAME_LENGTH (255)

bool is_valid_tcp_port(int port) {
    return (port > 0) && (port <= MAX_TCP_PORT);
}

static inline bool is_valid_hostname_char(char ch) {
    return ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '.' || ch == '-');
}

bool is_valid_hostname(char *s) {
    if (!s) {
        return false;
    }

    size_t len = strlen(s);
    if (len < 1 || len > MAX_CONNECTION_HOSTNAME_LENGTH) {
        return false;
    }

    bool prev_dot = false;

    for (int i=0; i<len; i++) {
        char ch = s[i];
        if (!is_valid_hostname_char(ch)) {
            return false;
        }

        if (ch != '.') {
            prev_dot = false;
        } else {
            if (i == 0) {
                // leading dot is invalid
                return false;
            }

            if (prev_dot) {
                // consecutive dots are invalid
                return false;
            }

            prev_dot = true;
        }
    }

    return true;
}

bool resolve_addresses(resolved_addresses_t *resolved_addresses, char *hostname) {
    // Windows seems to be the same as POSIX; for future reference:
    // https://github.com/MicrosoftDocs/sdk-api/blob/07512580a99bac226f8730c8f85344270f1beeff/sdk-api-src/content/ws2tcpip/nf-ws2tcpip-getaddrinfo.md

    if (!hostname || !resolved_addresses) {
        return false;
    }

    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int err = getaddrinfo(hostname, NULL, &hints, &res);
    if (err) {
        MVLOG_WARN("address lookup failed: %s", gai_strerror(err));
        return false;
    }

    if (!res) {
        MVLOG_WARN("address lookup did not yield any results");
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
        MVLOG_WARN("connect_tcp_client missing parameters out_sd=%p, hostname=%p, resolved_addresses=%p", out_sd, hostname, resolved_addresses);
        return false;
    }

    if (!is_valid_tcp_port(port)) {
        MVLOG_WARN("connect_tcp_client called with invalid port %d", port);
        return false;
    }

    if (!is_valid_hostname(hostname)) {
        MVLOG_WARN("connect_tcp_client called with invalid hostname \"%s\"", hostname);
        return false;
    }

    MVLOG_INFO("connecting to %s:%d", hostname, port);

    if (!resolve_addresses(resolved_addresses, hostname) || !(*resolved_addresses)) {
        MVLOG_WARN("address not resolved, unable to connect");
        return false;
    }

    int sd = -1;

    // Windows seems to be the same as POSIX; for future reference:
    // https://github.com/MicrosoftDocs/sdk-api/blob/07512580a99bac226f8730c8f85344270f1beeff/sdk-api-src/content/ws2tcpip/nf-ws2tcpip-getaddrinfo.md
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

        MVLOG_DEBUG("connecting via IPv%d...", ip_version);
        sd = socket(resolved_address->ai_family, resolved_address->ai_socktype, resolved_address->ai_protocol);
        if (sd == -1) {
            MVLOG_WARN("failed to create socket: %d %s", errno, strerror(errno));
        } else {
            if (connect(sd, resolved_address->ai_addr, resolved_address->ai_addrlen) == 0) {
                *out_sd = sd;
                return true;
            }

            MVLOG_WARN("socket failed to connect: %d %s", errno, strerror(errno));
            close_network_socket(sd);
        }
    }

    return false;
}

ssize_t read_network(socket_t sd, char *buffer, size_t buffer_size) {
    return recv(sd, buffer, buffer_size, 0);
}

void write_network_string(socket_t sd, char *s) {
    send(sd, s, strlen(s), NETWORK_SEND_FLAGS);
}

#if defined(TARGET_LINUX) || defined(TARGET_MACOS)
#include "network_posix.c"
#elif TARGET_WINDOWS
#include "network_windows.c"
#endif