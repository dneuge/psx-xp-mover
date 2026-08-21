#include <math.h>
#include <stdlib.h>

#include "logger.h"
#include "utils.h"

#include "interpolation.h"

static int ifloor(double value) {
    return (int) lround(floor(value));
}

static unsigned int uilimiti(int value, unsigned int min_incl, unsigned int max_incl) {
    if (value < min_incl) {
        return min_incl;
    }
    if (value > max_incl) {
        return max_incl;
    }
    return (unsigned int) value;
}

static int ilimit(int value, int min_incl, int max_incl) {
    if (value < min_incl) {
        return min_incl;
    }
    if (value > max_incl) {
        return max_incl;
    }
    return value;
}

static double dlimit(double value, double a_incl, double b_incl) {
    double min_incl;
    double max_incl;
    if (a_incl <= b_incl) {
        min_incl = a_incl;
        max_incl = b_incl;
    } else {
        min_incl = b_incl;
        max_incl = a_incl;
    }

    if (value < min_incl) {
        return min_incl;
    }

    if (value > max_incl) {
        return max_incl;
    }

    return value;
}

/**
 * Calculates modulo, counting negative numbers down from divisor, always resulting in positive numbers (hence "pmod").
 *
 * Example: Standard operation in C is -4 % 3 = -1 (same as 4 % 3 with negative sign restored on result).
 *          This operation results in  -4 ipmod 3 = 2 (as in 3-1 = 2)
 *                                     -3 ipmod 3 = 0
 *                                     -2 ipmod 3 = 1
 *                                     -1 ipmod 3 = 2
 *
 * @param value value to calculate modulo for
 * @param divisor divisor of modulo operation
 * @return "positive modulo", see explanation above; guaranteed to be positive
 */
static int ipmod(int value, int divisor) {
    int res = value % divisor;
    return (res >= 0) ? res : (res + divisor);
}

interpolator_t* create_interpolator() {
    return zmalloc(sizeof(interpolator_t));
}

void interpolator_reset(interpolator_t *instance) {
    if (!instance) {
        return;
    }

    instance->num_frames = 0;
}

static void interpolator_dump(interpolator_t *instance) {
#if (MVLOG_COMPILED_MIN_LOG_LEVEL <= MVLOG_LEVEL_TRACE)
    if (!xpmover_is_log_level_enabled(MVLOG_LEVEL_TRACE)) {
        return;
    }

    unsigned int remaining_frames = instance->num_frames;
    MVLOG_TRACE("dump num_frames=%d", remaining_frames);
    int frame_index = (int) instance->frames_start;
    while (remaining_frames > 0) {
        interpolator_key_frame_t *frame = &instance->frames[frame_index];
        MVLOG_TRACE("     #%02d %03d %f", frame_index, frame->timestamp_millis_part, frame->value);
        frame_index = ipmod(frame_index + 1, MAX_INTERPOLATOR_FRAMES);
        remaining_frames--;
    }
#endif
}

void interpolator_maintain(interpolator_t *instance, double timestamp_millis_part) {
    if (!instance) {
        return;
    }

    unsigned int num_frames = instance->num_frames;
    if (num_frames == 0) {
        return;
    }

    unsigned int latest_frame_index = ipmod(instance->frames_start + num_frames - 1, MAX_INTERPOLATOR_FRAMES);
    interpolator_key_frame_t *latest_frame = &instance->frames[latest_frame_index];

    int timestamp_millis_part_int = ilimit(ifloor(timestamp_millis_part), 0, 999);
    int timestamp_millis_diff = timestamp_millis_part_int - (int) latest_frame->timestamp_millis_part;
    if (timestamp_millis_diff < 0) {
        timestamp_millis_diff += 1000;
    }

    if (timestamp_millis_diff < 0 || timestamp_millis_diff >= MAX_INTERPOLATOR_FRAME_TIME_DIFF_MILLIS) {
        MVLOG_DEBUG("maintenance resets interpolator; timestamp_millis_part=%.2f => %d, L%02d %03d => diff %d", timestamp_millis_part, timestamp_millis_part_int, latest_frame_index, latest_frame->timestamp_millis_part, timestamp_millis_diff);
        interpolator_reset(instance);
    }
}

