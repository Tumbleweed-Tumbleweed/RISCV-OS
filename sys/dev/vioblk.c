// vioblk.c - VirtIO block device
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef VIOBLK_TRACE
#define TRACE
#endif

#ifdef VIOBLK_DEBUG
#define DEBUG
#endif

#include "virtio.h"
#include "intr.h"
#include "heap.h"
#include "device.h"
#include "thread.h"
#include "error.h"
#include "string.h"
#include "conf.h"
#include "misc.h"
#include "error.h"
#include "console.h"
#include "ioimpl.h"

#include <limits.h>

// ---COMPILE-TIME PARAMETERS--- //

#ifndef VIOBLK_INTR_PRIO
#define VIOBLK_INTR_PRIO 1
#endif

#ifndef VIOBLK_NAME
#define VIOBLK_NAME "vioblk"
#endif

// ---INTERNAL CONSTANT DEFINITIONS--- //

// VirtIO block device feature bits (number, *not* mask)
#define VIRTIO_BLK_F_SIZE_MAX       1
#define VIRTIO_BLK_F_SEG_MAX        2
#define VIRTIO_BLK_F_GEOMETRY       4
#define VIRTIO_BLK_F_RO             5
#define VIRTIO_BLK_F_BLK_SIZE       6
#define VIRTIO_BLK_F_FLUSH          9
#define VIRTIO_BLK_F_TOPOLOGY       10
#define VIRTIO_BLK_F_CONFIG_WCE     11
#define VIRTIO_BLK_F_MQ             12
#define VIRTIO_BLK_F_DISCARD        13
#define VIRTIO_BLK_F_WRITE_ZEROES   14


#define ENOTSUP     3 // Operation not supported
// ---INTERNAL TYPE DEFINITIONS--- //

// All VirtIO block device requests consist of a request header, defined below,
// followed by data, followed by a status byte. The header is device-read-only,
// the data may be device-read-only or device-written (depending on request
// type), and the status byte is device-written.
struct vioblk_request_header {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

// Request type (for vioblk_request_header)
#define VIRTIO_BLK_T_IN             0
#define VIRTIO_BLK_T_OUT            1

// Status byte values
#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2

/**
 * @brief VirtIO Block Device with virtqueues and condition variables
*/ 
struct vioblk_device {
    volatile struct virtio_mmio_regs * regs;
    int irqno;                              // interupt number for vioblk_device
    struct io io;                           // vioblocks io interface
    int blksz;                              // block size of the viboblock

    struct vioblk_request_header vblk_rh;   // vioblocks request header
    volatile uint8_t vblk_status;           // byte contatining the status; written by device on completion
    struct virtq_desc * descriptor;         // descriptor "table" that will just point to the indirect descriptor
    struct virtq_desc * indirect;           // we will use an indirect descriptor to simplify avail math
    struct virtq_avail * avail;             // driver to device communication
    struct virtq_used * used;               // device to driver communication
    struct condition vblk_cond;             // condition variable for viorng for threading
    struct rwlock vblk_lock;                // lock for fetch and store

