// cache.c - Block cache for a storage device
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef CACHE_TRACE
#define TRACE
#endif

#ifdef CACHE_DEBUG
#define DEBUG
#endif

#include "cache.h"
#include "conf.h"
#include "device.h"
#include "memory.h"
#include "error.h"
#include "string.h"
#include "thread.h"
#include "heap.h"
#include "misc.h"
#include "io.h"
#include "ioimpl.h"
#include "console.h"

// ---INTERNAL TYPE DEFINITIONS--- //

struct cache {
    struct cache_block * head;          // pointer to the head block position
    struct cache_block * tail;          // pointer to final block position
    struct rwlock ca_lock;              // lock for the cache itself
    struct io * bkgio;                  // pointer to the io backing device
    int size;                           // current length of cache
    unsigned long blksz;                // the number of bytes we can hold in each cache block
};

struct cache_block {
    struct cache_block * next;          // pointer to the next node in the linked list
    struct rwlock ca_blk_lock;          // lock for the individual cache blocks
    uint8_t * data;                     // pointer to data buffer
    uintptr_t data_adress;              // adress where orignally grabbed from memory
    int dirty;                          // 0 when clean, 1 when dirty
    int held;                           // indicates if/how many thread(s) are currently using this block
};

// ---INTERNAL FUNCTION DECLARATIONS--- //

// ---EXPORTED FUNCTION DEFINITIONS--- //

/*
INPUTS: struct io * bkgio - pointer to the io struct for the cache block
        unsigned long cache_blksz - indicates the block size we will use for the cache (in our case will always be 512)
OUTPUTS: returns pointer to newly created cache
DESCRIPTION: initalizes the cache struct
SIDE EFFECTS: new data allocated on the heap; cache is initialized
*/
struct cache * create_cache(struct io * bkgio, unsigned long cache_blksz) {
    assert(bkgio != NULL);
    struct cache * cache = kcalloc(sizeof(struct cache), 1);
    cache->blksz = cache_blksz;
    cache->head = NULL;
    cache->tail = NULL;
    cache->size = 0;
    cache->bkgio = bkgio;
    rwlock_init(&cache->ca_lock, "cache_lock");
    return cache;
}

/*
INPUTS: const struct cache * ca - pointer to the cache struct
OUTPUTS: returns the cache->blksz
DESCRIPTION: relays the block size of the cache
SIDE EFFECTS: no side effects
*/
unsigned int cache_blksz(const struct cache * ca) {
    return ca->blksz;
}

