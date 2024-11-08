#include <stdio.h>

#include "psx.h"

int main(int argc, char **argv) {
    psx_boost_frame_t parsed = {0};

    if (argc != 2) {
        printf("call with boost line to parse\n");
        return 1;
    }

    char *boost_line = argv[1];
    printf("Parsing: %s\n", boost_line);
    printf("\n");

    bool success = psx_parse_boost_frame(&parsed, boost_line);
    success &= psx_recalculate_boost_frame(&parsed);

    psx_print_boost_frame(&parsed);
    printf("\n");

    if (!success) {
        printf("\n!!! line could not be fully parsed !!!\n");
        return 1;
    }

    return 0;
}
