// intr.h - Interrupt manager
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _INTR_H_
#define _INTR_H_

#include "plic.h"
#include "riscv.h"

// The interrupt manager provides:
// - A registration API for external interrupt sources (via the PLIC).
// - Helpers for manipulating the global interrupt enable state (sstatus.SIE).
// - A dispatcher used by the trap handler to service supervisor-mode interrupts.
//
// The interrupt manager is responsible for enabling supervisor external
// interrupts (sie.SEIE). The timer subsystem is responsible for enabling and
// disabling supervisor timer interrupts (sie.STIE). 
//
// External interrupt sources are identified by a platform-specific source
// number /srcno/ in the range [1..INTR_SRC_CNT]. Source 0 is reserved as "no
// interrupt" by the PLIC claim interface.
//
// Interrupt sources are handled by an ISR function:
//
//     void xxx_isr(int srcno, void * aux);
//
// The /aux/ pointer is stored verbatim and passed back to the ISR when the
// interrupt is dispatched. The lifetime of /aux/ must extend for as long as the
// total lifetime of the system.

// EXPORTED CONSTANT DEFINITIONS
//

#define INTR_PRIO_MIN PLIC_PRIO_MIN // Minimum priority
#define INTR_PRIO_MAX PLIC_PRIO_MAX // Maximum priority
#define INTR_SRC_CNT  PLIC_SRC_CNT  // Number of interrupt sources

// EXPORTED FUNCTION DECLARATIONS
//

extern char intrmgr_initialized;
extern void intrmgr_init(void);

// Initializes the interrupt manager. This function must be called before any other
// functions declared in intr.h. The global variable /intrmgr_initialized/,
// which is statically initialized to 0, is set to 1 by intrmgr_init(); it must
// not be modified externally.
//
// On entry intrmgr_init() assumes:
// - The PLIC has been initialized.
//
// On return intrmgr_init() guarantees:
// - /intrmgr_initialized/ is set to 1.
// - Supervisor external interrupts are enabled (sie.SEIE=1).
// - Global interrupts are disabled (sstatus.SIE=0).
//
// * This function must be called once at system initialization time.
// * This function must _not_ be called from an ISR.
//
// See also: plic_init()

extern void enable_intr_source (
    int srcno,
    int prio,
    void (*isr)(int srcno, void * aux),
    void * israux
);

// Registers an ISR for an external interrupt source and enables that source.
// The /srcno/ argument is the interrupt source to enable, and must be greater
// than 0. The /prio/ argument is the priority to assign to the source, and must
// be greater than 0. Calling enable_intr_source() with /prio/ greater than
// INTR_PRIO_MAX is equivalent to calling it with /prio/ equal to INTR_PRIO_MAX.
//
// The /isr/ argument is the ISR that should be called when the source raises an
// interrupt. it must be a pointer to a function of the correct type. It will be
// called with the interrupt soruce number as the first argument and /israux/ as
// the second argument to service an interrupt from source /srcno/.
//
// The /isr/ pointer must remain valid for at least as long as the interrupt
// source remains enabled. The /israux/ pointer is passed to the ISR as
// provided; it is not checked for validity for enable_intr_source().
//
// On entry enable_intr_source() assumes:
// - /srcno/ is in the range [1..INTR_SRC_CNT].
// - /prio/ is greater than 0.
// - /isr/ is a pointer to a function of the required type.
//
// On return enable_intr_source() guarantees:
// - Unless the interrupt source /srcno/ is later disabled, the interrupt
//   manager will call /isr/ with the /israux/ argument when /srcno/ asserts an
//   itnerrupt and global interrupts are enabled (sstatus.SIE=1).
//
// * This function may be called from an ISR provided it is not called
//   concurrently with enable_intr_source() and disable_intr_source() for the
//   same source.
//
// See also: disable_intr_source().

extern void disable_intr_source(int srcno);

// Disables an external interrupt source. An interrupt asserted by the source
// will not cause an external interrupt and the interrupt manager will not call
// any previously registered ISR in response. This function should be called to
// disable an source and ISR previously enabled by enable_intr_source().
//
// On entry disable_intr_source() assumes:
// - /srcno/ is in the range [1..INTR_SRC_CNT].
//
// On return disable_intr_source() guarantees:
// - The PLIC is configured so that source /srcno/ is disabled.
// - The ISR dispatch table entry for /srcno/ is cleared.
//
// * This function may be called from an ISR.
//
// See also: enable_intr_source().

extern long enable_interrupts(void);

// Sets sstatus.SIE to 1 (globally enables interrupts) and returns the previous
// value of the sstatus CSR.
//
// On return enable_interrupts() guarantees:
// - sstatus.SIE is set (interrupts are globally enabled).
//
// * This function must _not_ be called from an ISR.
//
// See also: disable_interrupts(), restore_interrupts(), interrupts_enabled().


extern long disable_interrupts(void);

// Clears sstatus.SIE (globally disables interrupts) and returns the previous
// value of the sstatus CSR.
//
// The return value is typically passed to restore_interrupts() to restore
// the prior interrupt enable state.
//
// On return disable_interrupts() guarantees:
// - sstatus.SIE is clear (interrupts are globally disabled).
//
// * This function may be called from an ISR.
//
// See also: enable_interrupts(), restore_interrupts(), interrupts_disabled().


extern void restore_interrupts(int prev_status);

// Restores sstatus.SIE to its previous value. The /prev_status/ argument is
// should be the value returned by enable_interrupts() or disable_interrupts().
// Only the RISCV_SSTATUS_SIE bit of /prev_status/ is modifed.
//
// On entry restore_interrupts() assumes:
// - /prev_status/ is a value previously returned by enable_interrupts() or
//   disable_interrupts(), or any value whose RISCV_SSTATUS_SIE bit matches the
//   desired interrupt enable state.
//
// On return restore_interrupts() guarantees:
// - sstatus.SIE matches (prev_status & RISCV_SSTATUS_SIE).
//
// * This function may be called from an ISR.
//
// See also: enable_interrupts(), disable_interrupts().


extern int interrupts_enabled(void);

// Returns 1 if interrupts are globally enabled (sstatus.SIE is set) and zero
// otherwise.
//
// * This function may be called from an ISR.
//
// See also: interrupts_disabled().


extern int interrupts_disabled(void);

// Returns 1 if interrupts are globally disabled (sstatus.SIE is clear) and zero
// otherwise.
//
// * This function may be called from an ISR.
//
// See also: interrupts_enabled().

#endif // _INTR_H_
