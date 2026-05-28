// timer.c - Timer and Alarms
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef TIMER_TRACE
#define TRACE
#endif

#ifdef TIMER_DEBUG
#define DEBUG
#endif

#include "timer.h"
#include "thread.h"
#include "riscv.h"
#include "sbi.h" // for sbi_set_timer
#include "intr.h"
#include "misc.h"
#include "string.h"

#include <stddef.h>
#include <limits.h> // for ULLONG_MAX

// ---COMPILE-TIME OPTIONS--- //
#ifndef BOLT_FREQ
#define BOLT_FREQ 50 // Hz [MP3cp3]
#endif

// ---INTERNAL TYPE DEFINITIONS--- //
struct timer_alarm {
    struct timer_alarm * next_alarm;            // pointer to next timer alarm
    struct condition timer_cond;                // timer condition variable
    unsigned long long wake_time;               // alarm timer wake time
    int timer_passed;                           // essentially a boolean to let us know if we passed wake time or not
};

// ---EXPORTED GLOBAL VARIABLES--- //
char timer_initialized = 0;
unsigned int timer_frequency = 0;

// ---INTERNAL GLOBAL VARIABLES--- //
static struct timer_alarm * sleep_list; // list of pending alarms

// ---INTERNAL FUNCTION DECLARATIONS--- //
static void enable_timer_interrupts(void); // sets sie.STIE=1
static void disable_timer_interrupts(void); // clears sie.STIE

// ---EXPORTED FUNCTION DEFINITIONS--- //

/*
INPUTS: unsigned int freq - frequency we will set timer_frequency to
OUTPUTS: no outputs
DESCRIPTION: initialzies our alarm timer subsystem and indicates to system it is ready for use
SIDE EFFECTS: timer subsystem is set to active; timer_interrupts disabled
*/
void timer_init(unsigned int freq) {
    timer_frequency = freq;                                 // we do not allocated and define the alaram struct here
    sleep_list = NULL;
    enable_timer_interrupts();                               // allow timer interupts
    sbi_set_timer( rdtime() + (freq / BOLT_FREQ));           // timer will automatically go off to allow for preemption
    timer_initialized = 1;
}

/*
INPUTS: unsigned long long twake - the wake time for the alarm we are setting
OUTPUTS: no outputs
DESCRIPTION: adds new timer to sleep_list, sleeps that timer until twake time is passed
SIDE EFFECTS: new alarm_timer intizialized; may condition wait; disables interupts while working
*/
void sleep_until(unsigned long long twake) {
    disable_interrupts();
    if (twake <= rdtime()){                         // if twake is less than the current time it can not be added to our list, inside disable to avoid race condition
        enable_interrupts();
        return;
    }
    struct timer_alarm talarm;

    memset(&talarm, 0, sizeof(talarm));             // allocate and intialize alarm
    talarm.wake_time = twake;
    talarm.next_alarm = NULL;
    talarm.timer_passed = 0;
    condition_init(&talarm.timer_cond, "t_alarm_cond");

    struct timer_alarm * curr = sleep_list;
    struct timer_alarm * prev = NULL;
    while (curr != NULL && curr->wake_time <= twake) {          // find correct position to insert new alarm
        prev = curr;
        curr = curr->next_alarm;
    }

    talarm.next_alarm = curr;
    if (prev == NULL) sleep_list = &talarm;
    else prev->next_alarm = &talarm;

    if (sleep_list == &talarm) {                    // if our new alarm is the first in the sleep list then update mtimecmp to be the new wake time
        sbi_set_timer(talarm.wake_time);
        enable_timer_interrupts();
    }

    while (talarm.timer_passed == 0) {              // continue to condition wait as long as our timer_passed is not updated
        condition_wait(&talarm.timer_cond);
        disable_interrupts();
    }
    enable_interrupts();
}

void sleep_sec(unsigned int sec) {
    sleep_until(rdtime() + 1ULL * sec * timer_frequency);
}

void sleep_ms(unsigned int ms) {
    sleep_until(rdtime() + 1ULL * ms * timer_frequency / 1000);
}

void sleep_us(unsigned int us) {
    sleep_until(rdtime() + 1ULL * us * timer_frequency / 1000 / 1000);
}

/*
INPUTS: no intputs
OUTPUTS: no outputs
DESCRIPTION: essentially the isr for timer, updates timer status and alarm thresholds
SIDE EFFECTS: may disable timer interupts, may condition broadcast, sleep list updated
*/
void handle_timer_interrupt(void) {
    unsigned long long tnow = rdtime();
    trace("[%lu] %s()", tnow, __func__);
    struct timer_alarm * talarm;

    while(sleep_list != NULL && ((sleep_list->wake_time) <= tnow)) {
        talarm = sleep_list;                        // continue through the sleep list until we have caught up to tnow time
        sleep_list = talarm->next_alarm;
        talarm->timer_passed = 1;                   
        condition_broadcast(&talarm->timer_cond);
    }

    unsigned long long next_wake = rdtime() + (timer_frequency / BOLT_FREQ);
    if (sleep_list != NULL && sleep_list->wake_time < next_wake) sbi_set_timer(sleep_list->wake_time);
    else sbi_set_timer(next_wake);
    enable_timer_interrupts();
}

// --- INTERNAL FUNCTION DEFINITIONS--- //
void enable_timer_interrupts(void) {
    csrs_sie(RISCV_SIE_STIE);
}

void disable_timer_interrupts(void) {
    csrc_sie(RISCV_SIE_STIE);
}
