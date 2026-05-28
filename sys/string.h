// string.h - String and memory functions
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _STRING_H_
#define _STRING_H_

#include <stdarg.h>
#include <stddef.h>

// With the exception of vgprintf(), the following functions (try to) adhere to
// the C standard. See https://man7.org/linux/man-pages/man0/string.h.0p.html
//

extern size_t strlen(const char * s);
extern int strcmp(const char * s1, const char * s2);
extern int strncmp(const char * s1, const char * s2, size_t n);
extern char * strncpy(char * dst, const char * src, size_t n);
extern char * strlcpy(char * dst, const char * src, size_t n);
extern char * strchr(const char * s, int c);
extern char * strrchr(const char * s, int c);
extern void * memset(void * s, int c, size_t n);
extern int memcmp(const void * p1, const void * p2, size_t n);
extern void * memcpy(void * restrict dst, const void * restrict src, size_t n);
extern unsigned long strtoul(const char * str, char ** endptr, int base);
extern size_t snprintf(char * buf, size_t bufsz, const char * fmt, ...);
extern size_t vsnprintf(char * buf, size_t bufsz, const char * fmt, va_list ap);

extern size_t vgprintf (
    void (*putcfn)(char, void*), void * aux, const char * fmt, va_list ap);

#endif  // _STRING_H_
