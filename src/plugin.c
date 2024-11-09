#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <XPLMDataAccess.h>
#include <XPLMGraphics.h>
#include <XPLMProcessing.h>
#include <XPLMPlugin.h>

#include "psx.h"
#include "utils.h"

#define CALL_ON_NEXT_FRAME (-1.0f)

static XPLMFlightLoopID flight_loop_after_flight_model_id = {0};
static bool flight_loop_registered = false;

// NOTE: offset corrections are more a rough guess from experimentation than exact measurements
static double model_length_offset_meters = 28.194; // visually: roughly the height from ground to lower 1/3 of outer engines
                                                   // 28.194m is the documented offset from "flight deck" to aircraft center,
                                                   // note that the PSX boost server will send lat/lon for the "flight deck"
                                                   // view point, not the aircraft center
static double model_height_offset_meters = 3.25;   /* observable by changing HDG in PSX on a parking position, aircraft rotates
                                                      at tail; visually: a bit less than half the fuselage offset from main gear;
                                                      mainly caused by offset of PSX flight deck from gear but not exactly the
                                                      documented length */
static double debug_spin_hdg = 0.0;

const char dataref_name_model_length_offset[] = "xpmover/model_offset/length";
static XPLMDataRef dataref_model_length_offset = NULL;

const char dataref_name_model_height_offset[] = "xpmover/model_offset/height";
static XPLMDataRef dataref_model_height_offset = NULL;

const char dataref_name_debug_spin_hdg[] = "xpmover/debug/spin_hdg";
static XPLMDataRef dataref_debug_spin_hdg = NULL;

#define DATAREF_WRITABLE (1)

typedef int xpint_t;
typedef float xpfloat_t;
typedef double xpdouble_t;

static XPLMDataRef dataref_override_planepath = NULL;
static XPLMDataRef dataref_psi_hdg = NULL;
static XPLMDataRef dataref_phi_roll = NULL;
static XPLMDataRef dataref_theta_pitch = NULL;
static XPLMDataRef dataref_local_x = NULL;
static XPLMDataRef dataref_local_y = NULL;
static XPLMDataRef dataref_local_z = NULL;
static bool datarefs_initialized = false;

static xpint_t disable_planepath[] = {1};

static psx_client_t *psx_client = NULL;

static psx_boost_frame_t boost_frame = {0};
static bool boost_frame_applied = false;
static mtx_t boost_frame_mutex;

#define INIT_CYCLES_TO_OVERRIDE_PLANE_PATH (10);
static int cycles_to_override_plane_path = INIT_CYCLES_TO_OVERRIDE_PLANE_PATH;

#define DATAREF_EDITOR_PLUGIN_NAME "xplanesdk.examples.DataRefEditor"
#define DATAREF_EDITOR_MSG_ADD_DATAREF (0x01000000)
static XPLMPluginID dataref_editor_plugin_id = XPLM_NO_PLUGIN_ID;

