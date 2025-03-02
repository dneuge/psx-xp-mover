#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <XPLMDataAccess.h>
#include <XPLMGraphics.h>
#include <XPLMProcessing.h>
#include <XPLMPlugin.h>
#include <XPLMScenery.h>

#include "psx.h"
#include "utils.h"

// forward-declaration to roll back partial initialization when XPluginEnable() fails
PLUGIN_API void XPluginDisable();

#define CALL_ON_NEXT_FRAME (-1.0f)

static XPLMFlightLoopID flight_loop_after_flight_model_id = {0};
static bool flight_loop_registered = false;

// NOTE: offset corrections are more a rough guess from experimentation than exact measurements
static double model_height_offset_meters = 3.8;    // visually: roughly the height from ground to lower 1/3 of outer engines; adjust with ground pinning
static double model_length_offset_meters = 28.194; /* observable by changing HDG in PSX on a parking position, aircraft rotates
                                                      at tail; visually: a bit less than half the fuselage offset from main gear;
                                                      mainly caused by offset of PSX flight deck from gear but not exactly the
                                                      documented length
                                                      28.194m is the documented offset from "flight deck" to aircraft center,
                                                      note that the PSX boost server will send lat/lon for the "flight deck"
                                                      view point, not the aircraft center, so debug_spin_hdg is misleading */
static double debug_spin_hdg = 0.0;

const char dataref_name_model_length_offset[] = "xpmover/model_offset/length";
static XPLMDataRef dataref_model_length_offset = NULL;

const char dataref_name_model_height_offset[] = "xpmover/model_offset/height";
static XPLMDataRef dataref_model_height_offset = NULL;

const char dataref_name_debug_spin_hdg[] = "xpmover/debug/spin_hdg";
static XPLMDataRef dataref_debug_spin_hdg = NULL;

const char dataref_name_terrain_elevation_meters[] = "xpmover/terrain/elevation_msl_meters";
static XPLMDataRef dataref_terrain_elevation_meters = NULL;

const char dataref_name_terrain_elevation_remaining_cycles[] = "xpmover/terrain/remaining_cycles";
static XPLMDataRef dataref_terrain_elevation_remaining_cycles = NULL;

const char dataref_name_ground_contact_cycles[] = "xpmover/terrain/blending/ground_contact_cycles";
static XPLMDataRef dataref_ground_contact_cycles = NULL;

const char dataref_name_ground_contact_fraction[] = "xpmover/terrain/blending/ground_contact_fraction";
static XPLMDataRef dataref_ground_contact_fraction = NULL;

const char dataref_name_firm_ground_speed[] = "xpmover/terrain/blending/firm_ground_speed";
static XPLMDataRef dataref_firm_ground_speed = NULL;

const char dataref_name_lift_ground_speed[] = "xpmover/terrain/blending/lift_ground_speed";
static XPLMDataRef dataref_lift_ground_speed = NULL;

const char dataref_name_low_speed_fraction[] = "xpmover/terrain/blending/low_speed_fraction";
static XPLMDataRef dataref_low_speed_fraction = NULL;

const char dataref_name_low_speed_fraction_factor[] = "xpmover/terrain/blending/low_speed_fraction_factor";
static XPLMDataRef dataref_low_speed_fraction_factor = NULL;

const char dataref_name_elevation_blending_fraction[] = "xpmover/terrain/blending/elevation_blending_fraction";
static XPLMDataRef dataref_elevation_blending_fraction = NULL;

const char dataref_name_calculated_ground_speed[] = "xpmover/ground_speed_calculated";
static XPLMDataRef dataref_calculated_ground_speed = NULL;

const char dataref_name_publish_ground_speed[] = "xpmover/publish/ground_speed_calculated";
static XPLMDataRef dataref_publish_ground_speed = NULL;

const char dataref_name_publish_motion_vector[] = "xpmover/publish/motion_vector";
static XPLMDataRef dataref_publish_motion_vector = NULL;

#define DATAREF_WRITABLE (1)

typedef int xpint_t;
typedef float xpfloat_t;
typedef double xpdouble_t;

static XPLMDataRef dataref_override_oxygen = NULL;
static XPLMDataRef dataref_override_pressurization = NULL;
static XPLMDataRef dataref_override_planepath = NULL;
static XPLMDataRef dataref_cabin_altitude = NULL;
static XPLMDataRef dataref_pilot_felt_altitude = NULL;
static XPLMDataRef dataref_psi_hdg = NULL;
static XPLMDataRef dataref_phi_roll = NULL;
static XPLMDataRef dataref_theta_pitch = NULL;
static XPLMDataRef dataref_local_x = NULL;
static XPLMDataRef dataref_local_y = NULL;
static XPLMDataRef dataref_local_z = NULL;
static XPLMDataRef dataref_local_vx = NULL;
static XPLMDataRef dataref_local_vy = NULL;
static XPLMDataRef dataref_local_vz = NULL;
static XPLMDataRef dataref_ground_speed = NULL;
static XPLMDataRef dataref_ground_speed2 = NULL;
static bool datarefs_initialized = false;

