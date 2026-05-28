// heap.h - Heap memory manager
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

// The heap module provides dynamic memory allocation for kernel code.
//
// This is the allocator used by most of the kernel when the size or lifetime of
// an object is not known at compile time. It is analogous to malloc/calloc/
// free in user space, but it is tailoreid for kernel use and integrates with
// the kernel's page allocator when available.
//
// External users should treat this module as a service with a small API:
//   - heap_init() must be called once during boot before using kmalloc/kfree.
//   - kmalloc() allocates a requested number of bytes.
//   - kcalloc() allocates and zero-fills an array.
//   - kfree() releases a previously allocated object.
//   - copy_heap_stats() reports allocator statistics.
//

#ifndef _HEAP_H_
#define _HEAP_H_

#include <stddef.h>

#include "memory.h"

#define HEAP_ALLOC_MAX 4032 // Maximum allocation request size (in bytes)

// Heap statistics snapshot.
//
// These fields are intended for introspection.
// Most kernel code should not make functional decisions based on
// these values.

struct heap_stats {
    unsigned long cumul_objects;    // total successful allocations since boot
    unsigned long cumul_bytes;      // total bytes allocated since boot
    unsigned int managed_bytes;     // bytes currently managed by the heap
    unsigned int alloc_objects;     // number of currently allocated objects
    unsigned int alloc_bytes;       // bytes currently allocated (live)
    unsigned int alloc_maxsz;       // largest allocation size ever returned
    unsigned int avail_bytes;       // total bytes currently available for alloc
    unsigned int avail_maxsz;       // largest single allocation currently possible
};

extern char heap_initialized;
void heap_init(void * start, size_t size);

// Initializes the heap allocator. This function must be called before any other
// functions declared in heap.h. The global variable /heap_initialized/, which
// is statically initialized to 0, is set to 1 by heap_init(); it must not be
// modified externally.
//
// The /start/ and /size/ arguments together provide a region of memory from
// which the heap allocator is to satisfy memory allocation requests. The
// /start/ argument must be a pointer to the first byte of the managed region
// and /size/ must be the size (in bytes) of the managed region. The heap
// allocator may be initialized without any memory by supplying (start = NULL
// and size = 0).
//
// If the heap allocator cannot satisfy a memory allocation request using the
// memory available to it, it will call alloc_phys_page() to request additional
// memory. Memory pages acquired in this manner are never returned.
// Any memory granted to the heap allocator via heap_init() must not be accessed
// except through pointers returned by kmalloc() and kcalloc().
//
// On entry heap_init() assumes:
// - Either /start/ is NULL and /size/ is zero, or /start/ points to a region of
//   memory of size at least /size/.
//
// On return heap_init() guarantees:
// - /heap_initialized/ is set to 1.
//
// * This function must be called once at system initialization time.
//
// See also: kmalloc(), kcalloc(), kfree().

extern void copy_heap_stats(struct heap_stats * stat);

// Copies a snapshot of heap allocator statistics into a /heap_stats/ structure
// provided by the caller. The /stat/ argument must point to a properly-aligned
// region of memory large enough to hold an instance of the /heap_stats/
// structure.
//
// On entry copy_heap_stats() assumes:
// - /heap/ is a properly-aligned pointer to a region of memory large enough to
//   hold an instance of a /heap_stats/ structure.
//
// On return copy_heap_stats() guarantees:
// - The /heap_stats/ structure to which /stat/ points contains snapshot of heap
//   allocator statistics.

void * kmalloc(size_t size);

// Allocates /size/ bytes from the heap and returns a pointer to the allocated
// memory.
//
// The returned pointer is aligned to HEAP_ALIGN bytes. All bytes in the
// returned memory region are filled with an implementation-defined value to aid
// the detection and identification of inadvertent use of nominally
// uninitialized memory.
//
// kmalloc() returns a NULL pointer if /size/ is 0.
//
// If kmalloc() cannot satisfy the allocation request, it causes an kernel
// panic.
//
// On entry kmalloc() assumes:
// - /size/ is not greater than HEAP_ALLOC_MAX.
//
// On return kmalloc() guarantees:
// - The returned pointer points to at least /size/ bytes of memory that does
//   not overlap with any other allocated memory. (Note that this statement is
//   true even when /size/ is 0 and kmalloc() returns NULL.)
//
// Performance guarantees:
// - kmalloc() will succeed if there is a contiguous region of unallocated
//   memory of size at least /size/ plus a fixed implementation-defined overhead
//   of no more than 16 bytes.
//
// * This function must _not_ be called from an ISR.
//
// See also: kcalloc(), kfree().

void * kcalloc(size_t nelts, size_t eltsz);

// Allocates an array of /nelts/ elements of size /eltsz/ each and returns a
// pointer to the allocated memory. The returned memory is zero-filled.
//
// kcalloc() returns a NULL pointer if either /nelts/ or /eltsz/ is zero.
//
// On entry kcalloc() assumes:
// - Aggregate allocation request is not greater than HEAP_ALLOC_MAX.
//
// On return kcalloc() guarantees:
// - The returned pointer points to at least the requested amount of memory that
//   does not overlap with any other allocated memory. (Note that this statement
//   is true even when /nelts/ or /eltsz/ are 0 and kcalloc() returns NULL.)
// - The returned memory region is zero-initialized.
//
// See kmalloc() for performance guarantees.
//
// * This function must _not_ be called from an ISR.
//
// See also: kmalloc(), kfree().

void kfree(void * ptr);

// Frees memory previously allocated by kmalloc() or kcalloc().
//
// On entry kfree() assumes:
// - If /ptr/ is not NULL, then it was previously allocated by kmalloc() or
//   kcalloc() and has not already been freed.
//
// Performance guarantees:
// - The returned memory is made available for future allocation requests,
//   subject to fragmentation constraints.
//
// * This function must _not_ be called from an ISR.
//
// See also: kmalloc(), kfree().

#endif  // _HEAP_H_
