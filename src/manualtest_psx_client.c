#include <stdio.h>
#include <string.h>

#include "psx.h"
#include "utils.h"

#define DEFAULT_PORT (10749)

static void print_help() {
    printf("call with hostname/address and, optionally, port (default: %d)\n", DEFAULT_PORT);
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        print_help();
        return 1;
    }

    char *hostname = argv[1];

    int port = DEFAULT_PORT;
    if (argc > 2) {
        if (!parse_int(&port, argv[2], (int) strlen(argv[2]))) {
            print_help();
            return 1;
        }
    }

    printf("Creating client for host %s, port %d... (when connected: press any key to exit)\n", hostname, port);
    psx_client_t *client = create_psx_client(hostname, port, psx_print_boost_frame);
    if (!client) {
        printf("Failed to create client.\n");
        return 1;
    }

    getchar();

    if (!destroy_psx_client(client)) {
        printf("Failed to destroy client.\n");
        return 1;
    }

    printf("Terminated.\n");
    return 0;
}
