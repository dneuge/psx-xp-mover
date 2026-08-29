#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "logger.h"
#include "psx.h"
#include "utils.h"

#define DEFAULT_PORT (10749)

static void print_help() {
    printf("call with hostname/address and, optionally, port (default: %d)\n", DEFAULT_PORT);
}

#define NUM_STATS_EVALUATE (1000)
static int num_stats_recorded = 0;
static int timestamp_millis_parts[NUM_STATS_EVALUATE] = {0};

static void on_receive(psx_boost_frame_t *frame) {
    psx_log_boost_frame(frame, MVLOG_LEVEL_INFO);

    timestamp_millis_parts[num_stats_recorded] = frame->timestamp_millis_part;
    num_stats_recorded++;
    if (num_stats_recorded < NUM_STATS_EVALUATE) {
        return;
    }

    int previous_timestamp_millis_parts = timestamp_millis_parts[0];
    int min_interval = 9999;
    int max_interval = -1;
    uint64_t sum = 0;
    for (int i=1; i<NUM_STATS_EVALUATE; i++) {
        int current_timestamp_millis_parts = timestamp_millis_parts[i];
        int diff_timestamp_millis_parts = current_timestamp_millis_parts - previous_timestamp_millis_parts;
        while (diff_timestamp_millis_parts < 0) {
            diff_timestamp_millis_parts += 1000;
        }

        if (diff_timestamp_millis_parts < min_interval) {
            min_interval = diff_timestamp_millis_parts;
        }
        if (diff_timestamp_millis_parts > max_interval) {
            max_interval = diff_timestamp_millis_parts;
        }

        sum += diff_timestamp_millis_parts;

        previous_timestamp_millis_parts = current_timestamp_millis_parts;
    }
    num_stats_recorded = 0;

    double avg_interval = (double) sum / NUM_STATS_EVALUATE;
    double avg_fps = 1000 / avg_interval;

    MVLOG_INFO("[Stats] Boost frame intervals: min=%d, max=%d, avg=%.2f, fps=%.2f", min_interval, max_interval, avg_interval, avg_fps);
}

int main(int argc, char **argv) {
    xpmover_log_init();
    xpmover_set_min_log_level_console(MVLOG_LEVEL_TRACE);

    if (argc < 2 || argc > 3) {
        print_help();
        return 1;
    }

    if (!initialize_os_network_apis()) {
        printf("Failed to initialize OS network APIs.");
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
    psx_client_t *client = create_psx_client(hostname, port, on_receive, NULL, NULL);
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
