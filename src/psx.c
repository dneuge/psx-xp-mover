#include "psx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "utils.h"

#define BOOST_FIELD_SEPARATOR ';'
#define BOOST_FLAG_GROUND 'G'
#define BOOST_FLAG_FLIGHT 'F'
#define BOOST_NUM_FIELDS (8)

#define RECONNECT_DELAY_SECONDS (2)

#define RECV_BUFFER_SIZE (65535)
#define LINE_BUFFER_SIZE (1024)

const char exit_line[] = "exit\n";

void psx_print_boost_frame(psx_boost_frame_t *frame) {
    if (!frame) {
        printf("[XPMover] psx_print_boost_frame called with NULL\n");
        return;
    }

    printf(
        "[XPMover] [Boost] fd_lat=%.14lf, fd_lon=%.15lf, elev_m=%.1lf (%ld), hdg=%.1f (%d), pitch=%.1f (%d), bank=%.1f (%d), g=%d, ts=%u\n",
        frame->flight_deck_latitude, frame->flight_deck_longitude,
        frame->elevation_msl_meters, frame->flight_deck_altitude_msl_feet_hundreds,
        frame->heading_true_degrees, frame->heading_true_degrees_hundreds,
        frame->pitch_degrees, frame->pitch_degrees_hundreds,
        frame->bank_degrees, frame->bank_degrees_hundreds,
        frame->ground_contact,
        frame->timestamp_millis_part
    );
}

static bool parse_ground_flag(bool *dest, char *s, int length) {
    if (length != 1) {
        return false;
    }

    char ch = s[0];

    if (ch == BOOST_FLAG_GROUND) {
        *dest = true;
        return true;
    }

    if (ch == BOOST_FLAG_FLIGHT) {
        *dest = false;
        return true;
    }

    return false;
}

bool psx_parse_boost_frame(psx_boost_frame_t *frame, char *line) {
    if (!(frame && line)) {
        printf("[XPMover] psx_parse_boost_frame called with null: frame=%p, line=%p\n", frame, line);
        return false;
    }

    int field = 0;
    char *cursor = line;
    char *field_start = cursor;
    char ch = 0;
    bool success = false;
    while (true) {
        ch = *cursor;
        if (!ch || (ch == BOOST_FIELD_SEPARATOR)) {
            int length = cursor - field_start;

            switch (field) {
                case 0: success = parse_ground_flag(&frame->ground_contact, field_start, length); break;
                case 1: success = parse_long(&frame->flight_deck_altitude_msl_feet_hundreds, field_start, length); break;
                case 2: success = parse_int(&frame->heading_true_degrees_hundreds, field_start, length); break;
                case 3: success = parse_int(&frame->pitch_degrees_hundreds, field_start, length); break;
                case 4: success = parse_int(&frame->bank_degrees_hundreds, field_start, length); break;
                case 5: success = parse_double(&frame->flight_deck_latitude, field_start, length); break;
                case 6: success = parse_double(&frame->flight_deck_longitude, field_start, length); break;
                case 7: success = parse_int(&frame->timestamp_millis_part, field_start, length); break;
                default: return false;
            }

            if (!success) {
                return false;
            }

            field++;
            field_start = cursor + 1;

            if (!ch) {
                break;
            }
        }

        cursor++;
    }

    success = (field == BOOST_NUM_FIELDS);

    return success;
}