    unsigned int interupt_en;               // tracks if we have interupts currently enabled or not
    uint16_t idx_prev_used;                 // tracks last used idx 
    int cond_done;                          // isr sets this to 1 when vblk_cond has been broadcast
};

// ---INTERNAL FUNCTION DECLARATIONS--- //
static int vioblk_open(struct io ** ioptr, void * aux);

static void vioblk_reclaim(struct io * io);

static long vioblk_fetch (struct io * io, unsigned long long pos, void * buf, long len);

static long vioblk_store (struct io * io, unsigned long long pos, const void * buf, long len);

static int vioblk_ioctl(struct io * io, int op, void * arg);

static void vioblk_isr(int srcno, void * aux);
            
// ---INTERNAL GLOBAL VARIABLES--- //
static const struct iointf vioblk_intf = {
    .implname = "vioblk",
    .reclaim = &vioblk_reclaim,
    .fetch = &vioblk_fetch,
    .store = &vioblk_store,
    .ioctl = &vioblk_ioctl
};

// ---EXPORTED FUNCTION DEFINITIONS--- //

// The vioblk_attach function is declared and called from virtio_attach() in
// virtio.c when a VirtIO block device is found.

/*
INPUTS: volatile struct virtio_mmio_regs * regs - pointer to the vioblock registerss
        int irqno - interupt number for device
OUTPUTS: no outputs
DESCRIPTION: attaches vioblock device
SIDE EFFECTS:
*/
void vioblk_attach(volatile struct virtio_mmio_regs * regs, int irqno) {
    static unsigned short instcnt = 0; // number of vioblk devices
    virtio_featset_t enabled_features, wanted_features, needed_features;
    struct vioblk_device * vb;
    unsigned int blksz;
    int result;
    
	trace("%s(regs=%p,irqno=%d)", __func__, regs, irqno);

    assert (regs->device_id == VIRTIO_ID_BLOCK);

    // Signal device that we found a driver

    regs->status |= VIRTIO_STAT_DRIVER;
    __sync_synchronize(); // fence o,io

    // Negotiate features. We need:
    //  - VIRTIO_F_RING_RESET and
    //  - VIRTIO_F_INDIRECT_DESC
    // We want:
    //  - VIRTIO_BLK_F_BLK_SIZE and
    //  - VIRTIO_BLK_F_TOPOLOGY.

    virtio_featset_init(needed_features);
    virtio_featset_add(needed_features, VIRTIO_F_RING_RESET);
    virtio_featset_add(needed_features, VIRTIO_F_INDIRECT_DESC);
    virtio_featset_init(wanted_features);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_BLK_SIZE);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_TOPOLOGY);
    result = virtio_negotiate_features(regs,
        enabled_features, wanted_features, needed_features);

    if (result != 0) {
        kprintf("%p: virtio feature negotiation failed\n", regs);
        return;
    }

    // If the device provides a block size, use it. Otherwise, use 512.

    if (virtio_featset_test(enabled_features, VIRTIO_BLK_F_BLK_SIZE))
        blksz = regs->config.blk.blk_size;
    else
        blksz = 512;

    // blksz must be a power of two
    assert (((blksz - 1) & blksz) == 0);

    vb = kcalloc(1, sizeof(*vb));
    if (vb == NULL) return;

    vb->regs = regs;
    vb->irqno = irqno;
    vb->blksz = blksz;
    vb->interupt_en = 0;
    vb->idx_prev_used = 0;
    vb->cond_done = 0;

    condition_init(&vb->vblk_cond, "vioblock_cond");
    rwlock_init(&vb->vblk_lock, "vioblock_lock");

    void * unalgined1 = kcalloc(1, sizeof(struct virtq_desc));                 // initizalize the descriptor
    if (unalgined1 == NULL) return;
    vb->descriptor = (struct virtq_desc *)(((uintptr_t)unalgined1 + 15) & ~(uintptr_t)15);

    void * unalgined2 = kcalloc(1, 3 * sizeof(struct virtq_desc));          // initizalize the indirect descriptor
    if (unalgined2 == NULL) return;
    vb->indirect = (struct virtq_desc *)(((uintptr_t)unalgined2 + 15) & ~(uintptr_t)15);

    if (vb->indirect == NULL) return;
    vb->descriptor->addr = (uint64_t)(vb->indirect);
    vb->descriptor->len = 3 * sizeof(struct virtq_desc);
    vb->descriptor->flags = VIRTQ_DESC_F_INDIRECT;
    vb->descriptor->next = 0;


    vb->avail = kcalloc(1, sizeof(struct virtq_avail) + sizeof(uint16_t));        // initialize the avail ring
    if (vb->avail == NULL) return;
    vb->avail->flags = 0;
    vb->avail->idx = 0;
    vb->avail->ring[0] = 0;

    vb->used = kcalloc(1, sizeof(struct virtq_used)+ sizeof(struct virtq_used_elem));       // initialize the used ring
    if (vb->used == NULL) return;
    vb->used->flags = 0;
    vb->used->idx = 0;

    regs->status |= VIRTIO_STAT_FEATURES_OK;
    assert(regs->status & VIRTIO_STAT_FEATURES_OK);

    virtio_attach_virtq(regs, 0, 1, (uint64_t)vb->descriptor, (uint64_t)vb->used, (uint64_t)vb->avail);
    virtio_enable_virtq(regs, 0);

    regs->status |= VIRTIO_STAT_DRIVER_OK;          //set the driver to OK
    __sync_synchronize();           // fence o,oi

    ioinit(&vb->io, &vioblk_intf, blksz, 1);
    register_device(VIOBLK_NAME, instcnt++, &vioblk_open, vb);
}