static XPLMProbeRef probe = NULL;
static XPLMProbeInfo_t *probe_info = NULL;
static double terrain_elevation_meters = NAN;
static int terrain_elevation_remaining_cycles = 0;
#define TERRAIN_ELEVATION_MAX_AGE_CYCLES (30 /* seconds */ * 60 /* FPS */) /* should be set much higher than ground_contact_cycles */

#define MAX_NUM_RAW_ELEVATION_BLENDING_FRACTIONS (5) /* how many cycles should be averaged to smooth elevation blending? */
#define MAX_GROUND_CONTACT_CYCLES (2 /* seconds */ * 60 /* FPS */)
static int ground_contact_cycles = 0; // sliding cycle count of PSX signalling ground contact (incrementing to max cycles) or flight (decrementing to zero)
static double ground_contact_fraction = 0.0; // fraction of ground_contact_cycles/max, 1.0 = "long enough in ground contact", 0.0 = "long enough in flight"
static double firm_ground_speed = 60.0;  // approximate ground speed at which the aircraft should be firmly pinned to ground
static double lift_ground_speed = 160.0; // approximate ground speed at which the aircraft should have left ground
static double low_speed_fraction = 0.0; // fraction between "firm" (1.0) and "lift" (0.0) ground speeds
static double low_speed_fraction_factor = 0.8; // controls the effect of low_speed_fraction on overall blending fraction
static double elevation_blending_fraction = 0.0; // blends between XP and PSX elevations; 1.0 = fully pin to X-Plane terrain, 0.0 = fully apply PSX value; smoothed output
static double raw_elevation_blending_fractions[MAX_NUM_RAW_ELEVATION_BLENDING_FRACTIONS] = {0}; // raw blending fractions (not smoothed yet)
static int next_raw_elevation_blending_fractions_index = 0;
static int num_raw_elevation_blending_fractions = 0;

#define PSX_MAX_FPS (77)
#define PSX_MAX_TIME_ACCELERATION_FACTOR (64)
#define MAX_REALTIME_GROUND_SPEED (700)
#define MAX_GROUND_SPEED (PSX_MAX_TIME_ACCELERATION_FACTOR * MAX_REALTIME_GROUND_SPEED)

#define MILLISECONDS_PER_SECOND (1000)
#define MILLISECONDS_PER_HOUR (3600 * MILLISECONDS_PER_SECOND)

static double calculated_ground_speed = 0.0;
static bool publish_ground_speed = true;
static bool publish_motion_vector = true;

static xpint_t disable_planepath[] = {1};

static psx_client_t *psx_client = NULL;

static psx_boost_frame_t boost_frame = {0};
static bool boost_frame_applied = true;
static mtx_t boost_frame_mutex;
static bool has_boost_frame_mutex = false;

#define MAX_NUM_PREVIOUS_BOOST_FRAMES (2 * PSX_MAX_FPS)
static psx_boost_frame_t previous_boost_frames[MAX_NUM_PREVIOUS_BOOST_FRAMES] = {0};
static int previous_boost_frame_index = 0;
static int num_previous_boost_frames = 0;

#define INIT_CYCLES_TO_OVERRIDE_PLANE_PATH (10);
static int cycles_to_override_plane_path = INIT_CYCLES_TO_OVERRIDE_PLANE_PATH;

#define DATAREF_EDITOR_PLUGIN_NAME "xplanesdk.examples.DataRefEditor"
#define DATAREF_EDITOR_MSG_ADD_DATAREF (0x01000000)
static XPLMPluginID dataref_editor_plugin_id = XPLM_NO_PLUGIN_ID;

static void on_boost_frame_received(psx_boost_frame_t *new_boost_frame) {
    if (mtx_lock(&boost_frame_mutex) != thrd_success) {
        printf("[XPMover] receive callback failed to lock boost frame mutex\n");
        return;
    }

    boost_frame = *new_boost_frame;
    boost_frame_applied = false;

    mtx_unlock(&boost_frame_mutex);
}

static bool find_dataref(XPLMDataRef *dest, char *name) {
    *dest = XPLMFindDataRef(name);
    return (*dest) != NULL;
}

static void announce_dataref(const char *dataref_name) {
    if (dataref_editor_plugin_id == XPLM_NO_PLUGIN_ID) {
        dataref_editor_plugin_id = XPLMFindPluginBySignature(DATAREF_EDITOR_PLUGIN_NAME);
    }

    if (dataref_editor_plugin_id == XPLM_NO_PLUGIN_ID) {
        printf("[XPMover] DataRefEditor does not appear to be installed, unable to announce dataref\n");
        return;
    }

    XPLMSendMessageToPlugin(dataref_editor_plugin_id, DATAREF_EDITOR_MSG_ADD_DATAREF, (void*) dataref_name);
}