bool psx_recalculate_boost_frame(psx_boost_frame_t *frame) {
    frame->bank_degrees = -((float) frame->bank_degrees_hundreds) / 100.0f;
    frame->pitch_degrees = ((float) frame->pitch_degrees_hundreds) / 100.0f;
    frame->heading_true_degrees = ((float) frame->heading_true_degrees_hundreds) / 100.0f;

    // Details on how flight deck altitude is calculated/reversed: (equation used below)
    // https://aerowinx.com/board/index.php/topic,4471.msg47237.html#msg47237
    //
    // More detail on where the reference point is located:
    // between pilots (i.e. above middle of center pedestal), standard seat position, average height
    // https://aerowinx.com/board/index.php/topic,6867.msg74031.html
    //
    // Note that this *may* put the aircraft closer to (or even into) ground but it still is far from providing
    // an accurate position. Also note that pitch depends on ground elevation feedback. While we don't provide that
    // from this plugin, supplementary tooling may want to. However, we would need to recalculate pitch, then:
    // https://aerowinx.com/board/index.php/topic,5023.msg53571.html#msg53571
    // The reference point being different also means lat/lon is still somewhat misplaced. This is probably not an exact
    // science as it depends on where, for each use case, the reference point is needed. For visually moving a model in
    // place this is best corrected after turning lat/lon into cartesian local_* coordinates in X-Plane.
    // Keep in mind that this is not meant to be a proper "scenery generator".
    // If we can get reasonably good values: Great! But if we can't: It's not our focus (at least not yet).
    double flight_deck_altitude_msl_feet = ((double)frame->flight_deck_altitude_msl_feet_hundreds) / 100.0;
    double airframe_center_altitude_feet = flight_deck_altitude_msl_feet - (28.412073 + (92.5 * sin(deg2rad((double) frame->pitch_degrees))));
    frame->elevation_msl_meters = feet2meters(airframe_center_altitude_feet);

    return true;
}

static bool resolve_address(psx_client_t *client) {
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int err = getaddrinfo(client->hostname, NULL, &hints, &res);
    if (err) {
        printf("[XPMover] address lookup failed: %s\n", gai_strerror(err));
        return false;
    }

    if (!res) {
        printf("[XPMover] address lookup did not yield any results\n");
        return false;
    }

    if (client->resolved_addresses) {
        freeaddrinfo(client->resolved_addresses);
    }
    client->resolved_addresses = res;

    return true;
}

static int do_connect(psx_client_t *client) {
    printf("[XPMover] connecting to %s:%d\n", client->hostname, client->port);

    resolve_address(client);
    if (!client->resolved_addresses) {
        printf("[XPMover] address not resolved, unable to connect\n");
        return -1;
    }

    int sd = -1;

    for (struct addrinfo *resolved_address = client->resolved_addresses; resolved_address; resolved_address = resolved_address->ai_next) {
        if (resolved_address->ai_socktype != SOCK_STREAM) {
            continue;
        }

        int ip_version = 0;
        if (resolved_address->ai_family == AF_INET) {
            ((struct sockaddr_in*)resolved_address->ai_addr)->sin_port = ntohs(client->port);
            ip_version = 4;
        } else if (resolved_address->ai_family == AF_INET6) {
            ((struct sockaddr_in6*)resolved_address->ai_addr)->sin6_port = ntohs(client->port);
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
                return sd;
            }

            printf("[XPMover] socket failed to connect: %d %s\n", errno, strerror(errno));
            close(sd);
        }
    }

    return -1;
}

static bool on_line_received(psx_client_t *client, char *line) {
    psx_boost_frame_t boost_frame = {0};
    if (!psx_parse_boost_frame(&boost_frame, line)) {
        printf("[XPMover] failed to parse boost line\n");
        psx_print_boost_frame(&boost_frame);
        return false;
    }

    if (!psx_recalculate_boost_frame(&boost_frame)) {
        printf("[XPMover] failed to recalculate boost frame\n");
        psx_print_boost_frame(&boost_frame);
        return false;
    }

    client->on_boost_frame_callback(&boost_frame);

    return true;
}

