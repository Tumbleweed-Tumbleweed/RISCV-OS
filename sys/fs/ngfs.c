// ngfs.c - A FAT-like file system
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef NGFS_TRACE
#define TRACE
#endif

#ifdef NGFS_DEBUG
#define DEBUG
#endif

#include "ngfs.h"

#include "../cache.h"
#include "../device.h"
#include "../error.h"
#include "../filesys.h"
#include "../fsimpl.h"
#include "../heap.h"
#include "../misc.h"
#include "../string.h"
#include "../ioimpl.h"
#include "../thread.h"

// ---INTERNAL TYPE DEFINITIONS--- //
struct ngfs {
    struct filesystem base;                     // file system base
    struct cache * cache;                       // pointer to the cache
    struct io * bkgio;                          // pointer to the backing device io
    struct rwlock ngfs_lock;                    // lock for our ngfs struct
    uint32_t all_blocks;                        // total number of blocks
    uint32_t fat_blocks;                        // total number of FAT blocks
    uint32_t data_blocks;                       // total number of data blocks
};

struct ngfs_fileio {
    struct seekio io;                           // seekio interface
    struct ngfs * ngfs;                         // pointer to ngfs struct
    struct rwlock fileio_lock;                  // lock for read and write concurrency
    uint32_t FAT_start_block;                   // marks the first block in the FAT
    uint32_t size;                              // holds file size in bytes
    uint32_t dir_blk;                           // directory block holding this file's entry
    int entry_idx;                              // index of the entry within that dir block
};

struct ngfs_lsio {
    struct io io;                               // io interface
    struct ngfs * ngfs;                         // pointer to ngfs struct
    uint32_t dir_blk;                           // indicates the current dirrectory block
    int entry_idx;                              // indicates the current dirrectory entry 
};

// ---INTERNAL FUNCTION DECLARATIONS--- //
int ngfs_open(struct filesystem * fs, const char * name, struct io ** ioptr);
void ngfs_reclaim(struct io * io);
long ngfs_fetch(struct io * io, unsigned long long pos, void * buf, long bufsz);
long ngfs_read(struct io * io, void * buf, long bufsz);
long ngfs_store(struct io * io, unsigned long long pos, const void * buf, long len);
long ngfs_write(struct io * io, const void * buf, long len);
int ngfs_ioctl(struct io * io, int op, void * arg);
int ngfs_create(struct filesystem * fs, const char * name);
int ngfs_delete(struct filesystem * fs, const char * name);
void ngfs_flush(struct filesystem * fs);
long ngfs_listing_read(struct io * lsio, void * buf, long bufsz);
void ngfs_listing_reclaim(struct io * lsio);

uint32_t get_next_data_block(struct ngfs * ngfs, uint32_t cur_block);
void set_next_data_block(struct ngfs * ngfs, uint32_t cur_block, uint32_t next_block);
uint32_t get_free_data_block(struct ngfs * ngfs);

// ---INTERNAL GLOBAL VARIABLES--- //
static const struct iointf ngfs_block_intf = {
    .implname = "ngfs_fileio",
    .reclaim = &ngfs_reclaim,
    .read = &ngfs_read,
    .fetch = &ngfs_fetch,
    .write = &ngfs_write,
    .store = &ngfs_store,
    .ioctl = &ngfs_ioctl
};

static const struct iointf ngfs_lsio_intf = {
    .implname = "ngfs_lsio",
    .reclaim = &ngfs_listing_reclaim,
    .read = &ngfs_listing_read
};

static const struct filesystem ngfs_intf = {
    .implname = "ngfs",
    .openfile = &ngfs_open,
    .createfile = &ngfs_create,
    .deletefile = &ngfs_delete,
    .flush = &ngfs_flush
};

// ---EXPORTED FUNCTION DEFINITIONS--- //

