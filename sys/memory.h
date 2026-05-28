// memory.h - Physical and virtual memory manager
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _MEMORY_H_
#define _MEMORY_H_

#include <stddef.h>
#include <stdint.h>

#include "trap.h"  // for struct trap_frame

// EXPORTED CONSTANTS
//

#define PAGE_ORDER 12
#define PAGE_SIZE (1UL << PAGE_ORDER)
#define MEGA_SIZE ((1UL << 9) * PAGE_SIZE)  // megapage size
#define GIGA_SIZE ((1UL << 9) * MEGA_SIZE)  // gigapage size

// Flags for alloc_and_map_range() and set_range_flags()

#define PTE_V (1 << 0)  // internal use only
#define PTE_R (1 << 1)
#define PTE_W (1 << 2)
#define PTE_X (1 << 3)
#define PTE_U (1 << 4)
#define PTE_G (1 << 5)
#define PTE_A (1 << 6)  // internal use only
#define PTE_D (1 << 7)  // internal use only

// EXPORTED TYPE DEFINITIONS
//

// We refer to a memory space using an opaque memory space tag

typedef unsigned long mtag_t;

//EXPORTED STRUCT DEFINITIONS
//

struct mregion {
    unsigned long pma;
    unsigned long size;
};

// EXPORTED FUNCTION DECLARATIONS
//

extern char memory_initialized;

extern void memory_init (
    const struct mregion * ram, unsigned long ramcnt,
    const struct mregion * mmio, unsigned long mmiocnt,
    const struct mregion * resv, unsigned long resvcnt);

// Initializes the main memory mappings, sets up the heap memory manager, and
// adds any remaining memory to the free chunk list. This function must be called
// before any other functions declared in memory.h. The global variable
// /memory_initialized/, which is statically initialized to 0, is set to 1 by
// memory_init(); it must not be modified externally.
//
// On return from memory_init(), the current memory space is the distinguished
// /main/ memory space with mtag /main_mtag/. Additional memory spaces may be
// created using calls to clone_active_mspace().
//
// On return memory_init() guarantees:
// - /memory_initialized/ is set to 1.
// - /heap_init()/ is called.
// - The free chunk list is initialized.
//
// * This function must _not_ be called from an ISR.
//
// See clone_active_mspace().

extern mtag_t active_mspace(void);

// Returns the mtag of the currently active memory space
//
// * This function may be called from an ISR.

extern mtag_t switch_mspace(mtag_t mtag);

// Switches to the memory table specified by /mtag/
//
// * This function must _not_ be called from an ISR.

extern mtag_t clone_active_mspace(void);

// Copies all pages and page tables from the active memory space into newly
// allocated memory.
//
// The new memory space retains references to all global pages, and contains
// a copy of all of the data in all non-global pages in the active memory space.
//
// Returns the /mtag_t/ associated with the newly generated memory space.
//
// * This function must _not_ be called from an ISR.

extern void reset_active_mspace(void);

// Unmaps and frees all non-global pages of the currently active memory space.
//
// On return guarantees that all unused pages are returned to the allocator.
//
// * This function must _not_ be called from an ISR.

extern mtag_t discard_active_mspace(void);

// Unmaps all pages and frees all non-global pages of the currently active
// memory space, and switches to the main memory space.
//
// On return, guarantees that all pages associated with only the discarded memory
// space are returned to the allocator (including any non-global intermediate
// pages)
//
// * This function must _not_ be called from an ISR.

extern void* map_page(uintptr_t vma, void* pp, int rwxug_flags);

// Adds the specified page with the provided virtual memory address and flags to
// the active memory table.
//
// * This function must _not_ be called from an ISR.

extern void* map_range(uintptr_t vma, size_t size, void* pp, int rwxug_flags);

// Adds the specified consecutive pages starting at the specified virtual memory
// address with provided flags to the active memory table.
//
// * This function must _not_ be called from an ISR.

extern void* alloc_and_map_range(uintptr_t vma, size_t size, int rwxug_flags);

// Allocates pages for and maps the specified range of memory into the active virtual
// memory table.
//
// * This function must _not_ be called from an ISR.

extern void set_range_flags(const void* vp, size_t size, int rwxug_flags);

// Sets passed flags for pages in range, overwriting previous flags. Rounds down vp
// and rounds up vp to be a multiple of PAGE_SIZE.
//
// * This function must _not_ be called from an ISR.

extern void unmap_and_free_range(void* vp, size_t size);

// Unmaps and frees pages in range in the active memory table. Does not discriminate
// against global pages.
//
// * This function must _not_ be called from an ISR.

extern int enforce_vptr(const void* vp, size_t size, int rwxug_flags);

// Checks whether an array contains only well-formed addresses, and that vp _ len
// does not wrap around zero. Iterates over pages in range, and if flags require
// only read and/or write permissions, maps any portions of the array that are
// unmapped. Checks whether complete range has all flags required.
//
// * This function must _not_ be called from an ISR.

extern int validate_vstr(const char* vs, int rwxug_flags);

// Iterates through string specified at vs until:
// - A null termination is found (return 0)
// - We enter an unmapped page (error)
// - We enter a page which does not match the passed in flags (error)
//
// validate_vstr does not allocate or map any new pages.
//
// * This function may be called from an ISR.

extern void* alloc_phys_page(void);

// Allocates a single new page and returns a pointer to its physical address.
// If there are multiple suitable chunks, finds the best fit chunk.
//
// * This function must _not_ be called from an ISR.

extern void free_phys_page(void* pp);

// Returns a physical page to the allocator for reuse
//
// * This function must _not_ be called from an ISR.

extern void* alloc_phys_pages(unsigned int cnt);

// Allocates the passed number of physical pages contiguously from the free chunk
// list. If there are multiple suitable free chunks, finds the best fit chunk.
// If no chunk is suitable for allocation request, may panic. Otherwise, removes
// allocated pages from the free chunk list and returns a pointer to the start.
//
// * This function must _not_ be called from an ISR.

extern void free_phys_pages(void* pp, unsigned int cnt);

// Frees a set of contiguous pages starting at pp and returns them to the
// allocator.
//
// * This function must _not_ be called from an ISR.

extern unsigned long free_phys_page_count(void);

// Counts the number of remaining pages in the free chunk list.
//
// * This function may be called from an ISR.

extern int handle_umode_page_fault(struct trap_frame* tfr, uintptr_t vma);

// Called by handle_umode_exception() in excp.c to handle U mode load and store
// page faults. Allocates and maps the faulting address to a physical page in the
// memory table.
//
// Returns 1 to indicate that the fault has been handled (instruction to be restarted)
// and 0 to indicate that the page fault is fatal and the process should be terminated.
//
// * This function may _only_ be called in response to a load or store access fault
// from U mode (unless you really know what you're doing)

#endif
