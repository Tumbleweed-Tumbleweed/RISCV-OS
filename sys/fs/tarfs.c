// tarfs.c - A file system backed by a tar archive
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef TARFS_TRACE
#define TRACE
#endif

#ifdef TARFS_DEBUG
#define DEBUG
#endif

#include "tarfs.h"

#include <limits.h> // ULONG_MAX

#include "../fsimpl.h"
#include "../filesys.h"
#include "../ioimpl.h"
#include "../error.h"
#include "../heap.h"
#include "../string.h"
#include "../thread.h"
#include "../cache.h"
#include "../misc.h"

// INTERNAL CONSTANT DEFINITIONS
//

#define TAR_BLKSZ 512

// INTERNAL TYPE DEFINITIONS
//

struct ustar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char csum[8];
    char type;
    char link[100];
    char ustar[6];
    char version[2];
};

struct tarfs;

struct tarfs_file {
    struct tarfs_file * next;  // linked list next pointer
    struct tarfs * tarfs;      // filesystem to which file belongs
    unsigned long size;        // size of the file
    unsigned long blkno;       // block number of first block
    char name[];
};

struct tarfs {
    struct filesystem base;
    struct io * bkgio;
    struct cache * cache;
    struct tarfs_file * files;
};

struct tarfs_fileio {
    struct seekio io;
    struct tarfs_file * file;
};

struct tarfs_lsio {
    struct io io;
    struct tarfs_file * next;
};

// INTERNAL FUNCTION DECLARATIONS
//


static int tarfs_openfile(struct filesystem * fs, const char * name, struct io ** ioptr);

static int tarfs_open_fileio(struct tarfs * tarfs, const char * name, struct io ** ioptr);
static long tarfs_fileio_fetch(struct io * io, unsigned long long pos, void * buf, long bufsz);
static int tarfs_fileio_ioctl(struct io * io, int op, void * arg);

static int tarfs_open_lsio(struct tarfs * tarfs, struct io ** ioptr);
static long tarfs_lsio_read(struct io * io, void * buf, long bufsz);


// INTERNAL GLOBAL VARIABLES
//

static const struct iointf tarfs_file_intf = {
    .implname = "tarfs_fileio",
    .reclaim = (void(*)(struct io*))&kfree,
    .read = &seekio_read,
    .fetch = &tarfs_fileio_fetch,
    .ioctl = &tarfs_fileio_ioctl
};

static const struct iointf tarfs_lsio_intf = {
    .implname = "tarfs_lsio",
    .reclaim = (void(*)(struct io*))&kfree,
    .read = &tarfs_lsio_read
};

static const struct filesystem tarfs_intf = {
    .implname = "tarfs",
    .openfile = &tarfs_openfile,
};

// EXPORTED FUNCTION DEFINITIONS
//

int mount_tarfs(const char * mpname, struct io * bkgio) {
    char blkbuf[TAR_BLKSZ]; // block buffer
    struct ustar_header * const hdr = (void*)blkbuf;
    char buf[16]; // for null-terminating strings
    unsigned int zblkcnt = 0; // no. of zero blocks
    char * end; // for strtoul
    unsigned long long bkgcap;
    struct tarfs_file * file;
    struct tarfs * fs;
    int result;
    long rlen;
    size_t namelen;
    char * namestr;
    unsigned long blkno;
    unsigned long blkcnt;
    unsigned int blksz;

    trace("%s(%s,%p)", __func__, mpname, bkgio);

    fs = kcalloc(1, sizeof(*fs));

    blksz = ioblksz(bkgio);
    assert (0 <= blksz);

    if (TAR_BLKSZ % blksz != 0) {
        debug("Incompatible device block size: %d", blksz);
        return -ENOTSUP;
    }

    result = ioctl(bkgio, IOC_GETEND, &bkgcap);

    if (result != 0) {
        debug("ioctl(IOC_GETEND) returned %d", result);
        return result;
    }

    // Scan through archive to find all files. Each file starts with a file
    // header that gives its length. Create a /tarfs_file/ structure for each
    // file we find and keep those in a linked list. All of these I/O here is
    // direct to device (no cache) since we will only access the heade block
    // once and never again.

    blkno = 0;
    while (blkno < bkgcap / TAR_BLKSZ) {
        debug("Reading tar header in block %lu", blkno);
        rlen = iofetch(bkgio, 1ULL * blkno * TAR_BLKSZ, blkbuf, TAR_BLKSZ);
        blkno += 1;

        if (rlen < 0)
            return rlen;
        if (rlen != TAR_BLKSZ)
            return -EIO;
        
        if (hdr->name[0] == '\0') {
            // If name is blank, treat this as an all-zero block
            zblkcnt += 1;
            
            if (zblkcnt <= 1)
                continue;
            else
                break;
        } else
            zblkcnt = 0;
        
        hdr->name[100] = '\0';
        namestr = strrchr(hdr->name, '/');

        if (namestr == NULL)
            namestr = hdr->name;
        else
            namestr += 1;

        namelen = strlen(namestr);

        if (HEAP_ALLOC_MAX - sizeof(*file) - 1 < namelen) {
            debug("File name too long (%zu bytes)", namelen);
            return -EBADFMT;
        }

        // FIXME If we fail at any point after this, we don't free the the
        // allocated file structs (memory leak).

        file = kcalloc(1, sizeof(*file) + namelen + 1);
        strncpy(file->name, namestr, namelen+1);

        end = NULL;
        memset(buf, 0, sizeof(buf));
        memcpy(buf, hdr->size, sizeof(hdr->size));
        file->size = strtoul(buf, &end, 8);
        file->tarfs = fs;

        if (buf[0] == '\0' || end == NULL || *end != '\0')
            return -EBADFMT;
        
        debug("File %s has size %lu", file->name, file->size);

        file->blkno = blkno;
        blkcnt = (file->size + TAR_BLKSZ-1) / TAR_BLKSZ;

        if (ULONG_MAX - blkno < blkcnt) {
            debug("File %s too large", file->name);
            return -EBADFMT;
        }

        blkno += blkcnt;

        if (bkgcap / TAR_BLKSZ < blkno) {
            debug("File %s too large", file->name);
            return -EBADFMT;
        }

        file->next = fs->files;
        fs->files = file;
    }

    // Create a cache for the backing storage io object.

    fs->cache = create_cache(bkgio, TAR_BLKSZ);

    if (fs->cache == NULL)
        panic("create_cache() failed");
    
    fs->bkgio = ioaddref(bkgio);
    fs->base = tarfs_intf;

    result = mount_filesys(mpname, &fs->base);
    return result;
}

