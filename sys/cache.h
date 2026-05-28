// cache.h - Cache for a storage object
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _CACHE_H_
#define _CACHE_H_

#include "io.h"

struct cache; // opaque decl.

// BLOCK CACHE
//
// The cache module stores fixed-size blocks from a backing `struct io *` and
// serves repeated block accesses from memory when possible.
//
// Some implementations may support shared (nonexclusive) fetches of a cached
// block by multiple readers. This is optional. A cache implementation that
// does not support nonexclusive fetches may reject them by returning
// [-ENOTSUP] from cache_fetch().
//
//
// REQUIRED BEHAVIOR FOR THIS ASSIGNMENT
//
// - Cache size in blocks is exactly CACHE_CAPACITY.
// - The cache is empty immediately after create_cache() returns.
// - If the cache starts empty and callers fetch blocks
//   [N, N+1, N+2, ..., N+CACHE_CAPACITY], subsequent fetches of any of
//   those blocks must be cache hits (no backing-device read request).
// - If block /k/ is fetched and no subsequent cache_fetch() occurs, block /k/
//   must remain in the cache.
//
// EVICTION POLICY
//
// Eviction policy is intentionally unconstrained. On a miss with a full cache,
// any evictable block may be replaced.
//
// CONCURRENCY REQUIREMENTS
//
// The cache must be safe for concurrent use by multiple threads. A cached block
// may be in use by another thread while the current thread is operating.
// Implementations must prevent data races and coherence errors.
//
//
// UNDEFINED BEHAVIORS
//
// In this module, undefined behavior means the specification intentionally does
// not require one specific outcome. Implementations may choose behavior freely
// (for example: blocking, returning an error, panicking, etc.), and tests must
// not assume a particular result.
//
// The following cases are explicitly undefined:
// - A thread recursively calling cache_fetch() on the same block before
//   releasing its previous fetch of that block.
// - cache_fetch() called for a block not currently in cache when the cache is
//   full and all resident blocks are in use (non-evictable).
// - cache_release() called with a pointer that is not currently owned by the
//   calling thread through a successful cache_fetch()
//
// Those are just some example of UB for this module, other edge behaviors can
// be treated similarly (within reason).

extern struct cache * create_cache(
    struct io * bkgio, unsigned long cache_blksz);

// Creates and initializes a block cache over /bkgio/.
//
// On entry create_cache() assumes:
// - /bkgio/ is a valid backing io object.
// - /cache_blksz/ is less than HEAP_ALLOC_MAX
// - /cache_blksz/ is at least /bkgio->blksize/ bytes.
//
// On return create_cache() guarantees:
// - If successful, the returned pointer is a valid `struct cache *`.
// - The cache is empty.
//
// * This function may allocate heap memory.
// * This function may create helper threads.
// * This function must _not_ be called from an ISR.

extern unsigned int cache_blksz(const struct cache * ca);

// Returns the cache block size in bytes.
//
// On entry cache_blksz() assumes:
// - /ca/ is a valid `struct cache *`.
//

extern int cache_fetch(
    struct cache * ca, unsigned long long pos, int exclusive, void ** pptr);

// Fetches the cache block at backing-device byte offset /pos/.
//
// On entry cache_fetch() assumes:
// - /ca/ is a valid `struct cache *`.
// - /pos/ is aligned to cache_blksz(/ca/).
// - /exclusive/ is nonzero for an exclusive fetch and zero for a nonexclusive
//   fetch.
// - /pptr/ is a non-NULL pointer.
//
// On successful return cache_fetch() guarantees:
// - Return value is 0.
// - /pptr/ points to the cached in-memory block for /pos/.
// - The returned block remains in the cache and in-use until the matching
//   cache_release() call for that pointer.
// - If /exclusive/ is nonzero, no other thread is permitted to hold the same
//   block nonexclusively or exclusively at the same time.
// - If /exclusive/ is zero and the implementation supports nonexclusive
//   fetches, other threads may also hold the same block nonexclusively.
//
// On error cache_fetch() returns a negative error code.
// - Implementations that do not support nonexclusive fetches may return
//   [-ENOTSUP] when /exclusive/ is zero.
//
// * This function may block.
// * This function may switch to another thread context.
// * This function must _not_ be called from an ISR.

extern void cache_release(struct cache * ca, void * ptr, int dirty);

// Releases a block previously returned by cache_fetch().
//
// The /ptr/ argument must be a pointer previously returned through /pptr/ by a
// successful cache_fetch(/ca/, ..., ..., /pptr/) call whose block has not yet
// been released.
//
// If /dirty/ is nonzero, this call marks the block modified; the cache must
// eventually write the block back to the backing device before the block is
// discarded or before cache_flush() returns.
//
// On return cache_release() guarantees:
// - The caller no longer owns /ptr/.
// - /ptr/ may become invalid for access after this call.
//
// * This function must _not_ be called from an ISR.

extern int cache_flush(struct cache * ca);

// Flushes all dirty cache blocks to the backing device.
//
// On entry cache_flush() assumes:
// - /ca/ is a valid `struct cache *`.
//
// On successful return cache_flush() guarantees:
// - Return value is 0.
// - All blocks marked dirty that are not owned exclusively by another thread
//   are written back to disk and are no longer marked dirty.
//
// On error cache_flush() returns a negative error code.
//
// * This function may block.
// * This function may switch to another thread context.
// * This function must _not_ be called from an ISR.

#endif // _CACHE_H_
