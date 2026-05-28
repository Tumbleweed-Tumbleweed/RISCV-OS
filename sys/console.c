// console.c - Console I/O
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "console.h"
#include "intr.h"
#include "misc.h"
#include "sbi.h"

#include <stdarg.h>
#include <stdint.h>

#include "string.h"

// ---INTERNAL FUNCTION DECLARATIONS--- //

static void vprintf_putc(char c, void * aux);

// The following functions are called to initialize the console device (if any)
// and perform putchar/getchar I/O. They are declared weak so that they may be
// overridden elsewhere.

extern void console_impl_init(void) __attribute__ ((weak));
extern void console_impl_putc(char c) __attribute__ ((weak));
extern char console_impl_getc(void) __attribute__ ((weak));

// ---EXPORTED GLOBAL VARIABLES--- //
char console_initialized = 0;

// ---EXPORTED FUNCTION DEFINITIONS--- //

void console_init(void) {
    console_impl_init();
    console_initialized = 1;
}

void kputc(char c) {
    static char cprev = '\0';

    switch (c) {
    case '\r':
        console_impl_putc(c);
        console_impl_putc('\n');
        break;
    case '\n':
        if (cprev != '\r')
            console_impl_putc('\r');
        // nobreak
    default:
        console_impl_putc(c);
        break;
    }

    cprev = c;
}

char kgetc(void) {
    static char cprev = '\0';
    char c;

    // Convert \r followed by any number of \n to just \n

    do {
        c = console_impl_getc();
    } while (c == '\n' && cprev == '\r');
  
    cprev = c;

    if (c == '\r')
        return '\n';
    else
        return c;
}

void kputs(const char * str) {
    int sie;

    sie = disable_interrupts();

    while (*str != '\0')
        kputc(*str++);
    kputc('\n');

    restore_interrupts(sie);
}

char * kgetsn(char * buf, size_t n) {
	char * p = buf;
	char c;

	for (;;) {
		c = kgetc();

		switch (c) {
		case '\r':
			break;		
		case '\n':
			kputc('\n');
			*p = '\0';
			return buf;
		case '\b':
		case '\177':
			if (p != buf) {
				kputc('\b');
				kputc(' ');
				kputc('\b');

				p -= 1;
				n += 1;
			}
			break;
		default:
			if (n > 1) {
				kputc(c);
				*p++ = c;
				n -= 1;
			} else
				kputc('\a'); // bell
			break;
		}
	}
}

void kprintf(const char * fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}

void kvprintf(const char * fmt, va_list ap) {
    int sie;

    sie = disable_interrupts();
    vgprintf(vprintf_putc, NULL, fmt, ap);
    restore_interrupts(sie);
}

void kprintfluffy(int cols, const char ** artptr, const char * fmt, ...) {
    char linebuf[128];
    const char * arts;
    size_t artlen;
    va_list ap;
    int n;

    if (cols < 0)
        return;
    
    if (cols > sizeof(linebuf)-1)
        cols = sizeof(linebuf)-1;
    
    if (cols > 0) {
        va_start(ap, fmt);          // we changed arptr to fmt as it was throwing an error but this could be wrong @TODO
        n = vsnprintf(linebuf, cols, fmt, ap);
        va_end(ap);

        if (n < cols)
            memset(linebuf+n, ' ', cols-n);
        linebuf[cols] = '\0';
    } else
        linebuf[0] = '\0';
    
    
    arts = (artptr != NULL && *artptr != NULL) ? *artptr : "";
    artlen = strlen(arts);

    kprintf("%s %s\n", linebuf, arts);

    if (artlen != 0)
        *artptr = arts + artlen+1;
}

// ---INTERNAL FUNCTION DEFINITIONS--- //

void vprintf_putc(char c, void * __attribute__ ((unused)) aux) {
    kputc(c);
}

// ---DEFAULT CONSOLE FUNCTION DEFINITIONS--- //

// The default implementation of console getchar and putchar use SBI. The
// functions above may be re-defined elsewhere to provide alternatives.

void console_impl_init(void) {
    // nothing
}

void console_impl_putc(char c) {
    sbi_console_putchar(c);
}

char console_impl_getc(void) {
    long retval = -1;
    while (retval == -1) {
        retval = sbi_console_getchar();
    }
    return retval;
}