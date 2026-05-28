// heap.c - Heap memory manager
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef HEAP_TRACE
#define TRACE
#endif

#ifdef HEAP_DEBUG
#define DEBUG
#endif

#include <stddef.h>
#include <stdint.h>

#include "heap.h"
#include "misc.h"
#include "conf.h"
#include "string.h"
#include "memory.h"

#ifndef HEAP_ALIGN
#define HEAP_ALIGN 8
#endif

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

// INTERNAL CONSTANT DEFINITIONS
//

#define HEAP_GRANT_FILL 0x11 // byte fill for pristine memory
#define HEAP_ALLOC_FILL 0xAF // byte fill for allocated memory
#define HEAP_FREE_FILL  0x67 // byte fill for freed memory


// INTERNAL TYPE DEFINITIONS
//

#define HEAP_PRIST_MULT 3
#define HEAP_ALLOC_MULT 5
#define HEAP_FREE_MULT  7

struct heap_chunk_header {
    unsigned int size;   // allocation size in bytes
    unsigned int chkval; // check value
} __attribute__ ((aligned(HEAP_ALIGN)));

#define HCHSZ sizeof(struct heap_chunk_header)

struct heap_free_chunk {
    struct heap_chunk_header hdr;
    struct heap_free_chunk * next;
};

#define HFCSZ sizeof(struct heap_free_chunk)


// INTERNAL FUNCTION DECLARATIONS
//

static void * heap_alloc(size_t size, void * call_ra);
static void heap_free(void * ptr, void * call_ra);

static struct heap_free_chunk * make_chunk(void * start, size_t size);

static struct heap_free_chunk ** find_best_fit_chunk (
    struct heap_free_chunk ** cptr, uint32_t size) __attribute__ ((unused));

static struct heap_free_chunk ** find_first_fit_chunk (
    struct heap_free_chunk ** cptr, uint32_t size) __attribute__ ((unused));

static struct heap_free_chunk * sort_chunks(struct heap_free_chunk * list);

static struct heap_free_chunk * coalesce_chunks (
    struct heap_free_chunk * colist, struct heap_free_chunk * uclist);

static uint32_t calc_prist_chkval(const struct heap_chunk_header * hdr);
static uint32_t calc_alloc_chkval(const struct heap_chunk_header * hdr);
static uint32_t calc_free_chkval(const struct heap_chunk_header * hdr);

// INTERNAL VARIABLE DEFINITIONS
//

static struct heap_free_chunk * sorted_free_chunks;
static struct heap_free_chunk * unsorted_free_chunks;

// Heap statistics

static uint32_t heap_managed_bytes;
static uint32_t heap_alloc_objects;
static uint32_t heap_alloc_bytes;
static uint32_t heap_alloc_maxsz;
static uint64_t heap_cumul_objects;
static uint64_t heap_cumul_bytes;


// EXPORTED VARIABLE DEFINITIONS
//

char heap_initialized = 0;


// EXPORTED FUNCTION DEFINITIONS
//

void heap_init(void * start, size_t size) {
    unsigned int start_offset;
    
    trace("%s(%p,%zu)", __func__, start, size);

    if (size == 0) {
        debug("Heap initialized without initial grant");
        goto heap_init_success;
    }
    
    // Internally we use 32-bit uints, so heap can't be too large

    if (UINT32_MAX < size)
        size = UINT32_MAX;
    
    if (size != 0 && size < HFCSZ + 2*HEAP_ALIGN) {
        debug("Intial heap grant too small");
        start = NULL;
        size = 0;
    }

    // Align start and size to HEAP_ALIGN

    start_offset = ROUND_UP((uintptr_t)start, HEAP_ALIGN) - (uintptr_t)start;
    start += start_offset;
    size -= start_offset;
    
    size = ROUND_DOWN(size, HEAP_ALIGN);

    // Initial memory grant should be at least sizeof(struct heap_list_link).

    if (size == 0) {
        debug("Heap initialized without initial grant");
        goto heap_init_success;
    }

    // Initialize pristine chunk (first choice for all allocations)

    sorted_free_chunks = make_chunk(start, size);
    heap_managed_bytes = size;

heap_init_success:
    heap_initialized = 1;
}

