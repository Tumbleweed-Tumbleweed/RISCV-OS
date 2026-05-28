/*! @file heap.c
    @brief Simple, non-freeing user heap memory manager
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA
*/

#include "heap.h"
#include "string.h"
#include "syscall.h"

#include <stddef.h>
#include <stdint.h>

#define PAGE_ORDER 12
#define PAGE_SIZE (1UL << PAGE_ORDER)
#define ROUND_UP(n, k) (((n) + (k) - 1) / (k) * (k))
#define ROUND_DOWN(n, k) ((n) / (k) * (k))

// INTERNAL GLOBAL VARIABLES
//

/**
 *  @brief Lowest address of user heap memory
 */
static void * heap_low; // lowest address of heap memory

/**
 *  @brief End of user heap memory
 */
static void * heap_end; // end of heap memory

// EXPORTED GLOBAL VARIABLES
//

char heap_initialized = 0;

// EXPORTED FUNCTION DEFINITIONS
//

void heap_init(void * start, void * end) {
    if (start > end){
        _print("Heap Uninitialized");
        _exit();
    }

    heap_low = start;
    heap_end = end;
    heap_initialized = 1;
}

void * malloc(size_t size) {
    void * ptr;

    if (size == 0)
        return NULL;

    if (size > heap_end - heap_low) {
        _print("Heap Overflow");
        _exit();
    }

    ptr = heap_low;
    heap_low += size;
    return ptr;
}

void * calloc(size_t nelts, size_t eltsz) {
    size_t size;
    void * ptr;
    size =  nelts * eltsz;

    ptr = malloc(size);
    
    // check if malloc allocated any memory
    if (!ptr) return NULL;

    memset(ptr, 0, size);
    return ptr;
}

void free(void * ptr) {
    // DO NOTHING
}

