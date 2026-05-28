// device.h - Device manager
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef DEVICE_TRACE
#define TRACE
#endif

#ifdef DEVICE_DEBUG
#define DEBUG
#endif

#include "device.h"

#include <limits.h>  // INT_MAX
#include <stddef.h>

#include "conf.h"
#include "error.h"
#include "filesys.h"
#include "fsimpl.h"
#include "heap.h"
#include "misc.h"
#include "string.h"
#include "ioimpl.h"

// INTERNAL TYPE DEFINITIONS
//

// Device list entry

struct device_record {
    struct device_record * next;    // Next device record in linked list
    device_openfn_t openfn;         // Function for opening device
    void * ofaux;                   // Auxilliary argument for openfn
    char name[];                    // device name
};

// devfs listing io object

struct devfs_lsio {
    struct io base;
    const struct device_record * next;
};

// INTERNAL FUNCTION DECLARATIONS
//

static int devfs_open_file(struct filesystem * fs, const char * name, struct io ** ioptr);
static long devfs_listing_read(struct io * io, void * buf, long bufsz);

struct device_record * find_device(const char * name);

// EXPORTED GLOBAL VARIABLES
//

char devmgr_initialized = 0;

static struct filesystem devfs = {
    .implname = "devfs",
    .openfile = &devfs_open_file
};

// DEVMGR INTERNAL GLOBAL VARIABLES
//

static struct device_record * devlist;

static const struct iointf devfs_lsio_intf = {
    .implname = "devfs_lsio",
    .reclaim = (void(*)(struct io*))&kfree,
    .read = &devfs_listing_read
};

// EXPORTED FUNCTION DEFINITIONS
//

void devmgr_init(void) {
    trace("%s()", __func__);
    devmgr_initialized = 1;
}

int register_device (
    const char * name,
    int instno,
    device_openfn_t openfn,
    void * ofaux)
{
    struct device_record * dev = NULL;
    size_t namelen;

    trace("%s(\"%s\", %d)", __func__, name, instno);

    assert(devmgr_initialized);
    assert(name != NULL);
    assert(openfn != NULL);

    namelen = strlen(name);

    if (999 < instno || 255 < namelen)
        return -ENAMETOOLONG;

    dev = kmalloc(sizeof(*dev) + namelen+3+1);
    memset(dev, 0, sizeof(*dev));

    if (0 <= instno)
        snprintf(dev->name, namelen+3+1, "%s%d", name, instno);
    else
        memcpy(dev->name, name, namelen+1);

    // Check if the device is already registered
    
    if (find_device(dev->name) != NULL) {
        kfree(dev);
        return -EEXIST;
    }

    dev->openfn = openfn;
    dev->ofaux = ofaux;
    dev->next = devlist;
    devlist = dev;
    return 0;
}

extern int open_device(const char * name, struct io ** ioptr) {
    struct device_record * dev;

    trace("%s(\"%s\")", __func__, name);

    dev = find_device(name);

    return (dev != NULL) ? dev->openfn(ioptr, dev->ofaux) : -ENOENT;
}

int device_exists(const char * name) {
    return (find_device(name) != NULL);
}

int device_ioaddref_openfn(struct io ** ioptr, void * aux) {
    *ioptr = ioaddref(aux);
    return 0;
}

// INTERNAL FUNCTION DEFINITIONS
//

int mount_devfs(const char * mpname) {
    return mount_filesys(mpname, &devfs);
}

int devfs_open_file (
    struct filesystem * fs,
    const char * name,
    struct io ** ioptr)
{
    struct devfs_lsio * lsio;

    trace("%s(\"%s\")", __func__, name);

    assert (fs == &devfs);
    assert (ioptr != NULL);

    if (name != NULL && *name != '\0')
        return open_device(name, ioptr);
    
    // If /name/ is NULL, open a device listing.

    lsio = kcalloc(1, sizeof(*lsio));
    lsio->next = devlist;

    *ioptr = ioinit(&lsio->base, &devfs_lsio_intf, 1, 1);
    return 0;
}

long devfs_listing_read(struct io * io, void * buf, long bufsz) {
    struct devfs_lsio * const lsio = (struct devfs_lsio*)io;

    if (bufsz != 0 && lsio->next != NULL) {
        strlcpy(buf, lsio->next->name, bufsz);
        lsio->next = lsio->next->next;
        return strlen(buf)+1;
    } else
        return 0;
}

struct device_record * find_device(const char * name) {
    struct device_record * dev;

    trace("%s(\"%s\")", __func__, name);

    for (dev = devlist; dev != NULL; dev = dev->next) {
        if (strcmp(name, dev->name) == 0)
            return dev;
    }

    debug("Device %s not found", name);
    return NULL;
}