/*
INPUTS: const char * name - NULL terminated char pointer to a name for the filesystem
        struct io * bkgio - pointer to the backing device io
OUTPUTS: returns 0 on success; returns negative error code on failure
DESCRIPTION: initalizes NGFS variables, creates cache, registers filesytem 
SIDE EFFECTS: allocates memory
*/
int mount_ngfs(const char * name, struct io * bkgio) {
    struct ngfs * ngfs = kcalloc(sizeof(struct ngfs), 1);
    if (ngfs == NULL) return -ENOMEM;
    ngfs->bkgio = bkgio;                                // set backing device pointer
    ngfs->cache = create_cache(bkgio, NGFS_BLKSZ);      // create ngfs cache

    if (ngfs->cache == NULL) {
        kfree(ngfs);
        return -ENOMEM;
    }

    unsigned long long block_capacity;                  // determine how much space we have
    int valid = ioctl(bkgio, IOC_GETEND, &block_capacity);
    if (valid != 0) {                                   // if ioctl fails return the error code
        kfree(ngfs);
        return valid;
    }
    rwlock_init(&ngfs->ngfs_lock, "ngfs_lock");
    // calculate and set all block sizes and capcities
    ngfs->all_blocks = block_capacity / NGFS_BLKSZ;
    ngfs->fat_blocks = (ngfs->all_blocks + NGFS_FAT_ENTRIES_PER_BLOCK - 1) / NGFS_FAT_ENTRIES_PER_BLOCK;
    ngfs->data_blocks = ngfs->all_blocks - ngfs->fat_blocks;

    if (ngfs->data_blocks == 0) {
        kfree(ngfs);
        return -EINVAL;
    }

    // determine if we already intiazlied the root_dir
    void *blkptr;
    int fetched = cache_fetch(ngfs->cache, NGFS_ROOT_DATA_BLOCK * NGFS_BLKSZ, 1, &blkptr);
    if (fetched < 0) {
        kfree(ngfs);
        return fetched;
    }
    struct ngfs_fat * fatty = (struct ngfs_fat *)blkptr;
    int dir_init = fatty->fat[NGFS_ROOT_DATA_BLOCK];
    cache_release(ngfs->cache, blkptr, 0);

    if (dir_init == 0) {                                                        // if root dir is 0 then it has yet to be intialized
        // intialize the root directory
        set_next_data_block(ngfs, NGFS_ROOT_DATA_BLOCK, NGFS_BLOCK_END);        // updating FAT
        void *blkptr;                                                           // fetch the first data block, F + 0
        int fetched = cache_fetch(ngfs->cache, (ngfs->fat_blocks + NGFS_ROOT_DATA_BLOCK) * NGFS_BLKSZ, 1, &blkptr);     
        if (fetched < 0) {
            kfree(ngfs);
            return fetched;
        }
        memset(blkptr, 0, NGFS_BLKSZ);                                          // zero out the block
        struct ngfs_dir_entry * root_dir = (struct ngfs_dir_entry *)blkptr;
        strncpy(root_dir[0].name, ".", NGFS_MAX_FILENAME_LEN);
        root_dir[0].start_block = NGFS_ROOT_DATA_BLOCK;                         // F + 0 
        root_dir[0].size = sizeof(struct ngfs_dir_entry);
        cache_release(ngfs->cache, blkptr, 1);
    }

    // register the filesytem using passed name
    ngfs->base = ngfs_intf;
    valid = mount_filesys(name, &ngfs->base);
    if (valid != 0) {                                                           // if ioctl fails return the error code
        kfree(ngfs);
        return valid;
    }
    return 0;
}

// ---INTERNAL FUNCTION DEFINITIONS--- //

/*
INPUTS: struct filesystem * fs - points to the ngfs file system
        const char * name - pointer to the name of file we are opening
        struct io ** ioptr - pointer to the pointer for the opened object 
OUTPUTS: returns 0 on success; returns negative error code on failure
DESCRIPTION: opens a file if it exists within the file system
SIDE EFFECTS: ioptr object is set to valid io object on success, allocates memory on success
*/
int ngfs_open(struct filesystem * fs, const char * name, struct io ** ioptr) {
    assert(fs != NULL && ioptr != NULL);
    struct ngfs * const ngfs = (struct ngfs *)fs;

    rwlock_acquire(&ngfs->ngfs_lock, 1);
    if (name != NULL && *name != '\0') {                            // when file has a name we create a ngfs_fileio object
        if (strcmp(name, ".") == 0) {                               // can not open root dir!
            rwlock_release(&ngfs->ngfs_lock);
            return -EACCESS;
        }
        uint32_t file_start_block = 0;
        uint32_t file_size = 0;
        uint32_t found_dir_blk = 0;
        int found_entry_idx = 0;
        int found = 0;                                              // FLAG, 0 until it's found then becomes 1

        uint32_t cur_blk = NGFS_ROOT_DATA_BLOCK;                    // start our scan at block 0
        const int entries_per_block = NGFS_BLKSZ / NGFS_DENSZ;      // directories per data block

        while (!found && cur_blk != NGFS_BLOCK_END) {               // walk through root directory checking for file and next empty index????
            void * blkptr;
            int result = cache_fetch(ngfs->cache, (ngfs->fat_blocks + cur_blk) * NGFS_BLKSZ, 1, &blkptr);
            if (result < 0) {                          // if cache fetch fails, immediately return the error
                rwlock_release(&ngfs->ngfs_lock);
                return result;
            }
            struct ngfs_dir_entry * dents = (struct ngfs_dir_entry *)blkptr;
            for (int i = 0; i < entries_per_block; i++) {           // looping through all entries to find if file exists
                if (dents[i].name[0] == '\0') {                     // directory is continous, if we reach empty file we have checked all file
                    cache_release(ngfs->cache, blkptr, 0);
                    rwlock_release(&ngfs->ngfs_lock);
                    return -ENOENT;
                }
                else if (strcmp(name, dents[i].name) == 0) {        // if passed name matches dentry, found file to be opened
                    file_start_block = dents[i].start_block;
                    file_size = dents[i].size;
                    found_dir_blk = cur_blk;
                    found_entry_idx = i;
                    found = 1;
                    break;
                }
            }
            cache_release(ngfs->cache, blkptr, 0);
            if (found == 1) break;

            cur_blk = get_next_data_block(ngfs, cur_blk);           // iterate through all directory blocks
        }
        if (found != 1) {
            rwlock_release(&ngfs->ngfs_lock);
            return -ENOENT;
        }

        // initalize ngfs_fileio for new file
        struct ngfs_fileio * ngfs_io = kmalloc(sizeof(*ngfs_io));
        if (ngfs_io == NULL){
            rwlock_release(&ngfs->ngfs_lock);
            return -ENOMEM;
        }
        ngfs_io->ngfs = ngfs;
        ngfs_io->FAT_start_block = file_start_block;
        ngfs_io->size = file_size;
        ngfs_io->dir_blk = found_dir_blk;
        ngfs_io->entry_idx = found_entry_idx;
        rwlock_init(&ngfs_io->fileio_lock, "ngfs_fileio_lock");
        *ioptr = seekio_init(&ngfs_io->io, &ngfs_block_intf, 1, 1);  // @TODO what is blksz supposed to be???????
        rwlock_release(&ngfs->ngfs_lock);
        return 0;

    } else {                                                         // if no name is passed we creating a ngfs_lsio object
        struct ngfs_lsio * lsio = kcalloc(1, sizeof(*lsio));
        if (lsio == NULL) {
            rwlock_release(&ngfs->ngfs_lock);
            return -ENOMEM;
        }
        lsio->ngfs = ngfs;
        lsio->dir_blk = NGFS_ROOT_DATA_BLOCK;
        lsio->entry_idx = 1;                                         // @TODO do we start a directory or first block past directory (0 or 1)
        *ioptr = ioinit(&lsio->io, &ngfs_lsio_intf, 1, 1);           // @TODO what is lsio blksz supposed to be???????
        rwlock_release(&ngfs->ngfs_lock);
        return 0;
    }
}

