#ifndef DIE_H
#define DIE_H

#include <stdarg.h>

#if __STDC_VERSION__ >= 201112L
    _Noreturn
#elif defined(__GNUC__)
    __attribute__((noreturn))
#endif
void vdie(char const* format, va_list args);

#if __STDC_VERSION__ >= 201112L
    _Noreturn
#elif defined(__GNUC__)
    __attribute__((noreturn))
#endif
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
void die(char const* format, ...);

#endif

/*! \file */