void copy_heap_stats(struct heap_stats * stats) {
    uint32_t avail_bytes = 0;
    uint32_t avail_maxsz = 0;
    struct heap_free_chunk * chunk;

    // Coalesce all free chunks so that avail_maxsz accurately reflects maximum
    // allocation request we can service without asking for more memory

    sorted_free_chunks = coalesce_chunks (
        sorted_free_chunks, sort_chunks(unsorted_free_chunks));
    unsorted_free_chunks = NULL;

    for (chunk = sorted_free_chunks; chunk != NULL; chunk = chunk->next) {
        avail_maxsz = MAX(avail_maxsz, chunk->hdr.size);
        avail_bytes += chunk->hdr.size;
    }

    stats->cumul_objects = heap_cumul_objects;
    stats->cumul_bytes = heap_cumul_bytes;
    stats->managed_bytes = heap_managed_bytes;
    stats->alloc_objects = heap_alloc_objects;
    stats->alloc_bytes = heap_alloc_bytes;
    stats->alloc_maxsz = heap_alloc_maxsz;
    stats->avail_bytes = avail_bytes;
    stats->avail_maxsz = avail_maxsz;
}

void * kmalloc(size_t size) {
    void * ptr;

    trace("%s(%zu)", __func__, size);

    if (size == 0) {
        debug("Zero-sized kmalloc() request");
        return NULL;
    }

    if (HEAP_ALLOC_MAX < size)
        panic("kmalloc() request too large");

    size = ROUND_UP(size, HEAP_ALIGN);

    ptr = heap_alloc(size, __builtin_return_address(0));
    memset(ptr, HEAP_ALLOC_FILL, size);
    return ptr;
}

void * kcalloc(size_t nelts, size_t eltsz) {
    size_t size;
    void * ptr;

    trace("%s(%zu,%zu)", __func__, nelts, eltsz);

    if (nelts == 0 || eltsz == 0) {
        debug("Zero-sized kcalloc() request");
        return NULL;
    }

    if (SIZE_MAX / nelts < eltsz)
        panic("kcalloc() request too large");
    
    size = nelts * eltsz;

    if (HEAP_ALLOC_MAX < size)
        panic("kcalloc() request too large");
    
    size = ROUND_UP(size, HEAP_ALIGN);

    ptr = heap_alloc(size, __builtin_return_address(0));
    memset(ptr, 0, size);
    return ptr;
}

void kfree(void * ptr) {
    trace("%s(%p)", __func__, ptr);
    heap_free(ptr, __builtin_return_address(0));
}


// INTERNAL FUNCTION DEFINITIONS
//

void * __attribute__ ((weak)) alloc_phys_page(void) {
    // If the system does not have a memory manager (memory.c), the heap_alloc()
    // function below will call this weak definition instead.

    struct heap_stats stats;
    copy_heap_stats(&stats);

    debug("cumul_objects = %lu", stats.cumul_objects);
    debug("cumul_bytes = %lu", stats.cumul_bytes);
    debug("managed_bytes = %u", stats.managed_bytes);
    debug("alloc_objects = %u", stats.alloc_objects);
    debug("alloc_bytes = %u", stats.alloc_bytes);
    debug("alloc_maxsz = %u", stats.alloc_maxsz);
    debug("avail_bytes = %u", stats.avail_bytes);
    debug("avail_maxsz = %u", stats.avail_maxsz);
    panic("Out of memory");
}

void * heap_alloc(size_t size, void * call_ra) {
    struct heap_free_chunk ** cptr;
    struct heap_free_chunk * chunk;
    struct heap_chunk_header * ahdr;
    void * ptr;

    trace("%s(%zu,%p)", __func__, size, call_ra);
    (void)call_ra; // inhibit unused arg warning

    assert (size <= HEAP_ALLOC_MAX);
    assert (size % HEAP_ALIGN == 0);

    // Find best fit chunk in sorted free chunk list. If we don't find one, try
    // to coalesce sorted and unsorted chunk lists and try again.

    cptr = find_best_fit_chunk(&sorted_free_chunks, size);

    if (cptr == NULL && unsorted_free_chunks != NULL) {
        sorted_free_chunks = coalesce_chunks (
            sorted_free_chunks, sort_chunks(unsorted_free_chunks));
        unsorted_free_chunks = NULL;

        cptr = find_best_fit_chunk(&sorted_free_chunks, size);
    }

    // If we still haven't found a chunk large enough for the request, ask for
    // more memory from the memory manager. There is a weak-linked definition of
    // alloc_phys_page() in this file in case there is no memory manager.

    if (cptr == NULL) {
        assert (unsorted_free_chunks == NULL);
        unsorted_free_chunks = make_chunk(alloc_phys_page(), PAGE_SIZE);
        heap_managed_bytes += PAGE_SIZE;
        cptr = &unsorted_free_chunks;
    }

    // At this point /cptr/ points to a pointer to a chunk that is large enough
    // to satisfy the allocation request.

    chunk = *cptr;
    assert (chunk != NULL);
    assert (size <= chunk->hdr.size);

    // If the chunk is too small to split, use all of it. Otherwise, split a
    // piece off the chunk at the end.

    if (chunk->hdr.size - size < HFCSZ) {
        *cptr = chunk->next;
        ahdr = &chunk->hdr;
    } else {
        ahdr = (void*)(&chunk->hdr+1) + chunk->hdr.size - size - HCHSZ;
        chunk->hdr.size -= size + HCHSZ;
        ahdr->size = size;
    }

    ptr = ahdr + 1; // next address after chunk header
    ahdr->chkval = calc_alloc_chkval(ahdr);
    
    debug("Allocated %zu bytes at %p", ahdr->size, ptr);

    heap_cumul_bytes += ahdr->size;
    heap_alloc_bytes += ahdr->size;
    heap_cumul_objects += 1;
    heap_alloc_objects += 1;

    if (heap_alloc_maxsz < ahdr->size)
        heap_alloc_maxsz = ahdr->size;

    return ptr;
}