/*
INPUTS: struct io * io - pointer to the io object
OUTPUTS: no outputs
DESCRIPTION: reclaims resources and closes the io object
SIDE EFFECTS: passed io object is closed and its memory is freed
*/
void ngfs_reclaim(struct io * io) {
    struct ngfs_fileio * ngfs_io = (void*)io - offsetof(struct ngfs_fileio, io);
    kfree(ngfs_io);
}

/*
INPUTS: struct io * io - pointer to the io objects
        unsigned long long pos - data position where we will start fetch from
        void * buf - pointer to buffer where bytes will be fetched to
        long bufsz - size of buffer in bytes
OUTPUTS: returns the number of bytes fetched on success; negative error code on failure
DESCRIPTION: fetches bufsz data from the given pos in data to the buffer
SIDE EFFECTS: no side effects
*/
long ngfs_fetch(struct io * io, unsigned long long pos, void * buf, long bufsz) {
    struct ngfs_fileio * const fio = (void*)io - offsetof(struct ngfs_fileio, io);
    struct ngfs * const ngfs = fio->ngfs;

    if (bufsz == 0 || buf == NULL) return 0;                // if buf is empty or length is zero we have nothing to fetch

    rwlock_acquire(&fio->fileio_lock, 0);                   // shared lock to allow for multiple blocks to fetch at the same time
     
    if (fio->size == 0 || fio->size == pos) {               // file size 0 or pos means we have nowhere to fetch
        rwlock_release(&fio->fileio_lock);
        return 0;
    }

    if (pos > fio->size){                                   // if we fetch past the end of file return error
        rwlock_release(&fio->fileio_lock);
        return -EINVAL;
    }
      
    if (pos + bufsz > fio->size)bufsz = fio->size - pos;    // if we attempt to fetch past file size concat to max fetchable

    uint32_t cur_blk = fio->FAT_start_block;
    unsigned long long blk_idx = pos / NGFS_BLKSZ;          // data block we we begin fetch from
    unsigned long off = pos % NGFS_BLKSZ;                   // offset within data block we will begin fetch from

    for (unsigned long long i = 0; i < blk_idx; i++) {      // set cur_blk to correct block
        cur_blk = get_next_data_block(ngfs, cur_blk);                                                                                                     
        if (cur_blk == NGFS_BLOCK_END) {                    // pos within file size but chain ended — corrupted image                                                       
            rwlock_release(&fio->fileio_lock);                                                                                                             
            return -EIO;
          }                                                                                                                                                  
      }

      uint8_t * bufp = buf;                                 // buf pointer, will track what in the data buffer we have fetched already
      long remaining = bufsz;                               // remaining number of bytes left to fetch

    while (remaining > 0) {                                 // fetch data block by block
        void * blk;
        int result = cache_fetch(ngfs->cache, (ngfs->fat_blocks + cur_blk) * NGFS_BLKSZ, 1, &blk);
        if (result < 0) {
            rwlock_release(&fio->fileio_lock);
            return result;
        }

        long cpycnt = NGFS_BLKSZ - off;                     // how much data we can fetch from the current block
        if (cpycnt > remaining) cpycnt = remaining;         // if copycount can hold all of the remaining data the have it hold all the data

        memcpy(bufp, (uint8_t *)blk + off, cpycnt);         // fetch bytes
        cache_release(ngfs->cache, blk, 0);

        bufp += cpycnt;                                     // buf pointer, will track what in the data buffer we have fetched already
        remaining -= cpycnt;                                // remaining number of bytes left to fetch
        off = 0;  

        if (remaining > 0) {                                // if we are out of space on current block grab next block in file
            cur_blk = get_next_data_block(ngfs, cur_blk);
            if (cur_blk == NGFS_BLOCK_END) {
                rwlock_release(&fio->fileio_lock);
                return -EIO;                                // if we have reached final block and we ant store anymore return error
            }
        }
    }

    rwlock_release(&fio->fileio_lock);                      // @TODO ??? ---x--- do we do locks based on each fileio or based on the entire ngfs lock
    return (long)(bufp - (uint8_t *)buf);                   // return number of bytes fetched (ending buf pos - starting buf pos)
}                

/*
INPUTS: struct io * io - pointer to the io object
        void * buf - pointer to buffer where bytes will be read to
        long bufsz - size of buffer in bytes
OUTPUTS: returns the number of bytes read on success; negative error code on failure
DESCRIPTION: reads data from the io internal position into buf
SIDE EFFECTS: no side effects
*/
long ngfs_read(struct io * io, void * buf, long bufsz) {
    return seekio_read(io, buf, bufsz);
}