static bool register_double_dataref(XPLMDataRef *dest, const char *inDataName, XPLMGetDatad_f inReadDouble, void *inReadRefcon, XPLMSetDatad_f inWriteDouble, void *inWriteRefcon) {
    if (!inDataName) {
        printf("[XPMover] tried to register nameless dataref\n");
        return false;
    }

    if (!dest) {
        printf("[XPMover] tried to register dataref %s without destination\n", inDataName);
        return false;
    }

    if (!inReadDouble || !inWriteDouble) {
        printf("[XPMover] tried to register dataref %s without functions\n", inDataName);
        return false;
    }

    *dest = XPLMRegisterDataAccessor(
        inDataName, xplmType_Double, DATAREF_WRITABLE,
        NULL, NULL,
        NULL, NULL,
        inReadDouble, inWriteDouble,
        NULL, NULL,
        NULL, NULL,
        NULL, NULL,
        inReadRefcon,
        inWriteRefcon
    );

    announce_dataref(inDataName);

    return (*dest != NULL);
}

static double get_double(void *inRefcon) {
    return *((double*)inRefcon);
}

static void set_double(void *inRefcon, double value) {
    *((double*)inRefcon) = value;
}

static bool expose_double_as_dataref(XPLMDataRef *dest, const char *dataref_name, double *value_ref) {
    if (!value_ref) {
        printf("[XPMover] tried to expose %s with NULL reference\n", dataref_name);
        return false;
    }
    return register_double_dataref(dest, dataref_name, get_double, value_ref, set_double, value_ref);
}

static bool register_int_dataref(XPLMDataRef *dest, const char *inDataName, XPLMGetDatai_f inReadInt, void *inReadRefcon, XPLMSetDatai_f inWriteInt, void *inWriteRefcon) {
    if (!inDataName) {
        printf("[XPMover] tried to register nameless dataref\n");
        return false;
    }

    if (!dest) {
        printf("[XPMover] tried to register dataref %s without destination\n", inDataName);
        return false;
    }

    if (!inReadInt || !inWriteInt) {
        printf("[XPMover] tried to register dataref %s without functions\n", inDataName);
        return false;
    }

    *dest = XPLMRegisterDataAccessor(
        inDataName, xplmType_Int, DATAREF_WRITABLE,
        inReadInt, inWriteInt,
        NULL, NULL,
        NULL, NULL,
        NULL, NULL,
        NULL, NULL,
        NULL, NULL,
        inReadRefcon,
        inWriteRefcon
    );

    announce_dataref(inDataName);

    return (*dest != NULL);
}

static int get_int(void *inRefcon) {
    return *((int*)inRefcon);
}

static void set_int(void *inRefcon, int value) {
    *((int*)inRefcon) = value;
}

static bool expose_int_as_dataref(XPLMDataRef *dest, const char *dataref_name, int *value_ref) {
    if (!value_ref) {
        printf("[XPMover] tried to expose %s with NULL reference\n", dataref_name);
        return false;
    }
    return register_int_dataref(dest, dataref_name, get_int, value_ref, set_int, value_ref);
}

static void unregister_dataref(XPLMDataRef *dataref) {
    if (!dataref) {
        return;
    }

    XPLMUnregisterDataAccessor(*dataref);
    *dataref = NULL;
}

static int get_bool(void *inRefcon) {
    return *((bool*)inRefcon) ? 1 : 0;
}

static void set_bool(void *inRefcon, int value) {
    *((bool*)inRefcon) = (value != 0);
}

static bool expose_bool_as_dataref(XPLMDataRef *dest, const char *dataref_name, bool *value_ref) {
    if (!value_ref) {
        printf("[XPMover] tried to expose %s with NULL reference\n", dataref_name);
        return false;
    }
    return register_int_dataref(dest, dataref_name, get_bool, value_ref, set_bool, value_ref);
}

static psx_boost_frame_t* find_previous_boost_frame_by_age(int *actual_age_millis, int reference_millis_part, int minimum_age_millis, int maximum_age_millis) {
    int index = previous_boost_frame_index;
    psx_boost_frame_t *current_frame = NULL;
    psx_boost_frame_t *previous_frame = NULL;
    int previous_frame_age_millis = 0;
    int current_frame_age_millis = 0;
    bool is_previous_frame_too_young = true;
    bool is_current_frame_too_young = true;
    bool is_current_frame_too_old = false;

    //printf("[XPMover] -------------[find frame]-------------\n"); // DEBUG
    //printf("[XPMover] num_previous_boost_frames=%d\n", num_previous_boost_frames);

    for (int i=0; i<num_previous_boost_frames; i++) {
        previous_frame = current_frame;
        previous_frame_age_millis = current_frame_age_millis;
        is_previous_frame_too_young = is_current_frame_too_young;
        //printf("[XPMover] i=%d, index=%d, reference_millis_part=%d, previous_frame=%p, previous_frame_age_millis=%d, is_previous_frame_too_young=%d\n", i, index, reference_millis_part, previous_frame, previous_frame_age_millis, is_previous_frame_too_young); // DEBUG

        current_frame = &(previous_boost_frames[index]);
        //psx_print_boost_frame(current_frame); // DEBUG
        int frame_diff_millis = reference_millis_part - current_frame->timestamp_millis_part;
        if (frame_diff_millis < 0) {
            frame_diff_millis += 1000;
        } else if (frame_diff_millis >= 1000) {
            //printf("[XPMover] find_previous_boost_frame_by_age: bad frame difference %d\n", frame_diff_millis);
            return NULL;
        }

        current_frame_age_millis += frame_diff_millis;
        is_current_frame_too_young = (current_frame_age_millis < minimum_age_millis);
        is_current_frame_too_old = (current_frame_age_millis > maximum_age_millis);
        //printf("[XPMover] current_frame_age_millis=%d, is_current_frame_too_young=%d, is_current_frame_too_old=%d\n", current_frame_age_millis, is_current_frame_too_young, is_current_frame_too_old); // DEBUG
        if (is_current_frame_too_old) {
            break;
        }

        index--;
        if (index < 0) {
            index = MAX_NUM_PREVIOUS_BOOST_FRAMES - 1;
        }
        reference_millis_part = current_frame->timestamp_millis_part;
    }

    //printf("[XPMover] result: current_frame=%p, previous_frame=%p, is_current_frame_too_young=%d, is_current_frame_too_old=%d, is_previous_frame_too_young=%d\n", current_frame, previous_frame, is_current_frame_too_young, is_current_frame_too_old, is_previous_frame_too_young); // DEBUG

    if (current_frame && !(is_current_frame_too_young || is_current_frame_too_old)) {
        // current frame matches
        //printf("[XPMover] using current frame\n"); // DEBUG
        *actual_age_millis = current_frame_age_millis;
        return current_frame;
    }

    if (!is_previous_frame_too_young) {
        // current frame did not match but previous one did
        //printf("[XPMover] using previous frame\n"); // DEBUG
        *actual_age_millis = previous_frame_age_millis;
        return previous_frame;
    }

    // no frame matched
    //printf("[XPMover] no frame matched\n"); // DEBUG
    return NULL;
}

