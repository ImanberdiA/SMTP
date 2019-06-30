#include <masprintf.h>

#include <die.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

char *vmasprintf(char const* format, va_list args) {
    va_list args2;
    va_copy(args2, args);
    int size = vsnprintf(NULL, 0, format, args2);
    if (size < 0) {
        die("`vsnprintf(NULL, 0, \"%s\", /*...*/)` failed\n", format);
    }
    va_end(args2);

    char *result = malloc(size + 1);
    if (!result) {
        die("`malloc(%d)` failed: %s\n", size + 1, strerror(errno));
    }

    if (vsprintf(result, format, args) != size) {
        die("`vsprintf(NULL, \"%s\", /*...*/)` failed\n", format);
    }

    return result;
}

char *masprintf(char const* format, ...) {
    va_list args;
    va_start(args, format);
    char *result = vmasprintf(format, args);
    va_end(args);
    return result;
}

/*! \file */
