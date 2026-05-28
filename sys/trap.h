// trap.h - Trap frame and trap handling
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _TRAP_H_
#define _TRAP_H_

#include <stdint.h>

// This module defines the S-mode trap frame layout and the C entry points that
// trap.s uses to dispatch exceptions and interrupts.
//
// The assembly code in trap.s relies on the exact field order and offsets of
// struct trap_frame. Do not reorder fields, insert padding, or change types
// without updating the corresponding .equ offsets in trap.s.

// The trap_frame structure holds a snapshot of CPU state at trap entry for traps
// handled in S mode.
//
// Notes on layout
// - The general-purpose registers are stored in a fixed order that matches the
//   offsets defined in trap.s.

struct trap_frame {
    long a0, a1, a2, a3, a4, a5, a6, a7;      // argument registers
    long t0, t1, t2, t3, t4, t5, t6;          // temporaries
    long s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11; // saved registers (s0/fp handled separately)
    void * ra;                                // return address
    void * sp;                                // user or kernel sp at trap entry (mode-dependent)
    void * gp;                                // global pointer (restored for U-mode return)
    void * tp;                                // thread pointer (restored for U-mode return)
    long sstatus;                             // saved sstatus at trap entry
    unsigned long long instret;               // retired instruction counter snapshot
    void * fp;                                // must be here (see trap.s)
    void * sepc;                              // must be here (see trap.s)
};

extern void trap_frame_jump(struct trap_frame * tfr, void * sscratch)
    __attribute__ ((noreturn));

// Restores CPU state from the trap frame and returns via sret.
//
// The exact behavior depends on what the caller populates the trap frame with:
// - sepc controls the resume PC.
// - sstatus controls the privilege level and interrupt enable state after sret.
//
// On entry trap_frame_jump() assumes:
// - /tfr/ points to a valid trap frame whose layout matches trap.s.
//
// On return trap_frame_jump() guarantees:
// - Does not return (transfers control via sret).


// Trap dispatch entry points used by trap.s.
//
// trap.s reads scause, classifies the trap as an exception (msb = 0) or an
// interrupt (msb = 1), clears the interrupt bit when needed, and then branches
// to one of the handlers below.
//
// Naming
// - "umode" means the trap originated while executing in U mode (but is handled
//   in S mode).
// - "smode" means the trap originated while executing in S mode (but is still 
//   handled S mode.
//
// Exception handlers are implemented in excp.c.
// Interrupt handlers are implemented in intr.c.

extern void handle_smode_exception(unsigned int cause, struct trap_frame * tfr);

// Handles an exception that occurred while already executing in S mode.
//
// On entry handle_smode_exception() assumes:
// - /cause/ is the exception code (scause with msb cleared).
// - /tfr/ points to the trap frame captured by trap.s.
//
// On return handle_smode_exception() guarantees:
// - Returns to trap.s, which will restore state and sret.
//
// * This function may be called from an ISR.

extern void handle_umode_exception(unsigned int cause, struct trap_frame * tfr);

// Handles an exception that occurred while executing in U mode.
//
// On entry handle_umode_exception() assumes:
// - /cause/ is the exception code (scause with msb cleared).
// - /tfr/ points to the trap frame capturing the interrupted U-mode state.
//
// On return handle_umode_exception() guarantees:
// - Returns to trap.s, which will restore U-mode state and sret.
//
// * This function may be called from an ISR.

extern void handle_smode_interrupt(unsigned int cause);

// Handles an interrupt that occurred while executing in S mode.
//
// trap.s clears the interrupt bit of scause before passing /cause/ here.
//
// On entry handle_smode_interrupt() assumes:
// - /cause/ is the interrupt ID (scause with msb cleared).
//
// On return handle_smode_interrupt() guarantees:
// - Returns to trap.s, which will restore state and sret.
//
// * This function may be called from an ISR.

extern void handle_umode_interrupt(unsigned int cause);

// Handles an interrupt that occurred while executing in U mode.
//
// trap.s clears the interrupt bit of scause before passing /cause/ here.
//
// On entry handle_umode_interrupt() assumes:
// - /cause/ is the interrupt ID (scause with msb cleared).
//
// On return handle_umode_interrupt() guarantees:
// - Returns to trap.s, which will restore U-mode state and sret.
//
// * This function may be called from an ISR.

#endif  // _TRAP_H_
