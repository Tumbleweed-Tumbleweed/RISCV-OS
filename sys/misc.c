// misc.c - Miscellaneous functions
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "misc.h"

#include <stdarg.h>
#include <stddef.h>

#include "console.h"
#include "sbi.h" // for sbi_shutdown()
#include "intr.h"
#include "thread.h"

// EXPORTED FUNCTION DEFINITIONS
//

void panic_actual(const char* filename, int lineno, const char* msg) {
    if (thrmgr_initialized)
        kprintf("<%s:%d> ", running_thread_name(), running_thread());
    if (msg != NULL && *msg != '\0')
        kprintf("PANIC at %s:%d: %s\n", filename, lineno, msg);
    else
        kprintf("PANIC at %s:%d\n", filename, lineno);

    halt();
}

void assert_failed(const char* filename, int lineno, const char* stmt) {
    if (thrmgr_initialized)
        kprintf("<%s:%d> ", running_thread_name(), running_thread());
    kprintf("ASSERT FAILED at %s:%d (%s)\n", filename, lineno, stmt);
    halt();
}

void debug_actual(const char* filename, int lineno, const char* fmt, ...) {
    va_list ap;
    int pie;

    va_start(ap, fmt);
    pie = disable_interrupts();

    if (thrmgr_initialized)
        kprintf("<%s:%d> ", running_thread_name(), running_thread());
    kprintf("DEBUG at %s:%d: ", filename, lineno);
    kvprintf(fmt, ap);
    kprintf("\n");

    restore_interrupts(pie);
    va_end(ap);
}

void trace_actual(const char* filename, int lineno, const char* fmt, ...) {
    va_list ap;
    int pie;

    va_start(ap, fmt);
    pie = disable_interrupts();

    if (thrmgr_initialized)
        kprintf("<%s:%d> ", running_thread_name(), running_thread());
    kprintf("TRACE at %s:%d: ", filename, lineno);
    kvprintf(fmt, ap);
    kprintf("\n");

    restore_interrupts(pie);
    va_end(ap);
}

void halt(void) {
    sbi_shutdown();
}

void shutdown(void) __attribute__((weak, alias("halt")));

// The default shutdown() function is aliased to halt(). It may be overridden
// elsewhere to add additional processing at shutdown.
