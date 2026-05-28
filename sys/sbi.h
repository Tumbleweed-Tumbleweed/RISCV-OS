// sbi.h - Supervisor binary interface
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _SBI_H_
#define _SBI_H_

extern long sbi_set_timer(unsigned long long stime_value);

extern long sbi_console_putchar(int ch);

extern long sbi_console_getchar(void);

extern void sbi_shutdown(void) __attribute__ ((noreturn));

#endif // _SBI_H_
