#ifndef XPMOVER_UTILS_H
#define XPMOVER_UTILS_H

#include <stdbool.h>
#include <stdio.h>

void* zmalloc(size_t size);

bool parse_long(long *dest, char *s, int length);
bool parse_int(int *dest, char *s, int length);
bool parse_double(double *dest, char *s, int length);

char* copy_string(char *s);

double deg2rad(double degrees);
double rad2deg(double radians);

double nauticalmiles2meters(double nautical_miles);
double meters2nauticalmiles(double meters);

double feet2meters(double feet);
double meters2feet(double meters);

/**
 * Calculates the distance between both coordinates using the
 * Haversine method. This method provides an accuracy of 0.5% which is okay
 * for small distances only. If unsure, use Vincenty method or let
 * great_circle_distance_meters decide which method to use.
 *
 * @param latitude1  first coordinate latitude
 * @param longitude1 first coordinate longitude
 * @param latitude2  second coordinate latitude
 * @param longitude2 second coordinate longitude
 * @return distance in meters with an accuracy of 0.5%
 * @see https://en.wikipedia.org/wiki/Great-circle_distance
 */
double great_circle_distance_meters_haversine(double latitude1, double longitude1, double latitude2, double longitude2);

double great_circle_distance_meters(double latitude1, double longitude1, double latitude2, double longitude2);

#endif