int tarfs_openfile (
    struct filesystem * fs,
    const char * name,
    struct io ** ioptr)
{
    struct tarfs * const tarfs = (void*)fs;
    assert (fs != NULL);
    assert (ioptr != NULL);

    if (name != NULL)
        return tarfs_open_fileio(tarfs, name, ioptr);
    else
        return tarfs_open_lsio(tarfs, ioptr);
}


int tarfs_open_fileio (
    struct tarfs * fs,
    const char * name,
    struct io ** ioptr)
{
    struct tarfs_fileio * fio;
    struct tarfs_file * file;
    
    trace("%s(\"%s\")", __func__, name);

    for (file = fs->files; file != NULL; file = file->next)
        if (strcmp(name, file->name) == 0)
            break;
    
    if (file == NULL)
        return -ENOENT;
    
    fio = kmalloc(sizeof(*fio));
    fio->file = file;

    *ioptr = seekio_init(&fio->io, &tarfs_file_intf, 1, 1);

    return 0;
}

long tarfs_fileio_fetch (
    struct io * io, unsigned long long pos, void * buf, long bufsz)
{
    struct tarfs_fileio * const fio =
        (void*)io - offsetof(struct tarfs_fileio, io);
    struct cache * cache;
    unsigned long blkno;
    unsigned long off;
    size_t cpycnt;
    void * bufp;
    void * blk;
    int result;

    trace("%s(io=%p, buf=%p, bufsz=%zu)", __func__, io, buf, bufsz);

    assert (io != NULL);

    if (bufsz == 0 || buf == NULL)
        return 0;

    cache = fio->file->tarfs->cache;

    // Trim request so we don't try to read past end of file.

    if (fio->file->size - pos < bufsz)
        bufsz = fio->file->size - pos;

    bufp = buf; // pointer into buffer where we will write next byte
        
    while (bufsz > 0) {
        blkno = (fio->file->blkno * TAR_BLKSZ + pos) / TAR_BLKSZ;
        off = pos % TAR_BLKSZ;   // offset from where we want data

        result = cache_fetch(cache, blkno * TAR_BLKSZ, 1, &blk);
        
        if (result < 0)
            return result;

        if (TAR_BLKSZ-off < bufsz)
            cpycnt = TAR_BLKSZ-off;
        else
            cpycnt = bufsz;
        
        memcpy(bufp, blk+off, cpycnt);
        cache_release(cache, blk, /* dirty */ 0);

        pos += cpycnt;
        bufp += cpycnt;
        bufsz -= cpycnt;
    }

    return bufp - buf;
}


int tarfs_fileio_ioctl(struct io * io, int op, void * arg) {
    struct tarfs_fileio * const fio =
        (void*)io - offsetof(struct tarfs_fileio, io);
    switch (op) {
    case IOC_GETEND:
        *(unsigned long long *)arg = fio->file->size;
        return 0;
    case IOC_GETPOS:
    case IOC_SETPOS:
        return seekio_ioctl(io, op, arg);
    default:
        return -ENOTSUP;
    }
}

int tarfs_open_lsio(struct tarfs * fs, struct io ** ioptr) {
    struct tarfs_lsio * lsio;

    lsio = kcalloc(1, sizeof(*lsio));
    lsio->next = fs->files;
    *ioptr = ioinit(&lsio->io, &tarfs_lsio_intf, 1, 1);
    return 0;
}

long tarfs_lsio_read(struct io * io, void * buf, long bufsz) {
    struct tarfs_lsio * const lsio =
        (void*)io - offsetof(struct tarfs_lsio, io);

    if (bufsz == 0 || buf == NULL || lsio->next == NULL)
        return 0;

    strlcpy(buf, lsio->next->name, bufsz);
    lsio->next = lsio->next->next;
    return strlen(buf);
}
