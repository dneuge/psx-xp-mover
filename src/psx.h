#ifndef PSX_H
#define PSX_H

#include <stdbool.h>

#include "threads_compat.h"

#include "logger.h"
#include "network.h"

typedef struct {
    // received over network
    bool ground_contact;
    long flight_deck_altitude_msl_feet_hundreds;
    int heading_true_degrees_hundreds;
    int pitch_degrees_hundreds; // positive = nose up, negative = nose down
    int bank_degrees_hundreds; // positive = right, negative = left
    double flight_deck_latitude; // positive = north, negative = south
    double flight_deck_longitude; // positive = east, negative = west
    int timestamp_millis_part;

    // calculated locally
    double elevation_msl_meters;
    float heading_true_degrees;
    float pitch_degrees;
    float bank_degrees;
} psx_boost_frame_t;

static inline void psx_log_boost_frame(psx_boost_frame_t *frame, xpmover_log_level_t level) {
    if (MVLOG_COMPILED_MIN_LOG_LEVEL > level) {
        return;
    }

    if (!frame) {
        MVLOG_WARN("psx_log_boost_frame called with NULL");
        return;
    }

    xpmover_log(
        level, "[Boost] fd_lat=%.14lf, fd_lon=%.15lf, elev_m=%.1lf (%ld), hdg=%.1f (%d), pitch=%.1f (%d), bank=%.1f (%d), g=%d, ts=%u",
        frame->flight_deck_latitude, frame->flight_deck_longitude,
        frame->elevation_msl_meters, frame->flight_deck_altitude_msl_feet_hundreds,
        frame->heading_true_degrees, frame->heading_true_degrees_hundreds,
        frame->pitch_degrees, frame->pitch_degrees_hundreds,
        frame->bank_degrees, frame->bank_degrees_hundreds,
        frame->ground_contact,
        frame->timestamp_millis_part
    );
}

bool psx_parse_boost_frame(psx_boost_frame_t *frame, char *line);

bool psx_recalculate_boost_frame(psx_boost_frame_t *frame);

typedef void (*psx_on_boost_frame_callback_f) (psx_boost_frame_t *boost_frame);

typedef struct {
    char *hostname;
    int port;

    psx_on_boost_frame_callback_f on_boost_frame_callback;

    mtx_t mutex;
    bool has_mutex;

    thrd_t thread;
    bool has_thread;
    resolved_addresses_t resolved_addresses;

    bool should_shutdown;
} psx_client_t;

psx_client_t* create_psx_client(char *hostname, int port, psx_on_boost_frame_callback_f on_boost_frame_callback);
bool destroy_psx_client(psx_client_t *client);

#endif //PSX_H