void heap_free(void * ptr, void * call_ra) {
    struct heap_chunk_header * hdr;
    struct heap_free_chunk * chunk;

    trace("%s(%p,%p)", __func__, ptr, call_ra);
    (void)call_ra; // inhibit unused arg warning

    if (ptr == NULL)
        return;

    if ((uintptr_t)ptr % HEAP_ALIGN != 0)
        panic("kfree() called with invalid pointer");
    
    // Validate header

    hdr = (struct heap_chunk_header *)ptr - 1;

    if (hdr->size % HEAP_ALIGN != 0)
        panic("kfree() called with invalid pointer");    

    if (hdr->chkval != calc_alloc_chkval(hdr)) {
        if (hdr->chkval != calc_free_chkval(hdr))
            panic("kfree() called with previously freed pointer");
        else
            panic("kfree() called with invalid pointer");
    }

    chunk = (struct heap_free_chunk*)hdr;
    hdr->chkval = calc_free_chkval(hdr);
    memset(ptr, HEAP_FREE_FILL, hdr->size);
    chunk->next = unsorted_free_chunks;
    unsorted_free_chunks = chunk;

    heap_alloc_bytes -= hdr->size;
    heap_alloc_objects -= 1;
}

struct heap_free_chunk * make_chunk(void * start, size_t size) {
    struct heap_free_chunk * chunk;

    chunk = start;
    assert (HFCSZ <= size);
    chunk->hdr.size = size - HCHSZ;
    chunk->hdr.chkval = calc_prist_chkval(&chunk->hdr);
    chunk->next = NULL;

    memset(chunk+1, HEAP_GRANT_FILL, size - HFCSZ);
    return chunk;
}

struct heap_free_chunk ** find_best_fit_chunk (
    struct heap_free_chunk ** cptr, uint32_t size)
{
    struct heap_free_chunk ** best_cptr = NULL;
    uint32_t best_size = UINT32_MAX;
    int best_pristine = 0;
    struct heap_free_chunk * chunk;

    trace("%s(%p,%u)", __func__, cptr, size);

    while ((chunk = *cptr) != NULL) {
        if (size <= chunk->hdr.size) {
            int const p = (chunk->hdr.chkval == calc_prist_chkval(&chunk->hdr));
            int const s = (chunk->hdr.size < best_size);
            if ((!best_pristine && s) || (!best_pristine && p) || (p && s)) {
                best_size = chunk->hdr.size;
                best_cptr = cptr;
                best_pristine = p;
            }
        }

        cptr = &chunk->next;
    }
    
    return best_cptr;
}

struct heap_free_chunk ** find_first_fit_chunk (
    struct heap_free_chunk ** cptr, uint32_t size)
{
    struct heap_free_chunk * chunk;

    trace("%s(%p,%u)", __func__, cptr, size);

    while ((chunk = *cptr) != NULL) {
        if (size <= chunk->hdr.size)
            return cptr;

        cptr = &chunk->next;
    }

    return NULL;
}

struct heap_free_chunk * sort_chunks(struct heap_free_chunk * list) {
    // Sort the chunk addresses in increasing order using a radix-16 sort,
    // starting from the least significant digit. Since all addresses are aligned,
    // we divide the address by the alignment first.

    // We can terminate the sort early once the remaining most significant bits
    // are all the same. To identify when that happens, we keep track of the
    // cumulative bitwise-and and bitwise-or of the remaining sort bits in
    // /acc0/ and /acc1/. If (acc0 == acc1), we know we are done.

    struct heap_free_chunk * heads[16]; // heads of mod 16 bin lists
    struct heap_free_chunk * tails[16]; // tails of mod 16 bin lists
    struct heap_free_chunk * chunk;
    struct heap_free_chunk * tail;
    uintptr_t key, acc0, acc1;
    unsigned int k; // bin index

