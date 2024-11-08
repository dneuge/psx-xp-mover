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

#endif