/*
INPUTS: struct io * io - pointer to the io object
        unsigned long long pos - data position where we will start storing at
        const void * buf - pointer to buffer where bytes will be stored from
        long len - length of data to store in bytes
OUTPUTS: returns the number of bytes written on success; negative error code on failure
DESCRIPTION: stores data from the buffer of len into the data block and position indicated by pos
SIDE EFFECTS: datablocks may be updated
*/
long ngfs_store(struct io * io, unsigned long long pos, const void * buf, long len) {
    struct ngfs_fileio * const fio = (void*)io - offsetof(struct ngfs_fileio, io);
    struct ngfs * const ngfs = fio->ngfs;

    if (len == 0 || buf == NULL) return 0;                  // if buf is empty or length is zero we have nothing to store

    rwlock_acquire(&fio->fileio_lock, 1);                   // must have exclusive lock to prevent race conditions

    if (fio->size == 0) {                                   // file size 0 means we have nowhere to store to
        rwlock_release(&fio->fileio_lock);
        return 0;
    }
    
    if (pos > fio->size) {                                  // if we store past the end of file return error
        rwlock_release(&fio->fileio_lock);
        return -EINVAL;
    }

    if (pos + len > fio->size) len = fio->size - pos;       // if we will store past file size concatinate to store as much as possible
      
    uint32_t cur_blk = fio->FAT_start_block;
    unsigned long long blk_idx = pos / NGFS_BLKSZ;          // data block we we begin store at
    unsigned long off = pos % NGFS_BLKSZ;                   // offset within data block we will begin store at

    for (unsigned long long i = 0; i < blk_idx; i++) {      // set cur_blk to correct block
        cur_blk = get_next_data_block(ngfs, cur_blk);
        if (cur_blk == NGFS_BLOCK_END) {
            rwlock_release(&fio->fileio_lock);
            return -EIO;
        }
    }

    const uint8_t * bufp = buf;                             // buf pointer, will track what in the data buffer we have stored already
    long remaining = len;                                   // remaining number of bytes left to store

    while (remaining > 0) {                                 // store one block at a time until nothing left 
        void * blk;
        int result = cache_fetch(ngfs->cache, (ngfs->fat_blocks + cur_blk) * NGFS_BLKSZ, 1, &blk); 
        if (result < 0) {
            rwlock_release(&fio->fileio_lock);
            return result;
        }

        long cpycnt = NGFS_BLKSZ - off;                     // how much data we can store into the current block
        if (cpycnt > remaining) cpycnt = remaining;         // if copycount can hold all of the remaining data the have it hold all the data

        memcpy((uint8_t *)blk + off, bufp, cpycnt);
        cache_release(ngfs->cache, blk, 1);                 // mark block as dirty when we release so we properly store back to backing device

        bufp += cpycnt;                                     // update bufp to the next index we have yet to store
        remaining -= cpycnt;                                // subtract stored data from remaining
        off = 0;

        if (remaining > 0) {                                // if we are out of space on current block grab next block in file 
            cur_blk = get_next_data_block(ngfs, cur_blk);
            if (cur_blk == NGFS_BLOCK_END) {                // @TODO --x-- ??? would this be correct? we stored data already and now we jsut return error? what happens to what is written? should we allocate new block?
                rwlock_release(&fio->fileio_lock);
                return -EIO;                                // if we have reached final block and we ant store anymore return error
            }
        }
    }

    rwlock_release(&fio->fileio_lock);
    return (long)(bufp - (const uint8_t *)buf);             // return number of bytes stored (ending buf pos - starting buf pos)
}

/*
INPUTS: struct io * io - pointer to the io object
        const void * buf - pointer to buffer where bytes will be written from
        long len - length in bytes 
OUTPUTS: returns the number of bytes written on success; negative error code on failure
DESCRIPTION: writes bytes from the given buf into the file given by the io object
SIDE EFFECTS: datablocks may be updated
*/
long ngfs_write(struct io * io, const void * buf, long len) {
    return seekio_write(io, buf, len);              // will eventually just call store 
}