static void clear_previous_boost_frames() {
    num_previous_boost_frames = 0;
}

static void record_boost_frame(psx_boost_frame_t *frame) {
    int index = (previous_boost_frame_index + 1) % MAX_NUM_PREVIOUS_BOOST_FRAMES;
    previous_boost_frames[index] = *frame;
    if (num_previous_boost_frames < MAX_NUM_PREVIOUS_BOOST_FRAMES) {
        num_previous_boost_frames++;
    }
    previous_boost_frame_index = index;
}

static void clear_raw_elevation_blending_fractions() {
    for (int i=0; i<MAX_NUM_RAW_ELEVATION_BLENDING_FRACTIONS; i++) {
        raw_elevation_blending_fractions[i] = NAN;
    }
    next_raw_elevation_blending_fractions_index = 0;
    num_raw_elevation_blending_fractions = 0;
}

static void record_raw_elevation_blending_fraction(double raw_elevation_blending_fraction) {
    raw_elevation_blending_fractions[next_raw_elevation_blending_fractions_index] = raw_elevation_blending_fraction;

    if (num_raw_elevation_blending_fractions < MAX_NUM_RAW_ELEVATION_BLENDING_FRACTIONS) {
        num_raw_elevation_blending_fractions++;
    }

    next_raw_elevation_blending_fractions_index = (next_raw_elevation_blending_fractions_index + 1) % MAX_NUM_RAW_ELEVATION_BLENDING_FRACTIONS;
}

static double get_average_elevation_blending_fraction() {
    double sum = 0.0;
    int count = 0;
    bool all_in_flight = true;
    for (int i=0; i<MAX_NUM_RAW_ELEVATION_BLENDING_FRACTIONS; i++) {
        double fraction = raw_elevation_blending_fractions[i];
        if (isnan(fraction)) {
            continue;
        }
        if (fraction != 0.0) {
            all_in_flight = false;
        }
        sum += fraction;
        count++;
    }

    if (all_in_flight) {
        return 0.0;
    }

    if (count == 0) {
        return NAN;
    }

    double average = sum / count;
    if (average < 0.0) {
        return 0.0;
    }

    if (average > 1.0) {
        return 1.0;
    }

    return average;
}