void interpolator_add_point(interpolator_t *instance, unsigned int timestamp_millis_part, double value) {
    if (!instance) {
        return;
    }

    interpolator_maintain(instance, timestamp_millis_part);

    if (!isfinite(value)) {
        // NAN or not finite - ignore
        return;
    }

    int new_frame_index = (int) instance->frames_start;
    int num_frames = (int) instance->num_frames;
    if (num_frames == 0) {
        num_frames = 1;
    } else {
        unsigned int latest_frame_index = ipmod(instance->frames_start + num_frames - 1, MAX_INTERPOLATOR_FRAMES);
        interpolator_key_frame_t *latest_frame = &instance->frames[latest_frame_index];
        if (latest_frame->timestamp_millis_part == timestamp_millis_part) {
            // keep first recorded data point, as documented
            return;
        }

        if (num_frames >= MAX_INTERPOLATOR_FRAMES) {
            instance->frames_start = ipmod(new_frame_index + 1, MAX_INTERPOLATOR_FRAMES);
            num_frames = MAX_INTERPOLATOR_FRAMES;
        } else {
            new_frame_index = ipmod(new_frame_index + num_frames, MAX_INTERPOLATOR_FRAMES);
            num_frames++;
        }
    }
    instance->num_frames = num_frames;

    interpolator_key_frame_t *frame = &instance->frames[new_frame_index];
    frame->timestamp_millis_part = timestamp_millis_part;
    frame->value = value;
}

double interpolator_calculate(interpolator_t *instance, double timestamp_millis_part) {
    //interpolator_maintain(instance, timestamp_millis_part);

    if (!instance || (instance->num_frames < 2)) {
        return NAN;
    }

    MVLOG_TRACE("--- INTERPOLATOR");
    MVLOG_TRACE("query time %.6f", timestamp_millis_part);
    interpolator_dump(instance);

    int target_frame_index = ipmod(instance->frames_start + instance->num_frames - 1, MAX_INTERPOLATOR_FRAMES);
    interpolator_key_frame_t *target_frame = &instance->frames[target_frame_index];
    double time_to_target_frame = target_frame->timestamp_millis_part - timestamp_millis_part;
    if (time_to_target_frame < 0) {
        time_to_target_frame += 1000;
    }
    MVLOG_TRACE("time check T%02d %03d => time_to_target_frame=%8.6fms", target_frame_index, target_frame->timestamp_millis_part, time_to_target_frame);
    if (time_to_target_frame > MAX_INTERPOLATOR_FRAME_TIME_DIFF_MILLIS) {
        MVLOG_WARN("max interpolator frame time difference exceeded: %.6f (off by %.2fms)", time_to_target_frame, (1000-time_to_target_frame));
        return NAN;
    }

    int remaining_frames = instance->num_frames - 1;
    bool between_frames = false;
    interpolator_key_frame_t *base_frame = target_frame; // default assign to target frame to mitigate risk of accidentally being null/invalid
    while (remaining_frames > 0) {
        int base_frame_index = ipmod(target_frame_index - 1, MAX_INTERPOLATOR_FRAMES);
        base_frame = &instance->frames[base_frame_index];
        remaining_frames--;

        MVLOG_TRACE("check B%02d %03d %.6f", base_frame_index, base_frame->timestamp_millis_part, base_frame->value);
        MVLOG_TRACE("check T%02d %03d %.6f", target_frame_index, target_frame->timestamp_millis_part, target_frame->value);

        int time_between_frames = (int) target_frame->timestamp_millis_part - (int) base_frame->timestamp_millis_part;
        if (time_between_frames < 0) {
            time_between_frames += 1000;
        }

        if (time_to_target_frame <= time_between_frames) {
            between_frames = true;
            break;
        }

        target_frame_index = base_frame_index;
        target_frame = base_frame;
        time_to_target_frame -= time_between_frames;
    }

    if (!between_frames) {
        MVLOG_DEBUG("abort interpolation, not between frames");
        return NAN;
    }

    int millis_between_frames = (int) target_frame->timestamp_millis_part - (int) base_frame->timestamp_millis_part;
    if (millis_between_frames <= 0) {
        millis_between_frames += 1000;
    }

    double millis_since_base = timestamp_millis_part - base_frame->timestamp_millis_part;
    if (millis_since_base < 0) {
        millis_since_base += 1000;
    }

    // no need to calculate anything if we would end up exactly on a data point or are out of range (conversion error)
    if (millis_since_base <= 0) {
        MVLOG_TRACE("abort interpolation with base frame; millis_since_base=%f", millis_since_base);
        return base_frame->value;
    } else if (millis_since_base >= millis_between_frames) {
        MVLOG_TRACE("abort interpolation with target frame; millis_since_base=%f, millis_between_frames=%d", millis_since_base, millis_between_frames);
        return target_frame->value;
    }

    double fraction = dlimit(millis_since_base / millis_between_frames, 0.0, 1.0);
    double value_diff = target_frame->value - base_frame->value;
    double interpolated = dlimit(base_frame->value + (fraction * value_diff), base_frame->value, target_frame->value);

    MVLOG_TRACE(
        "interpolation res %.6f (millis_since_base=%f, millis_between_frames=%d, base=%.6f, target=%.6f, fraction=%.3f)",
        interpolated, millis_since_base, millis_between_frames, base_frame->value, target_frame->value, fraction
    );

    return interpolated;
}

void destroy_interpolator(interpolator_t *instance) {
    if (!instance) {
        return;
    }

    free(instance);
}
