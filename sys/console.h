// console.h - Console I/O
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _CONSOLE_H_
#define _CONSOLE_H_

#include <stddef.h>
#include <stdarg.h>


extern char console_initialized;
extern void console_init(void);

extern void kputc(char c);

extern char kgetc(void);

extern void kputs(const char * str);

extern char * kgetsn(char * buf, size_t n);

extern void kprintf(const char * fmt, ...);

extern void kvprintf(const char * fmt, va_list ap);

extern void kprintfluffy(int cols, const char ** artptr, const char * fmt, ...);

#endif // _CONSOLE_H_