/*
INPUTS: struct io * io - poiter to the io object
        int op - integer indicating which operation to execute
        void * arg - pointer to allow for access of passed struct
OUTPUTS: returns 0 on success; negative error code on failure
DESCRIPTION: performs various operation dependant on the passed ops
SIDE EFFECTS: may change the fat struct, dir entries and allocate data blocks if IOC_SETEND passed
*/
int ngfs_ioctl(struct io * io, int op, void * arg) {
    struct ngfs_fileio * const ngfs_io = (void*)io - offsetof(struct ngfs_fileio, io);
    switch (op) {
        case IOC_GETPOS:
            return seekio_ioctl(io, op, arg);

        case IOC_SETPOS:
            return seekio_ioctl(io, op, arg);

        case IOC_GETEND:
            *(unsigned long long *)arg = ngfs_io->size;
            return 0;

        case IOC_SETEND: {
            unsigned long long new_size = *(unsigned long long *)arg;
            if (ngfs_io->size == new_size) return 0;                    // if we have the same size passed then we can just return

            rwlock_acquire(&ngfs_io->fileio_lock, 1);
            struct ngfs * ngfs = ngfs_io->ngfs;
            uint32_t blocks_old = (ngfs_io->size + 511) / 512;          // total number of blocks before and after
            uint32_t blocks_new = (new_size + 511) / 512;

            if (blocks_old < blocks_new) {                              // increase the file block size
                uint32_t blk = ngfs_io->FAT_start_block;                // find the last block within the file
                if (blocks_old > 0) {
                    for (uint32_t i = 1; i < blocks_old; i++) blk = get_next_data_block(ngfs, blk);
                }
                for (uint32_t i = blocks_old; i < blocks_new; i++) {    // for each new block find a new available block
                    uint32_t free_blk = get_free_data_block(ngfs);
                    if (free_blk == NGFS_BLOCK_END) {                   // no more free blocks means memory is full
                        rwlock_release(&ngfs_io->fileio_lock);
                        return -ENOMEM;
                    }

                    void * blkptr;
                    cache_fetch(ngfs->cache, (ngfs->fat_blocks + free_blk) * NGFS_BLKSZ, 1, &blkptr);
                    memset(blkptr,0,512);                               // clear any new data in the block
                    cache_release(ngfs->cache, blkptr, 1);

                    if (i == 0) ngfs_io->FAT_start_block = free_blk;    // Update FAT table indexes
                    else set_next_data_block(ngfs, blk, free_blk);
                    set_next_data_block(ngfs, free_blk, NGFS_BLOCK_END);  
                    blk = free_blk;
                }
            } else if (blocks_old > blocks_new) {                       // decrease the file block size
                if (blocks_new == 0) {                                  // case where all file size set to 0
                    uint32_t blk = ngfs_io->FAT_start_block;
                    while (blk != NGFS_BLOCK_END && blk != NGFS_BLOCK_FREE) { // update FAT table to free all blocks in this file
                        uint32_t next_blk = get_next_data_block(ngfs, blk);
                        set_next_data_block(ngfs, blk, NGFS_BLOCK_FREE);
                        blk = next_blk;
                    }
                    ngfs_io->FAT_start_block = NGFS_BLOCK_END;          // set new end block to -1
                } else {
                    uint32_t blk = ngfs_io->FAT_start_block;            // find the last block for new file size
                    for (uint32_t i = 1; i < blocks_new; i++) blk = get_next_data_block(ngfs, blk);

                    uint32_t next_blk = get_next_data_block(ngfs, blk);
                    set_next_data_block(ngfs, blk, NGFS_BLOCK_END);

                    while (next_blk != NGFS_BLOCK_END) {                // update FAT table to free all blocks until neew end reached
                        uint32_t temp = get_next_data_block(ngfs, next_blk);
                        set_next_data_block(ngfs, next_blk, NGFS_BLOCK_FREE);
                        next_blk = temp;
                    }
                }
            }
            if (new_size < ngfs_io->size && blocks_new > 0) {           // zero out the parts of the file when only part of the file is shrunk
                uint32_t blks_to_clear = new_size % 512;

                if (blks_to_clear > 0) {
                    uint32_t blk = ngfs_io->FAT_start_block;
                    for (uint32_t i = 1; i < blocks_new; i++) blk = get_next_data_block(ngfs, blk);

                    void * blkptr;
                    if (cache_fetch(ngfs->cache, (ngfs->fat_blocks + blk) * NGFS_BLKSZ, 1, &blkptr) == 0) {    // when succesful
                        memset((uint8_t*)blkptr + blks_to_clear, 0, 512 - blks_to_clear);
                        cache_release(ngfs->cache, blkptr, 1);                                  
                    }
                }
            }
            // update the directory to match the new file size
            ngfs_io->size = new_size;
            void * blkptr;
            if (cache_fetch(ngfs->cache, (ngfs->fat_blocks + ngfs_io->dir_blk) * NGFS_BLKSZ, 1, &blkptr) == 0) {
                struct ngfs_dir_entry * dentry = (struct ngfs_dir_entry *)blkptr;       // cast as directory entries
                dentry[ngfs_io->entry_idx].size = new_size;
                dentry[ngfs_io->entry_idx].start_block = ngfs_io->FAT_start_block;
                cache_release(ngfs->cache, blkptr, 1);                                  // mark dirty 
            }
            rwlock_release(&ngfs_io->fileio_lock);
            return 0;
        }

        default:
            break;
    }
    return -ENOTSUP;
}