static float flight_loop_callback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon)
{
    // https://developer.x-plane.com/article/movingtheplane/

    psx_boost_frame_t boost_frame_copy = {0};

    if (!datarefs_initialized) {
        bool success = true;

        // first find all datarefs we need from X-Plane, we will not be able to run if we don't have those
        success &= find_dataref(&dataref_override_oxygen, "sim/operation/override/override_oxygen_system");
        success &= find_dataref(&dataref_override_pressurization, "sim/operation/override/override_pressurization");
        success &= find_dataref(&dataref_override_planepath, "sim/operation/override/override_planepath");
        success &= find_dataref(&dataref_cabin_altitude, "sim/cockpit/pressure/cabin_altitude_actual_ft");
        success &= find_dataref(&dataref_pilot_felt_altitude, "sim/cockpit2/oxygen/indicators/pilot_felt_altitude_ft");
        success &= find_dataref(&dataref_psi_hdg, "sim/flightmodel/position/psi");
        success &= find_dataref(&dataref_phi_roll, "sim/flightmodel/position/phi");
        success &= find_dataref(&dataref_theta_pitch, "sim/flightmodel/position/theta");
        success &= find_dataref(&dataref_local_x, "sim/flightmodel/position/local_x");
        success &= find_dataref(&dataref_local_y, "sim/flightmodel/position/local_y");
        success &= find_dataref(&dataref_local_z, "sim/flightmodel/position/local_z");
        success &= find_dataref(&dataref_local_vx, "sim/flightmodel/position/local_vx");
        success &= find_dataref(&dataref_local_vy, "sim/flightmodel/position/local_vy");
        success &= find_dataref(&dataref_local_vz, "sim/flightmodel/position/local_vz");
        success &= find_dataref(&dataref_ground_speed, "sim/flightmodel/position/groundspeed");
        success &= find_dataref(&dataref_ground_speed2, "sim/flightmodel2/position/groundspeed");

        if (success) {
            datarefs_initialized = true;
        } else {
            // TODO: disable plugin, it's unlikely that we could recover later
            printf("[XPMover] unable to find datarefs\n");
            return 0;
        }

        // now register datarefs we want to provide
        success &= expose_double_as_dataref(&dataref_model_height_offset, dataref_name_model_height_offset, &model_height_offset_meters);
        success &= expose_double_as_dataref(&dataref_model_length_offset, dataref_name_model_length_offset, &model_length_offset_meters);
        success &= expose_double_as_dataref(&dataref_debug_spin_hdg, dataref_name_debug_spin_hdg, &debug_spin_hdg);
        success &= expose_double_as_dataref(&dataref_terrain_elevation_meters, dataref_name_terrain_elevation_meters, &terrain_elevation_meters);
        success &= expose_int_as_dataref(&dataref_terrain_elevation_remaining_cycles, dataref_name_terrain_elevation_remaining_cycles, &terrain_elevation_remaining_cycles);
        success &= expose_int_as_dataref(&dataref_ground_contact_cycles, dataref_name_ground_contact_cycles, &ground_contact_cycles);
        success &= expose_double_as_dataref(&dataref_ground_contact_fraction, dataref_name_ground_contact_fraction, &ground_contact_fraction);
        success &= expose_double_as_dataref(&dataref_firm_ground_speed, dataref_name_firm_ground_speed, &firm_ground_speed);
        success &= expose_double_as_dataref(&dataref_lift_ground_speed, dataref_name_lift_ground_speed, &lift_ground_speed);
        success &= expose_double_as_dataref(&dataref_low_speed_fraction, dataref_name_low_speed_fraction, &low_speed_fraction);
        success &= expose_double_as_dataref(&dataref_low_speed_fraction_factor, dataref_name_low_speed_fraction_factor, &low_speed_fraction_factor);
        success &= expose_double_as_dataref(&dataref_elevation_blending_fraction, dataref_name_elevation_blending_fraction, &elevation_blending_fraction);
        success &= expose_double_as_dataref(&dataref_calculated_ground_speed, dataref_name_calculated_ground_speed, &calculated_ground_speed);
        success &= expose_bool_as_dataref(&dataref_publish_ground_speed, dataref_name_publish_ground_speed, &publish_ground_speed);
        success &= expose_bool_as_dataref(&dataref_publish_motion_vector, dataref_name_publish_motion_vector, &publish_motion_vector);
        if (!success) {
            // our own datarefs only enable external control but they are not essential to continue
            printf("[XPMover] failed to register datarefs\n");
        }
    }

    // disable flight model application; only needs to be done once per plugin initialization
    // We delay that to allow XP to potentially set internal variables once before we take over. This was done in an
    // attempt to prevent false detection of hypoxia (cockpit view instantly fades to black even when on a fixed ground
    // position, even at sea level) although that's apparently not enough to fix it.
    if (cycles_to_override_plane_path == 0) {
        printf("[XPMover] overriding planepath\n");
        XPLMSetDatavi(dataref_override_planepath, disable_planepath, 0, 1);
    }

    if (cycles_to_override_plane_path >= 0) {
        cycles_to_override_plane_path--;
    }

    // eventually expire terrain elevation
    // Terrain elevation is being queried by probing general scenery which is mentioned as being an expensive operation
    // so we only update it while on or close to ground. It may also happen that some probes hit dynamically moving
    // objects instead of actual terrain in which case we want to ignore the result. When flying between airports we
    // might have a very different terrain elevation on approach/touch-down (needing terrain elevation) as compared to
    // liftoff/departure (last update). This means we should revoke the previous elevation data eventually.
    // However, we cannot do it the moment we leave ground as the aircraft may bounce on takeoff or landing and we want
    // to still have consistent valid data in that case. So instead of setting it to NAN right away we only clear it
    // after some expiration time.
    if (terrain_elevation_remaining_cycles == 0) {
        terrain_elevation_meters = NAN;
    }

    if (terrain_elevation_remaining_cycles >= 0) {
        terrain_elevation_remaining_cycles--;
    }

    bool has_debug_override = (debug_spin_hdg != 0.0);

    if (mtx_lock(&boost_frame_mutex) != thrd_success) {
        printf("[XPMover] flight loop failed to lock boost frame mutex\n");
        return CALL_ON_NEXT_FRAME;
    }

    bool is_new = !boost_frame_applied || has_debug_override;
    if (is_new) {
        boost_frame_copy = boost_frame;
        boost_frame_applied = true;
    }

    mtx_unlock(&boost_frame_mutex);

    if (!is_new) {
        return CALL_ON_NEXT_FRAME;
    }

    if (has_debug_override) {
        double runtime_seconds = ((double) clock()) / CLOCKS_PER_SEC;
        // Setting debug_spin_hdg to a non-zero value overrides PSX heading to spin the aircraft around at a constant
        // rate. The value is the time (in seconds) needed per revolution.
        // This is useful when trying to find the model length offset: Set the dataref to spin the aircraft, then adjust
        // the offset dataref until it looks fine.
        if (debug_spin_hdg != 0.0) {
            boost_frame_copy.heading_true_degrees_hundreds = (int) roundl((fmod(runtime_seconds, debug_spin_hdg) / debug_spin_hdg) * 360.0 * 100.0);
        }
        psx_recalculate_boost_frame(&boost_frame_copy);
    }

    // TODO: check difference to previous location, if large call XPLMPlaceUserAtLocation before local positioning? (may be needed to prevent wrong hypoxia on load)

    // calculate ground speed
    int ground_speed_reference_boost_frame_age_millis = -1;
    psx_boost_frame_t *ground_speed_reference_boost_frame = find_previous_boost_frame_by_age(&ground_speed_reference_boost_frame_age_millis, boost_frame_copy.timestamp_millis_part, 800, 1500);
    if (ground_speed_reference_boost_frame) {
        double diff_distance_meters = great_circle_distance_meters(
            ground_speed_reference_boost_frame->flight_deck_latitude,
            ground_speed_reference_boost_frame->flight_deck_longitude,
            boost_frame_copy.flight_deck_latitude,
            boost_frame_copy.flight_deck_longitude
        );
        calculated_ground_speed = meters2nauticalmiles((diff_distance_meters * MILLISECONDS_PER_HOUR) / ground_speed_reference_boost_frame_age_millis);

        /*
        // DEBUG
        printf("[XPMover] -----------[CALC GROUND SPEED]------------\n");
        psx_print_boost_frame(&boost_frame_copy);
        psx_print_boost_frame(ground_speed_reference_boost_frame);
        printf("[XPMover] reference ground speed frame age: %d\n", ground_speed_reference_boost_frame_age_millis);
        printf("[XPMover] distance: %lf meters (%lf nm)\n", diff_distance_meters, meters2nauticalmiles(diff_distance_meters));
        */
    }
    record_boost_frame(&boost_frame_copy);

    if (calculated_ground_speed < 0.0) {
        calculated_ground_speed = 0.0;
    } else if (calculated_ground_speed > MAX_GROUND_SPEED) {
        calculated_ground_speed = MAX_GROUND_SPEED;
    }

    if (publish_ground_speed) {
        double ground_speed_meters = nauticalmiles2meters(calculated_ground_speed);
        XPLMSetDatad(dataref_ground_speed, ground_speed_meters);
        XPLMSetDatad(dataref_ground_speed2, ground_speed_meters);
    }

    // calculate motion vector (used by XP to e.g. calculate read-only ground speed dataref)
    if (publish_motion_vector && ground_speed_reference_boost_frame) {
        // note that this calculation does not include terrain blending, thus it is different to later local_* variables
        double current_x = 0.0;
        double current_y = 0.0;
        double current_z = 0.0;
        double reference_x = 0.0;
        double reference_y = 0.0;
        double reference_z = 0.0;
        XPLMWorldToLocal(boost_frame_copy.flight_deck_latitude, boost_frame_copy.flight_deck_longitude, boost_frame_copy.elevation_msl_meters, &current_x, &current_y, &current_z);
        XPLMWorldToLocal(ground_speed_reference_boost_frame->flight_deck_latitude, ground_speed_reference_boost_frame->flight_deck_longitude, ground_speed_reference_boost_frame->elevation_msl_meters, &reference_x, &reference_y, &reference_z);

        double reference_age_seconds = (double) ground_speed_reference_boost_frame_age_millis / MILLISECONDS_PER_SECOND;
        double inverse_reference_age_seconds = 1.0 / reference_age_seconds;
        double vector_x = (current_x - reference_x) * inverse_reference_age_seconds;
        double vector_y = (current_y - reference_y) * inverse_reference_age_seconds;
        double vector_z = (current_z - reference_z) * inverse_reference_age_seconds;

        XPLMSetDataf(dataref_local_vx, (float)vector_x);
        XPLMSetDataf(dataref_local_vy, (float)vector_y);
        XPLMSetDataf(dataref_local_vz, (float)vector_z);
    }

    // calculate low speed fraction for elevation blending
    if (calculated_ground_speed <= firm_ground_speed) {
        low_speed_fraction = 1.0;
    } else if (calculated_ground_speed >= lift_ground_speed) {
        low_speed_fraction = 0.0;
    } else if (firm_ground_speed < lift_ground_speed) {
        double ground_speed_transition_width = lift_ground_speed - firm_ground_speed;
        low_speed_fraction = 1.0 - ((calculated_ground_speed - firm_ground_speed) / ground_speed_transition_width);
        if (low_speed_fraction < 0.0) {
            low_speed_fraction = 0.0;
        } else if (low_speed_fraction > 1.0) {
            low_speed_fraction = 1.0;
        }
    }

    // calculate ground contact fraction (~time since touchdown or liftoff) for elevation blending
    if (boost_frame_copy.ground_contact) {
        ground_contact_cycles++;
    } else {
        ground_contact_cycles--;
    }
    if (ground_contact_cycles > MAX_GROUND_CONTACT_CYCLES) {
        ground_contact_cycles = MAX_GROUND_CONTACT_CYCLES;
    } else if (ground_contact_cycles < 0) {
        ground_contact_cycles = 0;
    }

    ground_contact_fraction = ((double)ground_contact_cycles) / MAX_GROUND_CONTACT_CYCLES;
    if (ground_contact_fraction < 0.0) {
        ground_contact_fraction = 0.0;
    } else if (ground_contact_fraction > 1.0) {
        ground_contact_fraction = 1.0;
    }

    // calculate fraction of elevation blending
    double raw_elevation_blending_fraction = NAN;
    if (low_speed_fraction == 0.0 && ground_contact_fraction == 0.0) {
        // fully transitioned to flight, apply unadjusted PSX altitude
        raw_elevation_blending_fraction = 0.0;
    } else if (low_speed_fraction == 1.0 && ground_contact_fraction == 1.0) {
        // fully transitioned to ground, pin to X-Plane terrain
        raw_elevation_blending_fraction = 1.0;
    } else {
        // in transition between ground and flight
        // The definite factor should be PSX-indicated ground contact. However, we already want to start transitioning
        // from XP to PSX during the takeoff roll by some degree to feel more natural, which is accomplished by adding
        // some amount of our "low speed fraction". The total factor is then clamped to the original range.
        raw_elevation_blending_fraction = ground_contact_fraction - ((1.0-low_speed_fraction) * low_speed_fraction_factor);
        if (raw_elevation_blending_fraction < 0.0) {
            raw_elevation_blending_fraction = 0.0;
        } else if (raw_elevation_blending_fraction > 1.0) {
            raw_elevation_blending_fraction = 1.0;
        }
    }

    // smooth elevation blending fraction
    record_raw_elevation_blending_fraction(raw_elevation_blending_fraction);
    elevation_blending_fraction = get_average_elevation_blending_fraction();

    // transform PSX flight deck lat/lon and aircraft center elevation to X-Plane OpenGL coordinates (meters)
    double local_x = 0.0;
    double local_y = 0.0;
    double local_z = 0.0;
    XPLMWorldToLocal(boost_frame_copy.flight_deck_latitude, boost_frame_copy.flight_deck_longitude, boost_frame_copy.elevation_msl_meters + model_height_offset_meters, &local_x, &local_y, &local_z);

    // center of rotation is offset between PSX and XP model
    // local OpenGL coordinates luckily are defined in meters, so we can correct the position by simple trigonometry
    local_x -= sin(deg2rad(boost_frame_copy.heading_true_degrees)) * model_length_offset_meters; // neg west / pos east
    local_z += cos(deg2rad(boost_frame_copy.heading_true_degrees)) * model_length_offset_meters; // neg north / pos south

    // probe terrain at current position, if needed
    bool need_xplane_elevation = (elevation_blending_fraction != 0.0);
    if (need_xplane_elevation) {
        XPLMProbeResult probe_result = XPLMProbeTerrainXYZ(probe, (float) local_x, (float) local_y, (float) local_z, probe_info);
        if (probe_result == xplm_ProbeHitTerrain) {
            // if the probed object moves it cannot be terrain but is probably some scenery object, ignore it
            if (probe_info->velocityX == 0.0 && probe_info->velocityY == 0.0 && probe_info->velocityZ == 0.0) {
                double probe_latitude = 0.0;
                double probe_longitude = 0.0;
                XPLMLocalToWorld(
                    probe_info->locationX, probe_info->locationY, probe_info->locationZ,
                    &probe_latitude, &probe_longitude, &terrain_elevation_meters
                );

                // reset expiration countdown
                terrain_elevation_remaining_cycles = TERRAIN_ELEVATION_MAX_AGE_CYCLES;
            }
        }
    }

    // adjust elevation as needed for terrain blending/pinning
    if ((elevation_blending_fraction > 0.0) && !isnan(terrain_elevation_meters)) {
        double adjusted_elevation_meters;
        if (elevation_blending_fraction == 1.0) {
            // pin to ground
            adjusted_elevation_meters = terrain_elevation_meters;
        } else {
            // blend between XP and PSX
            double elevation_difference = boost_frame_copy.elevation_msl_meters - terrain_elevation_meters;
            adjusted_elevation_meters = boost_frame_copy.elevation_msl_meters - (elevation_blending_fraction * elevation_difference);
        }

        // apply model offset
        adjusted_elevation_meters += model_height_offset_meters;

        // recalculate local_y (elevation); keep local_x/local_z (lateral position)
        double _ignore_local_x = 0.0;
        double _ignore_local_z = 0.0;
        XPLMWorldToLocal(boost_frame_copy.flight_deck_latitude, boost_frame_copy.flight_deck_longitude, adjusted_elevation_meters, &_ignore_local_x, &local_y, &_ignore_local_z);
    }

    XPLMSetDatai(dataref_override_oxygen, 1);
    XPLMSetDatai(dataref_override_pressurization, 1);
    XPLMSetDataf(dataref_cabin_altitude, 0.0f);
    XPLMSetDataf(dataref_pilot_felt_altitude, 0.0f);

    XPLMSetDataf(dataref_psi_hdg, boost_frame_copy.heading_true_degrees);
    XPLMSetDataf(dataref_phi_roll, boost_frame_copy.bank_degrees);
    XPLMSetDataf(dataref_theta_pitch, boost_frame_copy.pitch_degrees);

    XPLMSetDatad(dataref_local_x, local_x);
    XPLMSetDatad(dataref_local_y, local_y);
    XPLMSetDatad(dataref_local_z, local_z);

    return CALL_ON_NEXT_FRAME;
}

