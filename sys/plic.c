// plic.c - Interface to RISC-V PLIC
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef PLIC_TRACE
#define TRACE
#endif

#ifdef PLIC_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "plic.h"
#include "misc.h"

#include <stdint.h>

// ---COMPILE-TIME CONFIGURATION--- //

#define PLIC_SRC_CNT 96  // QEMU VIRT_IRQCHIP_NUM_SOURCES
#define PLIC_CTX_CNT 2

// ---INTERNAL MACRO DEFINITIONS--- //

// CTX(i,0) is hartid /i/ M-mode context
// CTX(i,1) is hartid /i/ S-mode context

#define CTX(i,s) (2*(i)+(s))

// ---INTERNAL TYPE DEFINITIONS--- //

struct plic_regs {
	union {
		uint32_t priority[PLIC_SRC_CNT]; /**< Interrupt Priorities registers */
		char _reserved_priority[0x1000];
	};

	union {
		uint32_t pending[PLIC_SRC_CNT/32]; /**< Interrupt Pending Bits registers */
		char _reserved_pending[0x1000];
	};

	union {
		uint32_t enable[PLIC_CTX_CNT][32]; /**< Interrupt Enables registers */
		char _reserved_enable[0x200000-0x2000];
	};

	struct {
		union {
			struct {
				uint32_t threshold;	/**< Priority Thresholds registers */
				uint32_t claim;	/**< Interrupt Claim/Completion registers */
			};
			
			char _reserved_ctxctl[0x1000];
		};
	} ctx[PLIC_CTX_CNT];
};

// ---INTERNAL FUNCTION DECLARATIONS--- //

static inline void plic_set_source_priority (uint32_t srcno, uint32_t level);

static inline int plic_source_pending(uint32_t srcno);

static inline void plic_enable_source_for_context (uint32_t ctxno, uint32_t srcno);

static inline void plic_disable_source_for_context (uint32_t ctxno, uint32_t srcno);

static inline void plic_set_context_threshold (uint32_t ctxno, uint32_t level);

static inline uint32_t plic_claim_context_interrupt (uint32_t ctxno);

static inline void plic_complete_context_interrupt (uint32_t ctxno, uint32_t srcno);

static void plic_enable_all_sources_for_context(uint32_t ctxno);

static void plic_disable_all_sources_for_context(uint32_t ctxno);

// ---EXPORTED GLOBAL VARIABLES--- //

char plic_initialized = 0;

// We currently only support single-hart operation, sending interrupts to S mode
// on hart 0 (context 0). The low-level PLIC functions already understand
// contexts, so we only need to modify the high-level functions (plic_init,
// plic_claim_request, plic_finish_request)to add support for multiple harts.

// ---INTERNAL GLOBAL VARIABLES--- //

static struct plic_regs * plic;		// plic pointer

// ---EXPORTED FUNCTION DEFINITIONS--- //

void plic_init(void * mmio_base) {
	int i;
	plic = mmio_base;

	// Disable all sources by setting priority to 0
	for (i = 1; i < PLIC_SRC_CNT; i++)
		plic_set_source_priority(i, 0);
	
	// Route all sources to S mode on hart 0 only
	for (int i = 0; i < PLIC_CTX_CNT; i++)
		plic_disable_all_sources_for_context(i);
	
	plic_enable_all_sources_for_context(CTX(0,1));
	plic_set_context_threshold(CTX(0,1), 0);
	plic_initialized = 1;
}

void plic_enable_source(int srcno, int prio) {
	trace("%s(srcno=%d,prio=%d)", __func__, srcno, prio);
	assert (0 < srcno && srcno < PLIC_SRC_CNT);
	assert (prio > 0);
	assert(plic_initialized == 1);

	plic_set_source_priority(srcno, prio);
}

void plic_disable_source(int srcno) {
	trace("%s()", __func__);
	assert (0 < srcno && srcno < PLIC_SRC_CNT);
	assert(plic_initialized == 1);

	plic_set_source_priority(srcno, 0);
}

int plic_claim_interrupt(void) {
	trace("%s()", __func__);
	assert(plic_initialized == 1);
	return plic_claim_context_interrupt(CTX(0,1));
}

void plic_finish_interrupt(int irqno) {
	trace("%s(irqno=%d)", __func__, irqno);
	assert(plic_initialized == 1);
	plic_complete_context_interrupt(CTX(0,1), irqno);
}

// ---INTERNAL FUNCTION DEFINITIONS--- //