/*
INPUTS: struct filesystem * fs - passes in the ngfs file system
        const char * name - name of new file to be created
OUTPUTS: returns 0 on success; negative error code on failure
DESCRIPTION: creates a brand new file
SIDE EFFECTS: sets memory; updates FAT and directory
*/
int ngfs_create(struct filesystem * fs, const char * name) {
    struct ngfs * const ngfs = (struct ngfs *)fs;         

    if (name == NULL || *name == '\0') return -EINVAL;                  // Null and empty strings are not valid names
    if (strlen(name) > NGFS_MAX_FILENAME_LEN) return -EINVAL;           // Do not accept names past NGFS_MAX_FILENAME_LEN
    
    rwlock_acquire(&ngfs->ngfs_lock, 1);                                // Must exclusviely lock to prevent race condition
                                          
    const int entries_per_block = NGFS_BLKSZ / NGFS_DENSZ;              // Number of directory entires per data block
    uint32_t cur_blk = NGFS_ROOT_DATA_BLOCK;                            // Begin looking for entries at 0 (root dir)
    uint32_t last_blk = NGFS_ROOT_DATA_BLOCK;
    uint32_t free_dir_blk = NGFS_BLOCK_END;
    int free_entry_idx = -1;

    while (cur_blk != NGFS_BLOCK_END) {                                 // walk through all directory block
        void * blkptr;   
        int result = cache_fetch(ngfs->cache, (ngfs->fat_blocks + cur_blk) * NGFS_BLKSZ, 1, &blkptr);
        if (result < 0) {                                               // fetch dir block, if we fail pass through error code
            rwlock_release(&ngfs->ngfs_lock);
            return result;
        }
        
        // search if entry already exists or for next empty directory entry
        struct ngfs_dir_entry * dents = (struct ngfs_dir_entry *)blkptr;
        int found_free = 0;                                             // FLAG, becomes 1 when emptry directory slot found
        for (int i = 0; i < entries_per_block; i++) {                   // go through all entries in this block
            if (dents[i].name[0] == '\0') {                             // empty names mean open idx; IF WE HAVE EMPTRY NAMES INBETWEEN TWO VALID FILES WILL CAUSE ERRORS!!!
                free_dir_blk = cur_blk;
                free_entry_idx = i;
                found_free = 1;
                break;
            }
            if (strcmp(name, dents[i].name) == 0) {                     // if file name already exists we return error
                cache_release(ngfs->cache, blkptr, 0);
                rwlock_release(&ngfs->ngfs_lock);
                return -EEXIST;
            }
        }
        cache_release(ngfs->cache, blkptr, 0);

        if (found_free) break;                                          // end loop when emptry block found
        last_blk = cur_blk;                                             // itertaes through the directory
        cur_blk = get_next_data_block(ngfs, cur_blk);
    }
    // allocate new directory block if no available block
    if (free_dir_blk == NGFS_BLOCK_END) {
        uint32_t new_blk = get_free_data_block(ngfs);
        if (new_blk == NGFS_BLOCK_END) {                                // if no block remain return error
            rwlock_release(&ngfs->ngfs_lock);
            return -ENOMEM;
        }

        set_next_data_block(ngfs, last_blk, new_blk);                   // updaiting the FAT table
        set_next_data_block(ngfs, new_blk, NGFS_BLOCK_END);

        // grab new directory block and empty it
        void * blkptr;
        int result = cache_fetch(ngfs->cache, (ngfs->fat_blocks + new_blk) * NGFS_BLKSZ, 1, &blkptr);
        if (result < 0) {
            rwlock_release(&ngfs->ngfs_lock);
            return result; 
        }      
        memset(blkptr, 0, NGFS_BLKSZ);
        cache_release(ngfs->cache, blkptr, 1);
        
        free_dir_blk = new_blk;                                         // directory data block indexs
        free_entry_idx = 0;
    }

    // grab the directory block of the new file 
    void * blkptr;
    int result = cache_fetch(ngfs->cache, (ngfs->fat_blocks + free_dir_blk) * NGFS_BLKSZ, 1, &blkptr);
    if (result < 0) { 
        rwlock_release(&ngfs->ngfs_lock);
        return result;
    }

    // cast block as ngfs_dir_entry and update the entry of the new file
    struct ngfs_dir_entry * dents = (struct ngfs_dir_entry *)blkptr;
    memset(dents[free_entry_idx].name, 0, sizeof(dents[free_entry_idx].name));
    strncpy(dents[free_entry_idx].name, name, NGFS_MAX_FILENAME_LEN);
    dents[free_entry_idx].size = 0;
    dents[free_entry_idx].start_block = NGFS_BLOCK_END;
    cache_release(ngfs->cache, blkptr, 1);
    
    // grab the root directory and update the size
    if (cache_fetch(ngfs->cache, (ngfs->fat_blocks * NGFS_BLKSZ), 1, &blkptr) < 0) {          // update root directory size
        rwlock_release(&ngfs->ngfs_lock);
        return -EIO;
    }
    struct ngfs_dir_entry * root_dentry = (struct ngfs_dir_entry *)blkptr;
    root_dentry->size = root_dentry->size + sizeof(struct ngfs_dir_entry);
    cache_release(ngfs->cache, blkptr, 1);
    
    rwlock_release(&ngfs->ngfs_lock);
    return 0;
}

