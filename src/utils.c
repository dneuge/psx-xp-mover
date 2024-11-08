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