/*
INPUTS: struct io * io - pointer to the io pointer
        void * aux passes in the viorng_device
OUTPUTS: returns 0 to indicate successful opening
DESCRIPTION: prepares the vioblk_device passed for I/O functionality
SIDE EFFECTS: *ioptr contains a pointer to the viorng_device I/O object
*/
int vioblk_open(struct io ** ioptr, void * aux) {
    struct vioblk_device * const vb = aux;
    trace("%s()", __func__);

    if(vb->interupt_en != 1){         // as the same vrng can be opened many times we only do this the first time it is opened
        enable_intr_source(vb->irqno, VIOBLK_INTR_PRIO, vioblk_isr, vb);
        vb->interupt_en = 1;
    }
    *ioptr = ioaddref(&vb->io);
    return 0;
}

/*
INPUTS: struct io * io - pointer to io object
OUTPUTS: no output
DESCRIPTION: when reclaim is called interupts are disabled from the passed vioblk_device; we do not reclaim memory!
SIDE EFFECTS: the passed vioblk_device from the io will no longer be able signal interupts
*/
void vioblk_reclaim(struct io * io) {
    struct vioblk_device * const vb = (void*)io - offsetof(struct vioblk_device, io);
    vb->interupt_en = 0;
    disable_intr_source(vb->irqno);
}


/*
INPUTS: struct io * io -
        unsigned long long bytepos -
        void * buf -
        long bytecnt -
OUTPUTS:
DESCRIPTION:
SIDE EFFECTS:
*/
long vioblk_fetch (struct io * io, unsigned long long bytepos, void * buf, long bytecnt) {
    unsigned long long endpos;
    int byte_capacity;

    // An implementation must fetch exactly bytecnt bytes if pos plus bytecnt is not more than the storage I/O object's capacity as returned by ioctl(IOC_GETEND).
    byte_capacity = vioblk_ioctl(io, IOC_GETEND, &endpos);
    if (byte_capacity < 0) return byte_capacity;
    if ((bytepos + bytecnt) > endpos) return -1; // EINVAL

    struct vioblk_device * vb;
    vb = (void *)io - offsetof(struct vioblk_device, io);

    // interrupts and locks setup
    rwlock_acquire(&vb->vblk_lock, 1);

    // header setup - local header so multiple threads dont clober
    vb->vblk_rh.type = VIRTIO_BLK_T_IN;
    vb->vblk_rh.reserved = 0;
    vb->vblk_rh.sector = bytepos / 512;
    vb->vblk_status = (uint8_t)-1;

    // indirect descriptiors
    // Header descriptiors
    vb->indirect[0].addr = (uint64_t)&vb->vblk_rh; 
    vb->indirect[0].len = sizeof(vb->vblk_rh);
    vb->indirect[0].flags = VIRTQ_DESC_F_NEXT; 
    vb->indirect[0].next = 1;

    // Buffer descriptors
    vb->indirect[1].addr = (uint64_t)buf; 
    vb->indirect[1].len = bytecnt;
    vb->indirect[1].flags = VIRTQ_DESC_F_WRITE | VIRTQ_DESC_F_NEXT; 
    vb->indirect[1].next = 2;

    // Status descriptors
    vb->indirect[2].addr = (uint64_t)&vb->vblk_status; 
    vb->indirect[2].len = sizeof(vb->vblk_status);
    vb->indirect[2].flags = VIRTQ_DESC_F_WRITE; 
    vb->indirect[2].next = 0;

    vb->cond_done = 0;
    __sync_synchronize();
    
    vb->avail->ring[(vb->avail->idx) % 1] = 0;
    __sync_synchronize();

    vb->avail->idx = (vb->avail->idx) + 1;
    __sync_synchronize();
    
    virtio_notify_avail(vb->regs, 0);
    __sync_synchronize();

    // Sleep
    disable_interrupts();
    while(vb->cond_done == 0) {
        condition_wait(&vb->vblk_cond);  
        disable_interrupts();
    }
    enable_interrupts();
    __sync_synchronize();

    // Unlock and restore inputs
    rwlock_release(&vb->vblk_lock);

    // Check if sucessful
    if (vb->vblk_status == VIRTIO_BLK_S_OK) {
        return bytecnt;
    }
    return -1;

}