static void on_boost_frame_received(psx_boost_frame_t *new_boost_frame) {
    if (mtx_lock(&boost_frame_mutex) != thrd_success) {
        printf("[XPMover] receive callback failed to lock boost frame mutex");
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

static double get_model_offset_height(void *inRefcon) {
    return model_height_offset_meters;
}

static void set_model_offset_height(void *inRefcon, double inValue) {
    model_height_offset_meters = inValue;
}

static double get_model_offset_length(void *inRefcon) {
    return model_length_offset_meters;
}

static void set_model_offset_length(void *inRefcon, double inValue) {
    model_length_offset_meters = inValue;
}

static double get_debug_spin_hdg(void *inRefcon) {
    return debug_spin_hdg;
}

static void set_debug_spin_hdg(void *inRefcon, double inValue) {
    debug_spin_hdg = inValue;
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

static bool register_double_dataref(XPLMDataRef *dest, const char *inDataName, XPLMGetDatad_f inReadDouble, XPLMSetDatad_f inWriteDouble) {
    *dest = XPLMRegisterDataAccessor(
        inDataName, xplmType_Double, DATAREF_WRITABLE,
        NULL, NULL,
        NULL, NULL,
        inReadDouble, inWriteDouble,
        NULL, NULL,
        NULL, NULL,
        NULL, NULL,
        NULL,
        NULL
    );

    announce_dataref(inDataName);

    return (*dest != NULL);
}

static void unregister_dataref(XPLMDataRef *dataref) {
    if (!dataref) {
        return;
    }

    XPLMUnregisterDataAccessor(*dataref);
    *dataref = NULL;
}

static float flight_loop_callback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon)
{
    // https://developer.x-plane.com/article/movingtheplane/

    psx_boost_frame_t boost_frame_copy = {0};

    if (!datarefs_initialized) {
        bool success = true;

        // first find all datarefs we need from X-Plane, we will not be able to run if we don't have those
        success &= find_dataref(&dataref_override_planepath, "sim/operation/override/override_planepath");
        success &= find_dataref(&dataref_psi_hdg, "sim/flightmodel/position/psi");
        success &= find_dataref(&dataref_phi_roll, "sim/flightmodel/position/phi");
        success &= find_dataref(&dataref_theta_pitch, "sim/flightmodel/position/theta");
        success &= find_dataref(&dataref_local_x, "sim/flightmodel/position/local_x");
        success &= find_dataref(&dataref_local_y, "sim/flightmodel/position/local_y");
        success &= find_dataref(&dataref_local_z, "sim/flightmodel/position/local_z");

        if (success) {
            datarefs_initialized = true;
        } else {
            // TODO: disable plugin, it's unlikely that we could recover later
            printf("[XPMover] unable to find datarefs\n");
            return 0;
        }

        // now register datarefs we want to provide
        success &= register_double_dataref(
            &dataref_model_height_offset,
            dataref_name_model_height_offset,
            get_model_offset_height,
            set_model_offset_height
        );
        success &= register_double_dataref(
            &dataref_model_length_offset,
            dataref_name_model_length_offset,
            get_model_offset_length,
            set_model_offset_length
        );
        success &= register_double_dataref(
            &dataref_debug_spin_hdg,
            dataref_name_debug_spin_hdg,
            get_debug_spin_hdg,
            set_debug_spin_hdg
        );
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

    bool has_debug_override = (debug_spin_hdg != 0.0);

    if (mtx_lock(&boost_frame_mutex) != thrd_success) {
        printf("[XPMover] flight loop failed to lock boost frame mutex");
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

    double local_x = 0.0;
    double local_y = 0.0;
    double local_z = 0.0;
    XPLMWorldToLocal(boost_frame_copy.flight_deck_latitude, boost_frame_copy.flight_deck_longitude, boost_frame_copy.elevation_msl_meters + model_height_offset_meters, &local_x, &local_y, &local_z);

    // center of rotation is offset between PSX and XP model
    // local OpenGL coordinates luckily are defined in meters, so we can correct the position by simple trigonometry
    local_x -= sin(deg2rad(boost_frame_copy.heading_true_degrees)) * model_length_offset_meters; // neg west / pos east
    local_z += cos(deg2rad(boost_frame_copy.heading_true_degrees)) * model_length_offset_meters; // neg north / pos south

    XPLMSetDataf(dataref_psi_hdg, boost_frame_copy.heading_true_degrees);
    XPLMSetDataf(dataref_phi_roll, boost_frame_copy.bank_degrees);
    XPLMSetDataf(dataref_theta_pitch, boost_frame_copy.pitch_degrees);

    XPLMSetDatad(dataref_local_x, local_x);
    XPLMSetDatad(dataref_local_y, local_y); // TODO: pin to ground when PSX indicates ground contact and speed is low (see XPLMScenery on probes)
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

    if (datarefs_initialized || psx_client) {
        printf("[XPMover] internal variables are already set, another instance appears to still be running; aborting startup\n");
        return 0;
    }

    psx_client = create_psx_client("localhost", 10749, on_boost_frame_received);
    if (!psx_client) {
        printf("[XPMover] failed to create PSX client; aborting startup\n");
        return 0;
    }

    cycles_to_override_plane_path = INIT_CYCLES_TO_OVERRIDE_PLANE_PATH;

    flight_loop_after_flight_model_id = XPLMCreateFlightLoop(&params);

    XPLMScheduleFlightLoop(flight_loop_after_flight_model_id, CALL_ON_NEXT_FRAME, 1);
    flight_loop_registered = true;

    return 1;
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
}

PLUGIN_API void XPluginStop() {
    printf("[XPMover] stop\n");
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from, long msg, void *p) {
    // do nothing
}