/*
INPUTS: struct filesystem * fs - passes in the ngfs file system
        const char * name - name of file to be deleted
OUTPUTS: returns 0 on success; negative error code on failure
DESCRIPTION: deleted a file from the file system
SIDE EFFECTS: updates FAT and directory; clears some memory
*/
int ngfs_delete(struct filesystem * fs, const char * name) {
    struct ngfs * ngfs = (struct ngfs *)fs;
    if (name == NULL || name[0] == '\0' || strcmp(name, ".") == 0) return -EINVAL;  // dont allow root dir to be deleted
    rwlock_acquire(&ngfs->ngfs_lock, 1);
    const int entries_per_block = NGFS_BLKSZ / NGFS_DENSZ;                          // directory entires per block
    uint32_t directory = NGFS_ROOT_DATA_BLOCK;
    uint32_t deletable_idx = 0;
    uint32_t start_block = NGFS_BLOCK_END;
    int exists = 0;

    while (directory != NGFS_BLOCK_END && exists == 0) {                            // loop through full directory to find the matching name
        void * blkptr;
        if (cache_fetch(ngfs->cache, (ngfs->fat_blocks + directory)* NGFS_BLKSZ, 1, &blkptr) < 0) {
            rwlock_release(&ngfs->ngfs_lock);
            return -EIO;
        }
        struct ngfs_dir_entry * dentry = (struct ngfs_dir_entry *)blkptr;           // cast the block as a directory for data access

        int start = 0;
        if (directory == NGFS_ROOT_DATA_BLOCK) start = 1;                           // if we are in root directory ignore first index
        for (int i = start; i < entries_per_block; i++) {                           // loop through all directory indexes
            if (dentry[i].name[0] == '\0') {                                        // if we are at the last entry in the directory delete
                cache_release(ngfs->cache, blkptr, 0);
                rwlock_release(&ngfs->ngfs_lock);
                return -ENOENT;
            }

            if (strcmp(dentry[i].name, name) == 0) {                                // grab the blocks location indexes if the names matches
                deletable_idx = i;
                start_block = dentry[i].start_block;
                exists = 1;
                break;
            }
        }
        cache_release(ngfs->cache, blkptr, 0);
        if (exists != 1) directory = get_next_data_block(ngfs, directory);          // check next directory table
    }

    if (exists != 1) {
        rwlock_release(&ngfs->ngfs_lock);
        return -ENOENT;
    }

    // clear the block we are deleting
    uint32_t blk = start_block;
    while (blk != NGFS_BLOCK_END && blk != NGFS_BLOCK_FREE) {                       // loop through and update fat table
        uint32_t next_blk = get_next_data_block(ngfs, blk);
        set_next_data_block(ngfs, blk, NGFS_BLOCK_FREE);
        blk = next_blk;
    }

    // grab the directory block     // update the directory across data blocks
    uint32_t swap_blk = directory;
    uint32_t next_blk = swap_blk;
    int swap_idx = (int)deletable_idx;
    int next_idx = swap_idx + 1;
    if (next_idx >= entries_per_block) {                                             // if current directory is full move to next directory blocks
            next_blk = get_next_data_block(ngfs, next_blk);
            next_idx = 0;
    }
    void *blkptr; 
    void *nxtptr;     
    int done = 0;                                   // FLAG, ends loop when becomes 0
    while (done != 1) {
        // fetch index of the block we just deleted and set it to our swap block
        if (cache_fetch(ngfs->cache, (ngfs->fat_blocks + swap_blk) * NGFS_BLKSZ, 1, &blkptr) < 0) {
            rwlock_release(&ngfs->ngfs_lock);
            return -EIO;
        }
        struct ngfs_dir_entry * swap_dentry = (struct ngfs_dir_entry *)blkptr;

        // if next_blk is set to the end then we have no more data to swap and we can end
        if (next_blk == NGFS_BLOCK_END) {
            memset(&swap_dentry[swap_idx], 0, sizeof(struct ngfs_dir_entry ));
            cache_release(ngfs->cache, blkptr, 1);
            done = 1;
            break;
        }
  
        // if the next dentry is not in the same block we need to fetch the next block and point to it
        if(next_blk != swap_blk) {
            if (cache_fetch(ngfs->cache, (ngfs->fat_blocks + next_blk) * NGFS_BLKSZ, 1, &nxtptr) < 0) {
            cache_release(ngfs->cache, blkptr, 0);
            rwlock_release(&ngfs->ngfs_lock);
            return -EIO;
            }
        } else {
            nxtptr = blkptr;                        // if they are stored in the same block we can just use the same pointer for both
        }
        struct ngfs_dir_entry * next_dentry = (struct ngfs_dir_entry *)nxtptr;

        // if we found the last entry we can release and end the loop
        if (next_dentry[next_idx].name[0] == '\0') {
            if (swap_blk != next_blk) cache_release(ngfs->cache, nxtptr, 0);
            memset(&swap_dentry[swap_idx],0, sizeof (struct ngfs_dir_entry));               // clear final swap index
            cache_release(ngfs->cache, blkptr, 1);
            done = 1;
            break;
        }

        memcpy(&swap_dentry[swap_idx], &next_dentry[next_idx], sizeof (struct ngfs_dir_entry));             // next moves into swap 
        if (swap_blk != next_blk) cache_release(ngfs->cache, nxtptr, 0);
        cache_release(ngfs->cache, blkptr, 1);

        // iterate us through our directory and allows up to keep continuity across multiple directory blocks
        swap_blk = next_blk;
        swap_idx = next_idx;
        next_idx = next_idx + 1;
        if (next_idx >= entries_per_block) {
            next_blk = get_next_data_block(ngfs, next_blk);
            next_idx = 0;
        }

    }
    
    // grab the root directory 
    if (cache_fetch(ngfs->cache, (ngfs->fat_blocks)* NGFS_BLKSZ, 1, &blkptr) < 0) {
        rwlock_release(&ngfs->ngfs_lock);;
        return -EIO;
    }

    // update the root directory
    struct ngfs_dir_entry * root_dentry = (struct ngfs_dir_entry *)blkptr;          // update dentry root size
    root_dentry->size = root_dentry->size - sizeof(struct ngfs_dir_entry);
    cache_release(ngfs->cache, blkptr, 1);

    rwlock_release(&ngfs->ngfs_lock);
    return 0;

}

/*
INPUTS: struct filesystem * fs - pointer to the filesystem struct related to NGFS
OUTPUTS: no outputs
DESCRIPTION: flushes ngfs cache
SIDE EFFECTS: all data dirty data is written back to disk
*/
void ngfs_flush(struct filesystem * fs) {
    struct ngfs * ngfs = (struct ngfs *)fs;
    cache_flush(ngfs->cache);               // overheard from a ta something very different thant thid
}

