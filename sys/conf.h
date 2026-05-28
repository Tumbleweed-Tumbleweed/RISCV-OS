// conf.h - Compile-time configuration
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

// KERNEL CONFIGURATION
//

#ifndef UMEM_START_VMA
#define UMEM_START_VMA 0x0C0000000UL
#endif

#ifndef UMEM_END_VMA
#define UMEM_END_VMA 0x100000000UL
#endif

#if UMEM_END_VMA <= UMEM_START_VMA
#error "UMEM_END_VMA <= UMEM_START_VMA"
#endif

#define UMEM_START ((void*)UMEM_START_VMA)
#define UMEM_END ((void*)UMEM_END_VMA)
#define UMEM_SIZE (UMEM_END - UMEM_START)

// Number of external interrupt sources

#ifndef NIRQ
#define NIRQ PLIC_SRC_CNT
#endif

// Maximum number of threads

#ifndef NTHR
#define NTHR 32
#endif

// Heap allocator alignment

#ifndef HEAP_ALIGN
#define HEAP_ALIGN 8
#endif


// Maximum number of open Io objects

#define PROC_IOMAX 16

// Capacity of block cache

#define CACHE_CAPACITY 64  // must be power of two
