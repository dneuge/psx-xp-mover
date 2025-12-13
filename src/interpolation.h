#ifndef XPMOVER_INTERPOLATION_H
#define XPMOVER_INTERPOLATION_H

#define MAX_INTERPOLATOR_FRAMES (8) /* used for modulo; power of 2 may be beneficial */

/**
 * Maximum number of milliseconds allowed to have passed between two time indices to still be able to safely tell
 * if less than a second has passed when just tracking millisecond parts.
 */
#define MAX_INTERPOLATOR_FRAME_TIME_DIFF_MILLIS (499)

typedef struct {
    unsigned int timestamp_millis_part;
    double value;
} interpolator_key_frame_t;

typedef struct {
    unsigned int frames_start;
    unsigned int num_frames;

    /// frames must be at most {@ref MAX_INTERPOLATOR_FRAME_TIME_DIFF_MILLIS} apart but older data is allowed to be kept
    interpolator_key_frame_t frames[MAX_INTERPOLATOR_FRAMES];
} interpolator_t;

interpolator_t* create_interpolator();

void destroy_interpolator(interpolator_t *instance);

void interpolator_reset(interpolator_t *instance);

/**
 * Perform maintenance on interpolator frames.
 *
 * Since frames are tracked only by milliseconds part time difference the interval between frames must not exceed
 * {@ref MAX_INTERPOLATOR_FRAME_TIME_DIFF_MILLIS} to still be relatable to a second, otherwise interpolation may target
 * outdated values. Maintenance needs to be carried out in time intervals reasonably smaller than
 * {@ref MAX_INTERPOLATOR_FRAME_TIME_DIFF_MILLIS}.
 *
 * Note that larger time differences cannot be detected here. It is recommended to additionally track complete
 * timestamps outside interpolator implementation and reset frames when data went stale for a longer period.
 *
 * @param instance interpolator instance, may be null
 * @param timestamp_millis_part millisecond part of current time (using same reference as interpolator frames)
 */
void interpolator_maintain(interpolator_t *instance, double timestamp_millis_part);

/**
 * Adds a key frame for the given data point to the interpolator. Automatically performs maintenance on call.
 *
 * Note that data points are only time-referenced by millisecond parts; data points must be at most
 * {@ref MAX_INTERPOLATOR_FRAME_TIME_DIFF_MILLIS} milliseconds apart to be relatable without overflowing seconds.
 * If no new data is present, you may call {@ref interpolator_maintain} more frequently to ensure relatable data or
 * {@ref interpolator_reset} if larger intervals have passed.
 *
 * Timestamps are expected to be advancing strictly (timestamp_millis_part less than previous frame is interpreted as
 * "next second"). When calling twice using the same timestamp, the previous data point will be maintained unless
 * cleared during maintenance.
 *
 * Time is tracked solely based on timestamps passed during function calls, thus timestamps can relate to any time
 * reference (i.e. out of sync with local RTC) but all calls need to reference the same clock.
 *
 * @param instance interpolator instance, may be null
 * @param timestamp_millis_part millisecond part of timestamp to reference data for
 * @param value value at timestamp, must be finite and not NAN
 */
void interpolator_add_point(interpolator_t *instance, unsigned int timestamp_millis_part, double value);

/**
 * Returns an interpolated value for the specified time or {@ref NAN} if interpolation is not possible.
 *
 * At least two surrounding data points must be present to interpolate, so only data older than one frame interval
 * should be queried. The queried timestamp is assumed to be in the past; data points will be searched going back from
 * latest available key frame.
 *
 * Data points as well as the requested timestamp must be at most {@ref MAX_INTERPOLATOR_FRAME_TIME_DIFF_MILLIS}
 * milliseconds apart to be relatable without overflowing seconds. If calls to interpolate or add data points do not
 * occur frequently enough, {@ref interpolator_maintain} and/or {@ref interpolator_reset} must be called to prevent
 * outdated "replays".
 *
 * @param instance interpolator instance, may be null
 * @param timestamp_millis_part millisecond part of timestamp to interpolate data for (using same reference as interpolator frames)
 * @return interpolated value or {@ref NAN} if unavailable
 */
double interpolator_calculate(interpolator_t *instance, double timestamp_millis_part);

#endif //XPMOVER_INTERPOLATION_H