/*
INPUTS: struct io * lsio - lsio pointer to allow access to ngfs_lsio struct
        void * buf - pointer to the buffer that will be read to 
        long bufsz - number of bytes we can read
OUTPUTS: returns the number of bytes read on success; negative error code on failure
DESCRIPTION: reads the name of the current listing object into the buffer
SIDE EFFECTS: no side effect
*/
long ngfs_listing_read(struct io * lsio, void * buf, long bufsz) {
    struct ngfs_lsio * ngfs_lsio = (void *)lsio - offsetof(struct ngfs_lsio, io);
    struct  ngfs * ngfs = ngfs_lsio->ngfs;

    if (bufsz == 0 || buf == NULL) return 0;                 // no space for anything to be written
    
    uint32_t dentry = 0;                                     // ensure we are on the correct directory block
    for (uint32_t blk = 0; blk < ngfs_lsio->dir_blk && dentry != NGFS_BLOCK_END; blk++) {
        dentry = get_next_data_block(ngfs, dentry);
    }

    if (dentry == NGFS_BLOCK_END) return 0;                  // find error code --x-- ??

    void * fptr;                                             // we grab the block by adding the dentry location to the fat block size
    if (cache_fetch(ngfs->cache, (ngfs->fat_blocks + dentry) * NGFS_BLKSZ, 1, &fptr) < 0) return -EIO;

    struct ngfs_dir_entry * dentry_tab = (struct ngfs_dir_entry *)fptr;
    int total_entries = NGFS_BLKSZ/ sizeof(struct ngfs_dir_entry);

    if (ngfs_lsio->entry_idx >= total_entries || dentry_tab[ngfs_lsio->entry_idx].name[0] == '\0') {
        cache_release(ngfs->cache, fptr, 0);
        return 0;                                           // may just leave as 0 for 0 blocks written --x--
    }

    char * name = dentry_tab[ngfs_lsio->entry_idx].name;
    long read = strlen(name);
    if (read > bufsz) read = bufsz;                         // we read the min of bufsz or length of name
    memcpy(buf, name, read);
    cache_release(ngfs->cache, fptr, 0);

    ngfs_lsio->entry_idx++;                                 // incrementing the listing idx
    if (ngfs_lsio->entry_idx >= total_entries) {            // if we have hit all the entries with our current block
        ngfs_lsio->dir_blk++;                               // move to next dir block an set idx to 0
        ngfs_lsio->entry_idx = 0;
    }
    return read;
}

/*
INPUTS: struct io * lsio - lsio pointer to allow access to ngfs_lsio struct
OUTPUTS: no outputs
DESCRIPTION: closes and reclaims the resources of the passed listing object
SIDE EFFECTS: free up memory allocated to the heap
*/
void ngfs_listing_reclaim(struct io * lsio) {
    struct ngfs_lsio * ngfs_lsio = (void*)lsio - offsetof(struct ngfs_lsio, io);
    kfree(ngfs_lsio);
}

// ---HELPER FUNCTION DECLERATIONS--- //

uint32_t get_next_data_block(struct ngfs * ngfs, uint32_t cur_block) {
    uint32_t fat_blk_idx = cur_block / NGFS_FAT_ENTRIES_PER_BLOCK;          // indicates which fat block the data is
    uint32_t fat_tab_idx = cur_block % NGFS_FAT_ENTRIES_PER_BLOCK;          // indicate which index in the fat block the data is

    void * fptr;                                                            // grab the passed block and place it in fblock
    if (cache_fetch(ngfs->cache, fat_blk_idx * NGFS_BLKSZ, 1, &fptr) < 0) {
        return NGFS_BLOCK_END;                                              // returns NGFS_BLOCK_END if cache_fetch fails
    }
    struct ngfs_fat * fatty = (struct ngfs_fat *)fptr;                      // cast data to ngfs_fat
    uint32_t next_blk = fatty->fat[fat_tab_idx];                            // grab index of next blocks
    cache_release(ngfs->cache, fptr, 0);                                    // mark cached block as released
    return next_blk;
}

void set_next_data_block(struct ngfs * ngfs, uint32_t cur_block, uint32_t next_block) {
    uint32_t fat_blk_idx = cur_block / NGFS_FAT_ENTRIES_PER_BLOCK;          // indicates which fat block the data is
    uint32_t fat_tab_idx = cur_block % NGFS_FAT_ENTRIES_PER_BLOCK;          // indicate which index in the fat block the data is

    void * fptr;                                                            // grab the passed block and place it in fblock
    if (cache_fetch(ngfs->cache, fat_blk_idx * NGFS_BLKSZ, 1, &fptr) < 0) return;
    struct ngfs_fat * fatty = (struct ngfs_fat *)fptr;
    fatty->fat[fat_tab_idx] = next_block;                                   // sets next blocks index in FAT TABLE
    cache_release(ngfs->cache, fptr, 1);
}

uint32_t get_free_data_block(struct ngfs * ngfs) {
    for (uint32_t blk = 1; blk < ngfs->data_blocks; blk++){
        uint32_t fat_blk_idx = blk / NGFS_FAT_ENTRIES_PER_BLOCK;            // indicates which fat block the data is
        uint32_t fat_tab_idx = blk % NGFS_FAT_ENTRIES_PER_BLOCK;            // indicate which index in the fat block the data is
        void * fptr;                                                        // grab the passed block and place it in fblock
        if (cache_fetch(ngfs->cache, fat_blk_idx * NGFS_BLKSZ, 1, &fptr) < 0) continue;
        struct ngfs_fat * fatty = (struct ngfs_fat *)fptr;
        if (fatty->fat[fat_tab_idx] == NGFS_BLOCK_FREE) {                   // if data block is free return its idx
            cache_release(ngfs->cache, fptr, 0);
            return blk;
        }
        cache_release(ngfs->cache, fptr, 0);                                // always release the cached block
    }
    return NGFS_BLOCK_END;                                                 // return NGFS_BLOCK_END on failure
}
