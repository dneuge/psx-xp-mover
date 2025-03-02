#include "utils.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void* zmalloc(size_t size) {
    void *out = malloc(size);
    if (!out) {
        return NULL;
    }

    memset(out, 0, size);

    return out;
}

bool parse_long(long *dest, char *s, int length) {
    long parsed = atol(s);

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
    int parsed = atoi(s);

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
    *dest = atof(s);
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

inline double deg2rad(double degrees) {
    return degrees * FACTOR_RADIANS_PER_DEGREE;
}

inline double rad2deg(double radians) {
    return radians * FACTOR_DEGREES_PER_RADIAN;
}

inline double nauticalmiles2meters(double nautical_miles) {
    return nautical_miles * METERS_PER_NAUTICAL_MILE;
}

inline double meters2nauticalmiles(double meters) {
    return meters * NAUTICAL_MILES_PER_METER;
}

#define METERS_PER_FOOT (0.3048)
#define FEET_PER_METER (1.0 / METERS_PER_FOOT)

inline double feet2meters(double feet) {
    return feet * METERS_PER_FOOT;
}

inline double meters2feet(double meters) {
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
