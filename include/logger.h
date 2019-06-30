#ifndef LOGGER_H
#define LOGGER_H

#include <stdarg.h>

void logger_initialize();

void logger_vprintf(char const* format, va_list args);

#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
void logger_printf(char const* format, ...);

void logger_finalize();

#endif


/*! \file */