    trace("%s(%p)", __func__, list);

    if (list == NULL)
        return NULL;

    for (int i = 0; i < 8*sizeof(void*)/4; i++) {
        memset(heads, 0, sizeof(heads));
        memset(tails, 0, sizeof(tails));
        acc0 = -1, acc1 = 0;

        for (chunk = list; chunk != NULL; chunk = chunk->next) {
            key = ((uintptr_t)chunk / HEAP_ALIGN) >> (4*i);
            acc0 &= key >> 4;
            acc1 |= key >> 4;
            k = key & 0xF;
        
            if (heads[k] != NULL)
                tails[k]->next = chunk;
            else
                heads[k] = chunk;
            tails[k] = chunk;
        }

        // Combine bins into a single list starting with bin 0. Some bins may be
        // empty, so we need to hadle this case. (However, there will be at
        // least one non-empty bin.)

        k = 0;
        while (k < 15 && heads[k] == NULL)
            k += 1;
        
        list = heads[k];
        tail = tails[k];
        k += 1;

        while (k < 16) {
            if (heads[k] != NULL) {
                tail->next = heads[k];
                tail = tails[k];
            }

            k += 1;
        }

        tail->next = NULL;

        // If the remaining address bits are the same, we can stop.

        if (acc0 == acc1)
            break;
    }

    return list;
}

// The coalesce_chunks() function coalesces adjacent free chunks. The arguments
// are two lists, both sorted in increasing order. The first list must be
// maximally self-coalesced, while the second need not be. The intended use case
// is to call coalesce_chunks() with /sorted_free_chunks/, which is always
// maximally self-coalesced, and /unsorted_free_chunks/ after it has been
// sorted.

struct heap_free_chunk * coalesce_chunks (
    struct heap_free_chunk * colist, // sorted coalesced chunk link
    struct heap_free_chunk * uclist // sorted uncoalesced chunk list
) {
    struct heap_free_chunk * chunk;
    struct heap_free_chunk * next;
    struct heap_free_chunk * head;
    void * chunk_end;
    
    trace("%s(%p,%p)", __func__, colist, uclist);

    if (uclist == NULL)
        return colist;

    if (colist == NULL || uclist < colist) {
        chunk = uclist;
        uclist = chunk->next;
    } else {
        chunk = colist;
        colist = chunk->next;
    }

    // /chunk/ is a pointer to the tail element of the new list of coalesced
    // chunks. In each iteration, we let /next/ be the next chunk in address
    // order (after /chunk/), which will be the head of either /colist/ or
    // /uclist/. If /chunk/ and /next/ can be coalesced, we do so. Otherwise,
    // /next/ is added at the tail of the list (to which /chunk/ points) and
    // becomes the new /chunk/ element.

    head = chunk;

    while (uclist != NULL) {
        // Get chunk from /colist/ or /uclist/ with lowest address

        if (colist == NULL || uclist < colist) {
            next = uclist;
            uclist = next->next;
        } else {
            next = colist;
            colist = next->next;
        }

        // Calculate first address past the end of the current chunk

        chunk_end = (void*)chunk + HCHSZ + chunk->hdr.size;
        assert (chunk_end <= (void*)next);

        // chunk_end points to where the next chunk in address order should
        // begin, and /next/ is the next free chunk in address order. If the two
        // coincide, we can coalesce them.

        if (chunk_end != next) {
            chunk->next = next;
            chunk = next;
        } else
            chunk->hdr.size += HCHSZ + next->hdr.size;
    }

    // At this point, the /uclist/ is empty. If /colist/ is not empty, it is
    // possible that its first element can be coalesced with /chunk/. The
    // remainder of the coalesced list can be appended to the current list
    // without scanning through it.

    chunk_end = (void*)chunk + HCHSZ + chunk->hdr.size;

    if (chunk_end == colist) {
        chunk->hdr.size += HCHSZ + colist->hdr.size;
        chunk->next = colist->next;
    } else
        chunk->next = colist;
    
    return head;
}

uint32_t calc_prist_chkval(const struct heap_chunk_header * hdr) {
    return (uint32_t)(uintptr_t)hdr / HEAP_ALIGN * HEAP_PRIST_MULT;
}

uint32_t calc_alloc_chkval(const struct heap_chunk_header * hdr) {
    return (uint32_t)(uintptr_t)hdr / HEAP_ALIGN * HEAP_ALLOC_MULT;
}

uint32_t calc_free_chkval(const struct heap_chunk_header * hdr) {
    return (uint32_t)(uintptr_t)hdr / HEAP_ALIGN * HEAP_FREE_MULT;
}

