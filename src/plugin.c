#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <XPLMDataAccess.h>
#include <XPLMGraphics.h>
#include <XPLMProcessing.h>

#include "psx.h"
#include "utils.h"

#define CALL_ON_NEXT_FRAME (-1.0f)

static XPLMFlightLoopID flight_loop_after_flight_model_id = {0};
static bool flight_loop_registered = false;

// NOTE: offset corrections are more a rough guess from experimentation than exact measurements
// TODO: make correction factors changeable at runtime through datarefs, experiment to improve further
#define MODEL_HEIGHT_OFFSET_METERS (3.25)   /* visually: roughly the height from ground to lower 1/3 of outer engines */
#define MODEL_LENGTH_OFFSET_METERS (28.194) /* observable by changing HDG in PSX on a parking position, aircraft rotates
                                             at tail; visually: a bit less than half the fuselage offset from main gear;
                                             mainly caused by offset of PSX flight deck from gear but not exactly the
                                             documented length */

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

static float flight_loop_callback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon)
{
    // https://developer.x-plane.com/article/movingtheplane/

    psx_boost_frame_t boost_frame_copy = {0};

    // TODO: do only once every x frames
    XPLMSetDatavi(dataref_override_planepath, disable_planepath, 0, 1);

    if (!datarefs_initialized) {
        bool success = true;
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
    }

    if (mtx_lock(&boost_frame_mutex) != thrd_success) {
        printf("[XPMover] flight loop failed to lock boost frame mutex");
        return CALL_ON_NEXT_FRAME;
    }

    bool is_new = !boost_frame_applied;
    if (is_new) {
        boost_frame_copy = boost_frame;
        boost_frame_applied = true;
    }

    mtx_unlock(&boost_frame_mutex);

    if (!is_new) {
        return CALL_ON_NEXT_FRAME;
    }

    // TODO: check difference to previous location, if large call XPLMPlaceUserAtLocation before local positioning?

    double local_x = 0.0;
    double local_y = 0.0;
    double local_z = 0.0;
    XPLMWorldToLocal(boost_frame_copy.flight_deck_latitude, boost_frame_copy.flight_deck_longitude, boost_frame_copy.elevation_msl_meters + MODEL_HEIGHT_OFFSET_METERS, &local_x, &local_y, &local_z);

    // center of rotation is offset between PSX and XP model
    // local OpenGL coordinates luckily are defined in meters, so we can correct the position by simple trigonometry
    local_x -= sin(deg2rad(boost_frame_copy.track_degrees)) * MODEL_LENGTH_OFFSET_METERS; // neg west / pos east
    local_z += cos(deg2rad(boost_frame_copy.track_degrees)) * MODEL_LENGTH_OFFSET_METERS; // neg north / pos south

    XPLMSetDataf(dataref_psi_hdg, boost_frame_copy.track_degrees);
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
}

PLUGIN_API void XPluginStop() {
    printf("[XPMover] stop\n");
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from, long msg, void *p) {
    // do nothing
}