PLUGIN_API int XPluginStart(char *name, char *sig, char *desc) {
    printf("[XPMover] start\n");

    strcpy(name, "PSX XP Mover");
    strcpy(sig, "de.energiequant.psx.xpmover");
    strcpy(desc, "Moves the aircraft according to PSX");

    return 1;
}

PLUGIN_API int XPluginEnable() {
    printf("[XPMover] enable\n");

    XPLMCreateFlightLoop_t params = {
        .structSize = sizeof(XPLMCreateFlightLoop_t),
        .phase = xplm_FlightLoop_Phase_AfterFlightModel,
        .callbackFunc = flight_loop_callback,
        .refcon = NULL,
    };

    if (datarefs_initialized || psx_client || probe || probe_info) {
        printf("[XPMover] internal variables are already set, another instance appears to still be running; aborting startup\n");
        return 0;
    }

    // prevent dummy/outdated data being applied before we receive the first PSX frame
    boost_frame_applied = true;

    if (mtx_init(&boost_frame_mutex, mtx_plain) != thrd_success) {
        printf("[XPMover] failed to initialize boost frame mutex; aborting startup\n");
        return 0;
    }
    has_boost_frame_mutex = true;

    psx_client = create_psx_client("localhost", 10749, on_boost_frame_received);
    if (!psx_client) {
        printf("[XPMover] failed to create PSX client; aborting startup\n");
        goto rollback;
    }

    probe_info = zmalloc(sizeof(XPLMProbeInfo_t));
    if (!probe_info) {
        printf("[XPMover] failed to allocate probe info; aborting startup\n");
        goto rollback;
    }
    probe_info->structSize = sizeof(XPLMProbeInfo_t);

    probe = XPLMCreateProbe(xplm_ProbeY);
    if (!probe) {
        printf("[XPMover] failed to create probe; aborting startup\n");
        goto rollback;
    }

    cycles_to_override_plane_path = INIT_CYCLES_TO_OVERRIDE_PLANE_PATH;
    clear_previous_boost_frames(); // TODO: also clear on PSX reconnect
    clear_raw_elevation_blending_fractions(); // TODO: also clear on PSX reconnect

    flight_loop_after_flight_model_id = XPLMCreateFlightLoop(&params);

    XPLMScheduleFlightLoop(flight_loop_after_flight_model_id, CALL_ON_NEXT_FRAME, 1);
    flight_loop_registered = true;

    return 1;

rollback:
    XPluginDisable();
    return 0;
}

