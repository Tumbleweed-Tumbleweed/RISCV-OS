// intr.c - Interrupt manager
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef INTR_TRACE
#define TRACE
#endif

#ifdef INTR_DEBUG
#define DEBUG
#endif

#include "intr.h"

#include <stddef.h>

#include "conf.h"
#include "misc.h"
#include "riscv.h"
#include "thread.h"
#include "timer.h"
#include "trap.h"

// ---EXPORTED GLOBAL VARIABLE DEFINITIONS---//
char intrmgr_initialized = 0;

// ---INTERNAL GLOBAL VARIABLE DEFINITIONS--- //
static struct {
    void (*isr)(int, void*);
    void* israux;
} isrtab[INTR_SRC_CNT];

// ---INTERNAL FUNCTION DECLARATIONS--- //
static void handle_interrupt(unsigned int cause);
static void handle_extern_interrupt(void);

// Dispatches a supervisor-mode interrupt by cause code. The /cause/ argument is
// a supervisor interrupt cause value as reported by the trap handler.
//
// On entry handle_interrupt() assumes:
// - It is called with interrupts disabled from a trap context.
//
// On return handle_interrupt() guarantees:
// - The appropriate subsystem interrupt handler is invoked for supported
//   causes.
// - The system panics for unsupported causes.
//
// * This function is called from an ISR.
// * This function does not context-switch.

// ---EXPORTED FUNCTION DEFINITIONS--- //
void intrmgr_init(void) {
    trace("%s()", __func__);

    disable_interrupts();
    
    assert (plic_initialized);

    // Enable external interrupts. The timer module controls the sie.STIE bit.

    csrs_sie(RISCV_SIE_SEIE);

    intrmgr_initialized = 1;
}

void enable_intr_source (int srcno, int prio, void (*isr)(int srcno, void* aux), void* israux) {
    assert(0 < srcno && srcno < NIRQ);
    assert(0 < prio);

    isrtab[srcno].isr = isr;
    isrtab[srcno].israux = israux;

    if (prio > INTR_PRIO_MAX)
        prio = INTR_PRIO_MAX;
    
    plic_enable_source(srcno, prio);
}

void disable_intr_source(int srcno) {
    plic_disable_source(srcno);
    isrtab[srcno].isr = NULL;
    isrtab[srcno].israux = NULL;
}

/*
INPUTS: unsigned int cause - indicates the reason for the interupt
OUTPUTS: no output
DESCRIPTION: passes the s_mode cause the the handler
SIDE EFFECTS: no side effect
*/
void handle_smode_interrupt(unsigned int cause) {
    handle_interrupt(cause);
}

/*
INPUTS: unsigned int cause - indicates the reason for the interupt
OUTPUTS: no output
DESCRIPTION: passes the u_mode cause the the handler
SIDE EFFECTS: no side effect
*/
void handle_umode_interrupt(unsigned int cause) {
    handle_interrupt(cause);
    enable_interrupts();
    yield_running_thread();
    return;
}

extern long enable_interrupts(void) {
    return csrrsi_sstatus_SIE();
}

extern long disable_interrupts(void) {
    return csrrci_sstatus_SIE();
}

extern void restore_interrupts(int prev_status) {
    csrwi_sstatus_SIE(prev_status);
}

extern int interrupts_enabled(void) {
    return ((csrr_sstatus() & RISCV_SSTATUS_SIE) != 0);
}

extern int interrupts_disabled(void) {
    return ((csrr_sstatus() & RISCV_SSTATUS_SIE) == 0);
}

// ---INTERNAL FUNCTION DEFINITIONS--- //

/*
INPUTS: unsigned int cause - integer to indicate what and where the error is
OUTPUTS: no output
DESCRIPTION: passes the interupt to the correct handler, if error unknown panic
SIDE EFFECTS: no addtional side effects
*/
void handle_interrupt(unsigned int cause) {
    switch(cause) {
    case RISCV_SCAUSE_STI:
        handle_timer_interrupt();
        break;

    case RISCV_SCAUSE_SEI:
        handle_extern_interrupt();
        break;

    default:
        panic("unhandled interrupt");
        break;
    }
}

/*
INPUTS: no inputs
OUTPUTS: no output
DESCRIPTION: process the interupt and executes specified isr if possible
SIDE EFFECTS: disables called interupt signal and re-enables this interupt signal once its completed
*/
void handle_extern_interrupt(void) {
    int srcno = plic_claim_interrupt();      // hard coded to context 1
    assert(0 <= srcno && srcno < NIRQ);      // NIRQ == PLIC source count
    if (srcno == 0) return;
    if(isrtab[srcno].isr != NULL)       // ensure this is a registered isr
        isrtab[srcno].isr(srcno, isrtab[srcno].israux);     // execute isr
    plic_finish_interrupt(srcno);           // end interupt sequenec

}