/*
INPUTS: uint32_t srcno - value of source to be set
		uint32_t level - value to set source priority levels
OUTPUTS: no output
DESCRIPTION: takes a source number and level (0-7) and sets that sources priority
SIDE EFFECTS: an individual plic source has its priority updated
*/
static inline void plic_set_source_priority(uint32_t srcno, uint32_t level) {
	plic->priority[srcno] = level;
}

/*
INPUTS: uint32_t srcno - source number to read
OUTPUTS: returns 1 if source is pending returns 0 if false
DESCRIPTION: determines if a specific source is pending for an interupt
SIDE EFFECTS: no side effects
*/
static inline int plic_source_pending(uint32_t srcno) {			
	uint32_t arr_pos = srcno / 32;			// locate the array posiiton 
	uint32_t bit_pos = srcno % 32;			// locate the bit we need to change 
	uint32_t bit_flip = plic->pending[arr_pos];
	bit_flip = (bit_flip >> bit_pos) & 1;			// shift the bit into the least signifigant position to 'and' out remainging bits
	if (bit_flip == 1) return 1;
	return 0;
}

/*
INPUTS: uint32_t ctxno - context number
		uint32_t srcno - source to be enabled
OUTPUTS: no output
DESCRIPTION: enables a specific source within a context
SIDE EFFECTS: a single bit with in a single source context inside the plic is enabled
*/
static inline void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno) { 
	uint32_t arr_pos = srcno / 32;			// locate the array posiiton 
	uint32_t bit_pos = srcno % 32;			// locate the bit we need to change 
	uint32_t bit_flip = 1;
	bit_flip = bit_flip << bit_pos;
	plic->enable[ctxno][arr_pos] = plic->enable[ctxno][arr_pos] | bit_flip;
}

/*
INPUTS: uint32_t ctxno - context number 
		uint32_t srcno - source to be disabled
OUTPUTS: no output
DESCRIPTION: disables a specific source wihtin a context
SIDE EFFECTS: a single bit with in a single source context inside the plic is disabled.
*/
static inline void plic_disable_source_for_context(uint32_t ctxno, uint32_t srcno) { 
	uint32_t arr_pos = srcno / 32;			// locate the array posiiton 
	uint32_t bit_pos = srcno % 32;			// locate the bit we need to change 
	uint32_t bit_flip = 1;
	bit_flip = bit_flip << bit_pos;
	bit_flip = ~bit_flip;
	plic->enable[ctxno][arr_pos] = plic->enable[ctxno][arr_pos] & bit_flip;
}

/*
INPUTS: uint32_t ctxno - context number
		uint32_t level - level to set threshold
OUTPUTS: no output
DESCRIPTION: takes a context and priority level to set the minimum threshold for an interupt
SIDE EFFECTS: the threshold register will mask all priority levels below the given level for a given context to 0.
*/
static inline void plic_set_context_threshold(uint32_t ctxno, uint32_t level) {
	plic->ctx[ctxno].threshold = level;
}	

/*
INPUTS: uint32_t ctxno - context number to parse for claim
OUTPUTS: returns the highest priority source within a context; 0 if none
DESCRIPTION: the higest priorty source within the context is claimed for interupt
SIDE EFFECTS: if claim is valid not additional claims from this course will be flagged until its completeds
*/
static inline uint32_t plic_claim_context_interrupt(uint32_t ctxno) {
	return plic->ctx[ctxno].claim;
}

/*
INPUTS: uint32_t ctxno - context number to be completed
		uint32_t srcno - source to be completed
OUTPUTS: no output
DESCRIPTION: writes the source for a given context to the claim register which completes the interupt
SIDE EFFECTS: once completed additional flags from the source are able to be recived from the source again
*/
static inline void plic_complete_context_interrupt(uint32_t ctxno, uint32_t srcno) {
	plic->ctx[ctxno].claim = srcno;
}

/*
INPUTS: uint32_t ctxno - context number to be enabled
OUTPUTS: no output
DESCRIPTION: takes a context number and enables all sources within it
SIDE EFFECTS: all sources in plics enable[context] are set to 1
*/
static void plic_enable_all_sources_for_context(uint32_t ctxno) {
	for (int i = 0; i < 32; i++)
		plic_enable_source_for_context(ctxno, i);
}

/*
INPUTS: uint32_t ctxno - context number to be disabled
OUTPUTS: no output
DESCRIPTION: takes a context number and disables all sources within it
SIDE EFFECTS: all sources in plics enable[context] are set to 0
*/
static void plic_disable_all_sources_for_context(uint32_t ctxno) {
	for (int i = 0; i < 32; i++)
		plic_disable_source_for_context(ctxno, i);		
}