PLUGIN_API void XPluginDisable() {
    printf("[XPMover] disable\n");

    if (flight_loop_registered) {
        XPLMDestroyFlightLoop(flight_loop_after_flight_model_id);
        flight_loop_registered = false;
    }

    datarefs_initialized = false;

    if (psx_client) {
        if (destroy_psx_client(psx_client)) {
            psx_client = NULL;
        } else {
            printf("[XPMover] PSX client could not be destroyed, failed to terminate properly\n");
        }
    }

    unregister_dataref(&dataref_model_height_offset);
    unregister_dataref(&dataref_model_length_offset);
    unregister_dataref(&dataref_debug_spin_hdg);
    unregister_dataref(&dataref_terrain_elevation_meters);
    unregister_dataref(&dataref_terrain_elevation_remaining_cycles);
    unregister_dataref(&dataref_ground_contact_cycles);
    unregister_dataref(&dataref_ground_contact_fraction);
    unregister_dataref(&dataref_firm_ground_speed);
    unregister_dataref(&dataref_lift_ground_speed);
    unregister_dataref(&dataref_low_speed_fraction);
    unregister_dataref(&dataref_low_speed_fraction_factor);
    unregister_dataref(&dataref_elevation_blending_fraction);
    unregister_dataref(&dataref_calculated_ground_speed);
    unregister_dataref(&dataref_publish_ground_speed);

    if (probe) {
        XPLMDestroyProbe(probe);
        probe = NULL;
    }

    if (probe_info) {
        free(probe_info);
        probe_info = NULL;
    }

    if (has_boost_frame_mutex) {
        mtx_destroy(&boost_frame_mutex);
        has_boost_frame_mutex = false;
    }
}

PLUGIN_API void XPluginStop() {
    printf("[XPMover] stop\n");
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from, long msg, void *p) {
    // do nothing
}
