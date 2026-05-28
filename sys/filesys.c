// fsimpl.h - File system manager
//
// Copyright (c) 2025-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef FILESYS_TRACE
#define TRACE
#endif

#ifdef FILESYS_DEBUG
#define DEBUG
#endif

#include "fsimpl.h"

#include <stddef.h>

#include "error.h"
#include "heap.h"
#include "misc.h"
#include "string.h"
#include "ioimpl.h"

// INTERNAL TYPE DEFINITIONS
//

struct mountpoint {
    struct mountpoint * next;  // Next mountpoint in linked list
    const char * name;         // mountpoint name
    struct filesystem * fs;    // Filesystem at this mountpoint
};


// INTERNAL FUNCTION PROTOTYPES
//

static struct mountpoint * findmp(const char * mpname);

static int open_root_listing(struct io ** ioptr);
static void root_listing_reclaim(struct io * io);
static long root_listing_read(struct io * io, void * buf, long bufsz);

static int nullfs_openfile(struct filesystem * fs, const char * flname, struct io ** ioptr);


// INTERNAL GLOBAL VARIABLES
//

struct root_lsio {
    struct io io;
    const struct mountpoint * next;
};

static const struct iointf root_lsio_intf = {
    .implname = "root_lsio",
    .reclaim = (void(*)(struct io*))&kfree,
    .read = &root_listing_read
};

// Linked list of mounted filesystems

static struct mountpoint * mplist;


// EXPORTED GLOBAL VARIABLES
//

char fsmgr_initialized = 0;

struct filesystem nullfs = {
    .implname = "nullfs",
    .openfile = &nullfs_openfile
};


// EXTERNAL FUNCTION DEFINITIONS
//

int fsmgr_init(void) {
    fsmgr_initialized = 1;
    return 0;
}

int mount_filesys(const char * mpname, struct filesystem * fs) {
    struct mountpoint * mp;

    trace("%s(\"%s\")", __func__, mpname);
    assert (mpname != NULL);
    assert (fs != NULL);

    // Check if mountpoint already exists

    for (mp = mplist; mp != NULL; mp = mp->next) {
        if (strcmp(mp->name, mpname) == 0)
            return -EEXIST;
    }

    // Create mountpoint and add it to the list.

    mp = kcalloc(1, sizeof(*mp));
    mp->next = mplist;
    mp->name = mpname;
    mp->fs = fs;
    mplist = mp;
    return 0;
}

void flush_all_filesys(void) {
    struct mountpoint * mp;

    for (mp = mplist; mp != NULL; mp = mp->next) {
        if (mp->fs->flush != NULL)
            mp->fs->flush(mp->fs);
    }
}

int open_file(const char * mpname, const char * flname, struct io ** ioptr) {
    struct mountpoint * mp;

    trace("%s(\"%s\",\"%s\")", __func__, mpname, flname);
    assert (ioptr != NULL);

    if (mpname == NULL || *mpname == '\0') {
        assert (flname == NULL);
        return open_root_listing(ioptr);
    }

    mp = findmp(mpname);
    
    if (mp == NULL)
        return -ENOENT;
    
    if (mp->fs->openfile == NULL)
        return -ENOTSUP;
    
    return mp->fs->openfile(mp->fs, flname, ioptr);
}

int create_file(const char * mpname, const char * flname) {
    struct mountpoint * mp;

    trace("%s(\"%s\",\"%s\")", __func__, mpname, flname);
    assert (mpname != NULL);

    if (flname == NULL)
        return -ENOTSUP;

    mp = findmp(mpname);

    if (mp == NULL)
        return -ENOENT;
    
    if (mp->fs->createfile == NULL)
        return -ENOTSUP;
    
    return mp->fs->createfile(mp->fs, flname);
}

int delete_file(const char * mpname, const char * flname) {
    struct mountpoint * mp;

    trace("%s(\"%s\",\"%s\")", __func__, mpname, flname);
    assert (mpname != NULL);

    if (flname == NULL)
        return -ENOTSUP;
    
    mp = findmp(mpname);

    if (mp == NULL)
        return -ENOENT;
    
    if (mp->fs->deletefile == NULL)
        return -ENOTSUP;
    
    return mp->fs->deletefile(mp->fs, flname);
}

void parse_path(char * path, char ** mpnameptr, char ** flnameptr) {
    assert (path != NULL);
    assert (mpnameptr != NULL);
    assert (flnameptr != NULL);
    char * ss; // slash in path

    // ignore leading slashes
    while (*path == '/')
        path += 1;

    ss = strchr(path, '/');

    if (ss != NULL) {
        *flnameptr = ss + 1;
        *ss = '\0';
    } else // no slashes indicates mp only
        *flnameptr = NULL;

    *mpnameptr = path;
}

// INTERNAL FUNCTION DEFINITIONS
//

static struct mountpoint * findmp(const char * mpname) {
    struct mountpoint * mp;

    for (mp = mplist; mp != NULL; mp = mp->next) {
        if (strcmp(mpname, mp->name) == 0)
            return mp;
    }

    return NULL;
}

int open_root_listing(struct io ** ioptr) {
    struct root_lsio * lsio;

    assert (ioptr != NULL);

    lsio = kcalloc(1, sizeof(*lsio));
    lsio->next = mplist;

    *ioptr = ioinit(&lsio->io, &root_lsio_intf, 1, 1);
    return 0;
}

long root_listing_read(struct io * io, void * buf, long bufsz) {
    struct root_lsio * lsio = (struct root_lsio*)io;

    if (bufsz != 0 && lsio->next != NULL) {
        strlcpy(buf, lsio->next->name, bufsz);
        lsio->next = lsio->next->next;
        return strlen(buf)+1;
    } else
        return 0;
}


int nullfs_openfile (
    struct filesystem * fs, const char * flname, struct io ** ioptr)
{
    assert (fs == &nullfs);
    assert (ioptr != NULL);

    if (flname == NULL) {
        *ioptr = create_nullio();
        return 0;
    } else
        return -ENOENT;
}