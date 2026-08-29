#include "utils.h"

#ifdef TARGET_WINDOWS
// needs to be set to make math constant defines available on MSVC
// https://github.com/MicrosoftDocs/cpp-docs/blob/f927e4f8273047750b384b41788a005a1a31fa62/docs/c-runtime-library/math-constants.md
#define _USE_MATH_DEFINES
#endif

#ifdef _MSC_VER
// MSVC linker fails to find inline methods, disable modifier
#define MAY_INLINE
#else
#define MAY_INLINE inline
#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NUMBER_PARSING_BUFFER_SIZE (256)

void* zmalloc(size_t size) {
    void *out = malloc(size);
    if (!out) {
        return NULL;
    }

    memset(out, 0, size);

    return out;
}

static inline bool apply_string_to_buffer(char *in, size_t in_length, char *out, size_t out_size) {
    if (in_length >= out_size) {
        return false;
    }

    memcpy(out, in, in_length);
    out[in_length] = 0;

    return true;
}

bool parse_long(long *dest, char *s, int length) {
    char buffer[NUMBER_PARSING_BUFFER_SIZE] = {0,};
    if (!apply_string_to_buffer(s, length, buffer, NUMBER_PARSING_BUFFER_SIZE)) {
        return false;
    }

    long parsed = atol(buffer);

    // turn back into a string to check if it's really parsed correctly
    int back_length = snprintf(NULL, 0, "%ld", parsed);
    if ((back_length <= 0) || (back_length != length)) {
        return false;
    }

    char *back = zmalloc(back_length + 1);
    if (!back) {
        return false;
    }

    bool success = false;

    int actual_back_length = snprintf(back, back_length + 1, "%ld", parsed);
    if (actual_back_length == back_length) {
        success = !strncmp(s, back, length);
    }

    free(back);

    if (success) {
        *dest = parsed;
    }

    return success;
}

bool parse_int(int *dest, char *s, int length) {
    char buffer[NUMBER_PARSING_BUFFER_SIZE] = {0,};
    if (!apply_string_to_buffer(s, length, buffer, NUMBER_PARSING_BUFFER_SIZE)) {
        return false;
    }

    int parsed = atoi(buffer);

    // turn back into a string to check if it's really parsed correctly
    int back_length = snprintf(NULL, 0, "%d", parsed);
    if ((back_length <= 0) || (back_length != length)) {
        return false;
    }

    char *back = zmalloc(back_length + 1);
    if (!back) {
        return false;
    }

    bool success = false;

    int actual_back_length = snprintf(back, back_length + 1, "%d", parsed);
    if (actual_back_length == back_length) {
        success = !strncmp(s, back, length);
    }

    free(back);

    if (success) {
        *dest = parsed;
    }

    return success;
}

bool parse_double(double *dest, char *s, int length) {
    char buffer[NUMBER_PARSING_BUFFER_SIZE] = {0,};
    if (!apply_string_to_buffer(s, length, buffer, NUMBER_PARSING_BUFFER_SIZE)) {
        return false;
    }

    *dest = atof(buffer);
    // TODO: how to verify?
    return true;
}

char* copy_string(char *s) {
    if (!s) {
        return NULL;
    }

    size_t length = strlen(s);

    char *copy = malloc(length + 1);
    if (!copy) {
        return NULL;
    }

    if (length > 0) {
        memcpy(copy, s, length);
    }
    copy[length] = 0;

    return copy;
}

#define FACTOR_DEGREES_PER_RADIAN (180.0 / M_PI)
#define FACTOR_RADIANS_PER_DEGREE (M_PI / 180.0)

MAY_INLINE double deg2rad(double degrees) {
    return degrees * FACTOR_RADIANS_PER_DEGREE;
}

MAY_INLINE double rad2deg(double radians) {
    return radians * FACTOR_DEGREES_PER_RADIAN;
}

MAY_INLINE double nauticalmiles2meters(double nautical_miles) {
    return nautical_miles * METERS_PER_NAUTICAL_MILE;
}

MAY_INLINE double meters2nauticalmiles(double meters) {
    return meters * NAUTICAL_MILES_PER_METER;
}

#define METERS_PER_FOOT (0.3048)
#define FEET_PER_METER (1.0 / METERS_PER_FOOT)

MAY_INLINE double feet2meters(double feet) {
    return feet * METERS_PER_FOOT;
}

MAY_INLINE double meters2feet(double meters) {
    return meters * FEET_PER_METER;
}

#define MEAN_RADIUS_EARTH (6371009)

/**
 * Calculates the distance between both coordinates using the
 * Haversine method. This method provides an accuracy of 0.5% which is okay
 * for small distances only. If unsure, use Vincenty method or let
 * greatCircleDistanceInMeters decide which method to use.
 *
 * @param latitude1  first coordinate latitude
 * @param longitude1 first coordinate longitude
 * @param latitude2  second coordinate latitude
 * @param longitude2 second coordinate longitude
 * @return distance in meters with an accuracy of 0.5%
 * @see https://en.wikipedia.org/wiki/Great-circle_distance
 */
double great_circle_distance_meters_haversine(const double latitude1, const double longitude1, const double latitude2, const double longitude2) {
    // convert coordinates given in degrees to radians needed for calculation
    double latitude1Radians = deg2rad(latitude1);
    double longitude1Radians = deg2rad(longitude1);
    double latitude2Radians = deg2rad(latitude2);
    double longitude2Radians = deg2rad(longitude2);

    double deltaLatitude = fabs(latitude1Radians - latitude2Radians); // delta phi
    double deltaLongitude = fabs(longitude1Radians - longitude2Radians); // delta lambda

    double singleSineHalfDeltaLatitude = sin(deltaLatitude / 2.0);
    double haversineDeltaLatitude = singleSineHalfDeltaLatitude * singleSineHalfDeltaLatitude;

    double singleSineHalfDeltaLongitude = sin(deltaLongitude / 2.0);
    double haversineDeltaLongitude = singleSineHalfDeltaLongitude * singleSineHalfDeltaLongitude;

    double centralAngle = 2.0 * asin(sqrt(haversineDeltaLatitude + cos(latitude1Radians) * cos(latitude2Radians) * haversineDeltaLongitude));
    double distanceMeters = MEAN_RADIUS_EARTH * centralAngle;

    return distanceMeters;
}

double great_circle_distance_meters(double latitude1, double longitude1, double latitude2, double longitude2) {
    return great_circle_distance_meters_haversine(latitude1, longitude1, latitude2, longitude2);
}
