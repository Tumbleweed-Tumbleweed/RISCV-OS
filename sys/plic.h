// plic.h - Platform-Level Interrupt Controller (PLIC) interface
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _PLIC_H_
#define _PLIC_H_

// PLATFORM-LEVEL INTERRUPT CONTROLLER
//
// The PLIC routes external interrupt sources to one or more privilege-mode
// contexts. This module provides a small, platform-specific interface used by
// the interrupt manager (intr.c).
//
// This driver is currently configured for the QEMU virt machine PLIC:
//
// - Interrupt sources are numbered 1..PLIC_SRC_CNT. Source 0 is reserved.
// - Priority levels are QEMU-specific in the range [PLIC_PRIO_MIN..PLIC_PRIO_MAX].
// - This implementation is currently hardwired to a single hart and the
//   supervisor-mode context for hart 0.
//
// This module only configures PLIC delivery. It does not enable global
// interrupts, nor does it register ISRs. See intr.h for that layer.

#define PLIC_PRIO_MIN 1 // QEMU-specific
#define PLIC_PRIO_MAX 7 // QEMU-specific
#define PLIC_SRC_CNT 96 // QEMU-specific

extern char plic_initialized;

extern void plic_init(void * mmio_base);

// Initializes the PLIC driver. The /mmio_base/ argument must be the base
// virtual address of the PLIC MMIO register region, mapped with appropriate
// permissions.
//
// After successful initialization, plic_init() sets /plic_initialized/,
// which is statically initialized to 0, to 1. The /mmio_base/ mapping must
// remain valid for the lifetime of the system or until the PLIC is no longer
// used.
//
// On entry plic_init() assumes:
// - /mmio_base/ is a valid pointer to the PLIC register MMIO region.
// - The PLIC MMIO region is accessible using aligned 32-bit loads and stores.
//
// On return plic_init() guarantees:
// - /plic_initialized/ is set to 1.
// - All interrupt sources are enabled specifically for hart 0's supervisor-mode 
//   context in the PLIC.
// - All sources are masked (disabled) by setting their priority to 0.
//
// * This function must _not_ be called from an ISR.
// * This function does not context-switch.

extern void plic_enable_source(int srcno, int prio);

// Enables an interrupt source by assigning it a non-zero priority. The /srcno/
// argument identifies the interrupt source to enable. The /prio/ argument
// specifies the priority level for that source.
//
// In this driver, plic_enable_source() enables a source for delivery by
// setting its priority to a non-zero value. Delviery is still subject to the
// context enable bitmask and the context threshold configured by plic_init()./
//
// On entry plic_enable_source() assumes:
// - /srcno/ is in the range [1..PLIC_SRC_CNT].
// - /prio/ is greater than 0.
// - plic_init() has been called and /plic_initialized/ is 1.
//
// On return plic_enable_source() guarantees:
// - The priority of source /srcno/ is set to /prio/.
// - If /prio/ is non-zero, the source is logically enabled.
//
// * This function may be called from an ISR.
// * This function does not context-switch.
//
// See also: plic_disable_source().

extern void plic_disable_source(int srcno);

// Disables an interrupt source by setting its priority to 0. The /srcno/
// argument identifies the interrupt source to disable. 
//
// On entry plic_disable_source() assumes:
// - /srcno/ is in the range [1..PLIC_SRC_CNT].
// - plic_init() has been called and /plic_initialized/ is 1.
//
// On return plic_disable_source() guarantees:
// - The priority of source /srcno/ is set to 0.
//
// * This function may be called from an ISR.
// * This function does not context-switch.
//
// See also: plic_enable_source().

extern int plic_claim_interrupt(void);

// Claims (acknowledges) the highest-priority pending interrupt for context 1
// (supervisor mode, hart 0). This implementation hardwires claiming to 
// context 1 only. Returns the source number of the claimed interrupt, 
// or 0 if no interrupt is pending.
//
// The returned source number is intended to be passed to
// plic_finish_interrupt() after the interrupt has been serviced.
//
// On entry plic_claim_interrupt() assumes:
// - plic_init() has been called and /plic_initialized/ is 1.
// - It is called from an external interrupt handler context, or in a manner
//   that ensures correct pairing with plic_finish_interrupt().
//
// On return plic_claim_interrupt() guarantees:
// - The return value is in the range [0..PLIC_SRC_CNT].
// - If the return value is non-zero, that source is claimed for servicing.
//
// * This function may be called from an ISR.
// * This function does not context-switch.
//
// See also: plic_finish_interrupt().

extern void plic_finish_interrupt(int srcno);

// Completes servicing for a previously claimed interrupt. The /srcno/ argument
// must be the value returned by plic_claim_interrupt() for the same context.
//
// On entry plic_finish_interrupt() assumes:
// - /srcno/ is in the range [1..PLIC_SRC_CNT] and was previously returned by
//   plic_claim_interrupt().
// - plic_init() has been called and /plic_initialized/ is 1.
//
// On return plic_finish_interrupt() guarantees:
// - The interrupt identified by /srcno/ is completed for the configured
//   context, allowing the PLIC to re-deliver that source when it becomes
//   pending again.
//
// * This function may be called from an ISR.
// * This function does not context-switch.

#endif // _PLIC_H_
