// timer.h - Timer and Alarms
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _TIMER_H_
#define _TIMER_H_

// SYSTEM TIMER AND PREEMPTION INTERRUPTS
//
// The timer subsystem provides two services:
//
// - One-time alarms for placing threads to sleep until a specified time.
// - [MP3cp3] Coarse periodic interrupts.
//
// [MP3cp3] The timer subsystem guarantees that a timer interrupt will be
// generated at a minimum frequency (given as interrupts per second) specified
// by the compile-time constant BOLT_FREQ, provided interrupts are enabled for
// at least some part of the period. The interrupt frequency and timing are not
// exact.
//
// All timer services use the system timer, which may beread using rdtime(), as
// their time reference.
//
// The timer subsystem uses the RISC-V hardware timer associated with the
// supervisor timer interrupt, and is intended to be the sole subsystem using
// that timer. The timer subsystem manages the STIE (supervisor timer interrupt
// enable) bit of the /sie/ register. This bit must _not_ be modified outside
// timer.c after timer_init() is called.
//
// The timer system does _not_ modify the global interrupt enable state except
// during short critical sections.
//

extern char timer_initialized;
extern unsigned int timer_frequency;
extern void timer_init(unsigned int freq);

// Initializes the timer subsystem. The /freq/ argument is the system timer
// frequency is ticks per second. This function must be called once at system
// initialization time before any other function declared in timer.h is used;
// timer_init() is normally called by the board initialization function.

// After successful initialization, timer_init() set the global variable
// /timer_initialized/, which is statically initialized to 0, to 1, indicating
// that the timer subsystem is now ready to provide alarm and periodic routine
// services. The /timer_frequency/ global variable, also initially 0, is set to
// /freq/. Global variables /timer_initialized/ and /timer_frequency/ must not
// be modified externally.
//
// Interrupts must be enabled for the timer system to function, however, they
// need not be enabled when timer_init() is called.
//
// On return timer_init() guarantees:
// - /timer_initialized/ is set to 1.
// - A timer interrupt will be generated at least /BOLT_FREQ/ times per second
//   when interrupts are enabled.
//
// * This function must be called once at system initialization time with
//   interrupts disabled before any other functions declared in timer.h.
//
// See also: alarm_init().


extern void handle_timer_interrupt(void);

// Services a timer interrupt. This function must be called by the system
// interrupt handler in response to a supervisor timer interrupt. It should be
// called with interrupts disabled, or in a manner that ensures mutual exclusion
// with respect to other timer functions.
//
// On entry handle_timer_interrupt() assumes:
// - It is called either (a) only when timer interrupts are enabled (sie.STIE=1
//   and sstatus.SIE=1), or (b) in a manner that ensures that it does not
//   execute concurrently with any other functions declared in timer.h.
//
// On return handle_timer_interrupt() guarantees:
// - All suspended threads that were waiting for an alarm not later than when
//   handle_timer_interrupt() was called become RUNNABLE.
// - If there are suspended threads still waiting for an alarm, the next timer
//   interrupt is set to occur no later than the earliest alarm time of any
//   threads still waiting for future alarms.
// - [MP3cp3] The next timer interrupt is set to occur no more than N ticks from
//   the current time, where N = timer_frequency / BOLT_FREQ.
//
// * This function must be called from an ISR in response to a timer interrupt.


// ONE-TIME ALARMS
//

extern void sleep_until(unsigned long long twake);

// Suspends the calling thread until the specified time. The /twake/ argument
// specifies the time at which the calling thread should be resumed. The /twake/
// argument is an absolute time, referenced to the same reference clock as the
// rdtime() function (riscv.h).
//
// If /twake/ is at or before the current time, as obtained by rdtime(), 
// sleep_until() returns immediately. Otherwise, the calling thread is
// suspended until /twake/.
//
// On return sleep_until() guarantees:
// - The current time, as returned by rdtime(), it not before /twake/.
//
// Performance guarantees:
// - If sleep_until() is called at or after /twake/, it returns immediately
//   without suspending the calling thread.
// - The calling thread becomes RUNNABLE no later than on return from first call
//   to handle_timer_interrupt() after /twake/.
//
// * This function may switch to another thread context.
// * This function may _not_ be called from an ISR.
//
// See also: sleep_ms(), sleep_us(), sleep_sec().

extern void sleep_sec(unsigned int sec);
extern void sleep_ms(unsigned int ms);
extern void sleep_us(unsigned int us);

// Suspends the calling thread for the specified duration, expressed in seconds,
// milliseconds, or microseconds. These functions are equivalent to calling
// sleep_until() with the specified duration added to the value returned by
// rdtime(). See sleep_until() for details.
//
// * These functions may switch to another thread context.
// * These functions may _not_ be called from an ISR.

#endif // _TIMER_H_