/*
INPUTS: struct cache * cache - pointer to our cache struct
        unsigned long long pos - position, backing device byte offset, cache aligned
        int exclusive - indicates weather we are fetching this block exclusively or not
        void ** pptr - pointer to the pointer where data will be placed
OUTPUTS: returns 0 on success; returns negative error code on failure
DESCRIPTION: fetches data from the cache first, if not in cache fetches from backing device, updates cache, then places it in pptr
SIDE EFFECTS: may initalize and allocated or deallocate a new cache_block to the heap; modifies cache linked list; fetched data placed in pptr
*/
int cache_fetch(struct cache * cache, unsigned long long pos, int exclusive, void ** pptr) {
    assert(cache != NULL);
    assert(pptr != NULL);
    if ( pos % cache->blksz != 0) return -EINVAL;                   // must be 512 alligned block size
    //if (exclusive == 0) return -ENOTSUP;                            // currently not supporting non exclusive fetches

    rwlock_acquire(&cache->ca_lock, 1);
    struct cache_block * ca_blk = cache->head;
    struct cache_block * temp = NULL;

    while (ca_blk != NULL) {                    // we first check our cache to see if the block is in the cache
        if (ca_blk->data_adress == pos) {       // we essentially use a link listed struct to have LRU cache
            if (temp != NULL) {                 // link list removal / move to front
                temp->next = ca_blk->next;
                if (cache->tail == ca_blk) cache->tail= temp;
                ca_blk->next = cache->head;
                cache->head = ca_blk;
            }
            ca_blk->held++;
            *pptr = ca_blk->data;
            rwlock_release(&cache->ca_lock);
            return 0;
        }
        temp = ca_blk;                          // these two lines iterate us through the cache blocks
        ca_blk = ca_blk->next;
    }

    // data we are looking for is not currently in cache
    if (cache->size == CACHE_CAPACITY) {            // if cache full, find next available block to be evicted
        struct cache_block * tenant = NULL;         // tracks the block we will evict
        struct cache_block * neighbor = NULL;       // tracks the block just before the one we evict
        ca_blk = cache->head;
        temp = NULL;

        while (ca_blk != NULL) {
            if (ca_blk->held == 0) {                // will always hold the furthest back evictable tenant
                tenant = ca_blk;
                neighbor = temp;
            }
            temp = ca_blk;
            ca_blk = ca_blk->next;
        }

        if (tenant == NULL) {
            rwlock_release(&cache->ca_lock);
            return -EBUSY;                          // if there is no evictable block throw error
        }

        if (tenant->dirty == 1) {                   // write back to disk if block is dirty
            long stored = iostore(cache->bkgio, tenant->data_adress, tenant->data, cache->blksz);
            if (stored < 0) {                       // if iostore failed pass error code through
                rwlock_release(&cache->ca_lock);
                return stored;
            }
            if (stored != cache->blksz) {           // ensure total number of stored blocks is correct
                rwlock_release(&cache->ca_lock);
                return -EIO;
            }
        }

        // update linked list
        if (neighbor != NULL) neighbor->next = tenant->next;
        else cache->head = tenant->next;

        if (cache->tail == tenant) cache->tail = neighbor;
        kfree(tenant->data);
        kfree(tenant);
        cache->size--;
    }
    
    ca_blk = kcalloc(sizeof(struct cache_block), 1);
    if (ca_blk == NULL) {
        rwlock_release(&cache->ca_lock);
        return -ENOMEM;                                             // throw no mem error if not enough mem to alocate block
    }

    ca_blk->data = kcalloc(cache->blksz, 1);
    if (ca_blk->data == NULL) {                                     // check if theres enough memory for data
        kfree(ca_blk);
        rwlock_release(&cache->ca_lock);
        return -ENOMEM;
    }

    long fetched = iofetch(cache->bkgio, pos, ca_blk->data, cache->blksz);
    if (fetched < 0) {                                              // if iofetch throws an error code free block and pass the error code back up
        kfree(ca_blk->data);
        kfree(ca_blk);
        rwlock_release(&cache->ca_lock);
        return fetched;
    }
    // new cache block intialization
    ca_blk->dirty = 0;
    ca_blk->data_adress = pos;
    rwlock_init(&ca_blk->ca_blk_lock, "ca_blk_lock");               // init lock although right now we dont use the lock for anything
    ca_blk->next = cache->head;
    ca_blk->held = 1;
    cache->head = ca_blk;
    cache->size++;
    if (cache->size == 1) cache->tail = ca_blk;                     // make sure if this is the first block in the cache the tail pointer is set
    *pptr = ca_blk->data;
    rwlock_release(&cache->ca_lock);
    return 0;
}

/*
INPUTS: struct cache * cache - pointer to our cache struct
        void * pblk - pointer to the data we will want to match to released block
        int dirty - indicates if the cache block data has been modified
OUTPUTS: no output
DESCRIPTION: marks the passed block as released from a thread and dirty if changed
SIDE EFFECTS: cache_blocks may be marked dirty or for eviction
*/
void cache_release(struct cache * cache, void * pblk, int dirty) {
    assert(cache != NULL);
    assert(pblk != NULL);

    rwlock_acquire(&cache->ca_lock, 1);
    struct cache_block * ca_blk = cache->head;

    while (ca_blk != NULL) {                    // loops through full cache
        if (ca_blk->data == pblk) {             // release block based on the data
            if (dirty == 1) ca_blk->dirty = 1;
            ca_blk->held--;                     // if held drops to 0 fetch may evict this block.
            rwlock_release(&cache->ca_lock);
            return;
        }
        ca_blk = ca_blk->next;
    }
    rwlock_release(&cache->ca_lock);
}

/*
INPUTS: struct cache * cache - pointer to cache struct
OUTPUTS: returns 0 on success; returns negative error code on failure
DESCRIPTION: writes all dirty block back to memory, cache itself keeps it blocks
SIDE EFFECTS: all block in cache should be clean; data in disk is updated; 
*/
int cache_flush(struct cache * cache) {
    assert(cache != NULL);
    rwlock_acquire(&cache->ca_lock, 1);
    struct cache_block * ca_block = cache->head;
    while (ca_block != NULL) {              // iterrate through all cache block
        if (ca_block->dirty == 1) {
            long stored = iostore(cache->bkgio, ca_block->data_adress, ca_block->data, cache->blksz);               // store dirty blocks
            if (stored < 0) {                                              // if iostore throws an error code pass the error code back up
                rwlock_release(&cache->ca_lock);
                return stored;
            }
            ca_block->dirty = 0;
        }
        ca_block = ca_block->next;
    }
    rwlock_release(&cache->ca_lock);
    return 0;
}

// ---INTERNAL FUNCTION DEFINITIONS--- //
