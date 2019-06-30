#include <die.h>

#include <stdio.h>
#include <stdlib.h>

void vdie(char const* format, va_list args) {
    vfprintf(stderr, format, args);
    exit(EXIT_FAILURE);
}

void die(char const* format, ...) {
    va_list args;
    va_start(args, format);
    vdie(format, args);
}

/*! \file */