static int run_connection_loop(void *ref) {
    psx_client_t *client = ref;
    printf("[XPMover] connection thread started\n");

    while (!client->should_shutdown) {
        // TODO: the mutex is currently not needed as hostname/port are constant; remove?
        thrd_sleep(&(struct timespec){.tv_sec=RECONNECT_DELAY_SECONDS}, NULL);

        int sd = do_connect(client);
        if (sd == -1) {
            printf("[XPMover] connection failed\n");
            continue;
        }

        printf("[XPMover] connected\n");
        char recv_buffer[RECV_BUFFER_SIZE] = {0,};
        char line_buffer[LINE_BUFFER_SIZE] = {0,};
        char *line_buffer_write_cursor = line_buffer;
        char *past_line_buffer = line_buffer + LINE_BUFFER_SIZE;
        bool success = true;
        while (!client->should_shutdown) {
            int num_received = read(sd, recv_buffer, RECV_BUFFER_SIZE);
            if (num_received <= 0) {
                break;
            }

            for (int i=0; i<num_received; i++) {
                char ch = recv_buffer[i];
                if (ch == '\n') {
                    *line_buffer_write_cursor = 0;
                    success = on_line_received(client, line_buffer);
                    if (!success) {
                        printf("[XPMover] failed to process line: %s\n", line_buffer);
                        break;
                    }

                    line_buffer_write_cursor = line_buffer;
                } else {
                    *line_buffer_write_cursor = ch;
                    line_buffer_write_cursor++;
                }

                if (line_buffer_write_cursor >= past_line_buffer) {
                    printf("[XPMover] excessive line length\n");
                    success = false;
                    break;
                }
            }

            if (!success) {
                break;
            }
        }

        printf("[XPMover] closing connection\n");
        write(sd, exit_line, strlen(exit_line));
        close(sd);
    }

    printf("[XPMover] connection thread terminated\n");
    return 0;
}

psx_client_t* create_psx_client(char *hostname, int port, psx_on_boost_frame_callback_f on_boost_frame_callback) {
    if (!(hostname && on_boost_frame_callback)) {
        printf("[XPMover] missing parameters to create_psx_client: hostname=%s, on_boost_frame_callback=%p\n", hostname, on_boost_frame_callback);
        return NULL;
    }

    psx_client_t *client = zmalloc(sizeof(psx_client_t));
    if (!client) {
        printf("[XPMover] failed to allocate PSX client instance\n");
        return NULL;
    }

    client->hostname = copy_string(hostname);
    if (!client->hostname) {
        printf("[XPMover] failed to copy hostname to PSX client\n");
        goto error;
    }

    client->port = port;
    client->on_boost_frame_callback = on_boost_frame_callback;

    client->mutex = (mtx_t) THREADS_MUTEX_INIT;

    if (mtx_init(&client->mutex, mtx_plain) != thrd_success) {
        printf("[XPMover] failed to create mutex for PSX client\n");
        goto error;
    }
    client->has_mutex = true;

    if (thrd_create(&client->thread, run_connection_loop, client) != thrd_success) {
        printf("[XPMover] failed to create connection thread for PSX client\n");
        goto error;
    }
    client->has_thread = true;

    return client;

error:
    if (client->has_mutex) {
        mtx_destroy(&client->mutex);
    }

    if (client->hostname) {
        free(client->hostname);
    }

    free(client);

    return NULL;
}

bool destroy_psx_client(psx_client_t *client) {
    if (!client) {
        printf("[XPMover] attempted to destroy NULL PSX client\n");
        return true;
    }

    // signal shutdown and wait for other thread to unblock
    client->should_shutdown = true;
    if (client->has_mutex) {
        if (mtx_lock(&client->mutex) != thrd_success) {
            printf("[XPMover] failed to lock mutex to shutdown connection thread\n");
            return false;
        }

        mtx_unlock(&client->mutex);
    }

    if (client->has_thread) {
        printf("[XPMover] joining connection thread\n");

        if (thrd_join(client->thread, (int*) NULL) != thrd_success) {
            printf("[XPMover] failed to join connection thread\n");
            return false;
        }
    }
    client->has_thread = false;

    if (client->has_mutex) {
        mtx_destroy(&client->mutex);
        client->has_mutex = false;
    }

    free(client->hostname);
    client->hostname = NULL;

    if (client->resolved_addresses) {
        freeaddrinfo(client->resolved_addresses);
        client->resolved_addresses = NULL;
    }

    free(client);

    return true;
}