/*
INPUTS: struct io * io -
        unsigned long long bytepos -
        const void * buf -
        long bytecnt -
OUTPUTS:
DESCRIPTION:
SIDE EFFECTS:
*/
long vioblk_store (struct io * io, unsigned long long bytepos, const void * buf, long bytecnt) {
    unsigned long long endpos;
    int byte_capacity;

    // An implementation must fetch exactly bytecnt bytes if pos plus bytecnt is not more than the storage I/O object's capacity as returned by ioctl(IOC_GETEND).
    byte_capacity = vioblk_ioctl(io, IOC_GETEND, &endpos);
    if (byte_capacity < 0)return byte_capacity;
    if ((bytepos + bytecnt) > endpos) return -1; // EINVAL

    struct vioblk_device * vb;
    vb = (void *)io - offsetof(struct vioblk_device, io);

    // interrupts and locks setup
    rwlock_acquire(&vb->vblk_lock, 1);

    // header setup
    vb->vblk_rh.type = VIRTIO_BLK_T_OUT;
    vb->vblk_rh.reserved = 0;
    vb->vblk_rh.sector = bytepos / 512;
    vb->vblk_status = (uint8_t) -1;

    // indirect descriptiors
    // Header descriptiors
    vb->indirect[0].addr = (uint64_t)&vb->vblk_rh; 
    vb->indirect[0].len = sizeof(vb->vblk_rh);
    vb->indirect[0].flags = VIRTQ_DESC_F_NEXT; 
    vb->indirect[0].next = 1;

    // Buffer descriptors
    vb->indirect[1].addr = (uint64_t)buf; 
    vb->indirect[1].len = bytecnt;
    vb->indirect[1].flags = VIRTQ_DESC_F_NEXT; 
    vb->indirect[1].next = 2;

    // Status descriptors
    vb->indirect[2].addr = (uint64_t)&vb->vblk_status; 
    vb->indirect[2].len = sizeof(vb->vblk_status);
    vb->indirect[2].flags = VIRTQ_DESC_F_WRITE; 
    vb->indirect[2].next = 0;

    vb->cond_done = 0;
    __sync_synchronize();
    
    vb->avail->ring[(vb->avail->idx) % 1] = 0;
    __sync_synchronize();

    vb->avail->idx = (vb->avail->idx) + 1;
    __sync_synchronize();
    
    virtio_notify_avail(vb->regs, 0);
    __sync_synchronize();

    // Sleep
    disable_interrupts();
    while(vb->cond_done == 0) {
        condition_wait(&vb->vblk_cond);
        disable_interrupts();
    }
    enable_interrupts();
    __sync_synchronize();
    // Unlock and restore inputs
    rwlock_release(&vb->vblk_lock);

    // Check if sucessful
    if (vb->vblk_status == VIRTIO_BLK_S_OK) {
        return bytecnt;
    }
    return -1;

}

/*
INPUTS: struct io * io - pointer to the io object to allow access of vioblock_device
        int op - indactes an int that alligned with an operation to be performed
        void * arg - pointer to the location that will store the resulting unsignined long long
OUTPUTS: returns 0 on success and -ENOTSUP, if the opperation can not be completed
DESCRIPTION: passes the block size in bytes to the arg * as type unsignined long long *
SIDE EFFECTS: no side effect
*/
int vioblk_ioctl(struct io * io, int op, void * arg) {
    struct vioblk_device * const vb = (void*)io - offsetof(struct vioblk_device, io);
    if (op == IOC_GETEND) {             
        *(unsigned long long *)arg = vb->regs->config.blk.capacity * 512;
        return 0;
    }
    return -ENOTSUP;
}

/*
INPUTS: int irqno - vioblock_device interupt source number
        void * aux - pointer to pass in the vioblock_device
OUTPUTS: no outputs
DESCRIPTION: executes the vioblock isr, acknowledges the interupt and updates the condition variable and the cond_done boolean
SIDE EFFECTS: interupt_ack updated; updates vioblock buffer indicators
*/
void vioblk_isr(int irqno, void * aux) {
    struct vioblk_device * const vb = aux;
    if (vb->regs->interrupt_status == 0) return;                        // no interupt called then leave the ISR
    vb->regs->interrupt_ack = vb->regs->interrupt_status;               // acknoledge the interupt

    if (vb->used->idx != vb->idx_prev_used) {                           // ignore if used is up to date
        vb->idx_prev_used = vb->used->idx;
        vb->cond_done = 1;
        condition_broadcast(&vb->vblk_cond);
    }
}