#ifndef MASPRINTF_H
#define MASPRINTF_H

#include <stdarg.h>

char *vmasprintf(char const* format, va_list args);

#ifdef __GNUC__
    __attribute__((format(printf, 1, 2)))
#endif
char *masprintf(char const* format, ...);

#endif


/*! \file */
