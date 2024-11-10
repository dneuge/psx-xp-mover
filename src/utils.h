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

#endif
