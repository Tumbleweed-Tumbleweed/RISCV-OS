/*! @file viorng.c 
    @brief VirtIO rng device
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#include "virtio.h"
#include "intr.h"
#include "heap.h"
#include "error.h"
#include "string.h"
#include "thread.h"
#include "device.h"
#include "conf.h"
#include "intr.h"
#include "misc.h"
#include "ioimpl.h"

// ---INTERNAL CONSTANT DEFINITIONS--- //

#ifndef VIORNG_NAME
#define VIORNG_NAME "viorng"
#endif

#ifndef VIORNG_IRQ_PRIO
#define VIORNG_IRQ_PRIO 1
#endif

// ---INTERNAL TYPE DEFINITIONS--- //

struct viorng_device {
    // requestq;
    volatile struct virtio_mmio_regs * regs;
    int irqno;
    struct io io;
    struct virtq_desc * descriptor;
    struct virtq_avail * avail;             // driver to device communication
    struct virtq_used * used;               // device to driver communication
    unsigned int interupt_en;               // tracks if we have interupts currently enabled or not
    unsigned int buf_len;                   // tracks the current buffer length
    unsigned int buf_loc;                   // tracks the current buffer start location
    uint16_t idx_prev_used;                 // tracks last used idx 
    char buffer[512];                       // buffer that will hold our random data
    struct condition vrng_cond;             // condition variable for viorng for threading

};
// ---INTERNAL FUNCTION DECLARATIONS--- //

static int viorng_open(struct io ** ioptr, void * aux);

static void viorng_reclaim(struct io * io);

static long viorng_read(struct io * io, void * buf, long bufsz);

static void viorng_isr(int irqno, void * aux);

// ---INTERNAL GLOBAL VARIABLES--- //

static const struct iointf viorng_intf = {
    .implname = "viorng",
    .reclaim = &viorng_reclaim,
    .read = &viorng_read
};

// ---EXPORTED FUNCTION DEFINITIONS--- //

// The vioblk_attach function is declared and called from virtio_attach() in
// virtio.c when a VirtIO RNG device is found.

/*
INPUTS: volatile sturct virtio_mmio_regs * regs - pointer to this virtio devices MMIO registers
        int irqno - viorng_device interupt source number
OUTPUTS: no output
DESCRIPTION: attaches and negotiates viorng device to cpu
SIDE EFFECTS: viorng_device is attached upon success
*/
void viorng_attach(volatile struct virtio_mmio_regs * regs, int irqno) {
    static unsigned short instcnt = 0;          // number of viorng devices
    virtio_featset_t enabled_features, wanted_features, needed_features;
    struct viorng_device * vrng;
    int result;
    
    assert (regs->device_id == VIRTIO_ID_RNG);

    regs->status |= VIRTIO_STAT_DRIVER;             // Signal device that we found a driver
    __sync_synchronize();           // fence o,io

    virtio_featset_init(needed_features);
    virtio_featset_init(wanted_features);
    result = virtio_negotiate_features(regs,
        enabled_features, wanted_features, needed_features);

    if (result != 0) {
        debug("%p: virtio feature negotiation failed\n", regs);
        return;
    }

    vrng = kcalloc(1, sizeof(*vrng));               // intialize vrng
    vrng->regs = regs;
    vrng->irqno = irqno;
    vrng->interupt_en = 0;
    vrng->buf_len = 0;
    vrng->buf_loc = 0;
    vrng->idx_prev_used = 0;
        
    vrng->descriptor = kcalloc(1, sizeof(struct virtq_desc)); 
    vrng->descriptor->addr = (uint64_t)(vrng->buffer);               // descriptor initialization //?
    vrng->descriptor->len = sizeof(vrng->buffer);
    vrng->descriptor->flags = VIRTQ_DESC_F_WRITE;
    vrng->descriptor->next = 0;

    vrng->avail = kcalloc(1, sizeof(struct virtq_avail) + sizeof(uint16_t));        // sturct + ring
    vrng->avail->flags = 0;             // avail initialization
    vrng->avail->idx = 0;
    vrng->avail->ring[0] = 0;

    vrng->used = kcalloc(1, sizeof(struct virtq_used)+ sizeof(struct virtq_used_elem));         // struct + virtq_used_elem
    vrng->used->flags = 0;              // used initialization
    vrng->used->idx = 0;
    //vrng->used->ring[0]->id = 999;
    condition_init(&vrng->vrng_cond,"viorng_condition");

    regs->status |= VIRTIO_STAT_FEATURES_OK;
    assert(regs->status & VIRTIO_STAT_FEATURES_OK);
    
    virtio_attach_virtq(regs, 0, 1, (uint64_t)vrng->descriptor, (uint64_t)vrng->used, (uint64_t)vrng->avail);
    virtio_enable_virtq(regs, 0);       // hard code to 0 becuase we only have 1 so its just the first

    regs->status |= VIRTIO_STAT_DRIVER_OK;          //set the driver to OK
    __sync_synchronize();           // fence o,oi

    register_device(VIORNG_NAME, instcnt++, &viorng_open, vrng);
    ioinit(&vrng->io, &viorng_intf, 1, 0);          // check block size, ref count and the first parameter
}

/*
INPUTS: struct io * io - pointer to the io pointer
        void * aux passes in the viorng_device
OUTPUTS: returns 0 to indicate successful opening
DESCRIPTION: prepares the viorng_device passed for I/O functionality
SIDE EFFECTS: *ioptr contains a pointer to the viorng_device I/O object
*/
int viorng_open(struct io ** ioptr, void * aux) {
    struct viorng_device * const vrng = aux;
    trace("%s()", __func__);

    if(vrng->interupt_en != 1){         // as the same vrng can be opened many times we only do this the first time it is opened
        enable_intr_source(vrng->irqno, VIORNG_IRQ_PRIO, viorng_isr, vrng);
        vrng->interupt_en = 1;
    }
    *ioptr = ioaddref(&vrng->io);
    return 0;
}

/*
INPUTS: struct io * io - pointer to io object
OUTPUTS: no output
DESCRIPTION: when reclaim is called interupts are disabled from the passed viorng_device; we do not reclaim memory!
SIDE EFFECTS: the passed viorng_device from the io will no longer be able signal interupts
*/
void viorng_reclaim(struct io * io) {
    struct viorng_device * const vrng = (void*)io - offsetof(struct viorng_device, io);
    vrng->interupt_en = 0;
    disable_intr_source(vrng->irqno);
}

/*
INPUTS: struct io * io - pointer to io object
        void * buf - pointer to the buffer
        long bufsz - length of buffer size to read
OUTPUTS: returns the number of characters read
DESCRIPTION: random data from the device is placed into the viorng buffer until there is enough data for bufsz to fill the passed buf
SIDE EFFECTS: avail ring and index is updated; vrng buf data is updated; random data is passed into passed buffer
*/
long viorng_read(struct io * io, void * buf, long bufsz) {
    struct viorng_device * const vrng = (void*)io - offsetof(struct viorng_device, io);
    long read = 0;
    char * c = buf;         // character pointer for passed buffer
    if (bufsz == 0) return read;

    while((read < bufsz)) {         // read enough to fill bufsz
        disable_interrupts();
        if (vrng->buf_len == 0){            // if our buf is empty ask device for more data
            vrng->avail->ring[(vrng->avail->idx) % 1] = 0;
            vrng->avail->idx = (vrng->avail->idx) + 1;
            virtio_notify_avail(vrng->regs, 0);
            while(vrng->buf_len == 0) {
                condition_wait(&vrng->vrng_cond);         // condition wait till dat is recieved
                disable_interrupts();
            }
        }
        
        c[read] = vrng->buffer[vrng->buf_loc];       // essebtailly same as uart
        vrng->buf_loc++;            // increment buff location
        vrng->buf_len--;            // decrement buff length
        read++;
        enable_interrupts();
    }
    return read;
}

/*
INPUTS: int irqno - viorng_device interupt source number
        void * aux - pointer to pass in the viorng_device
OUTPUTS: no output
DESCRIPTION: executes the entropy device isr, acknowledges the interupt and updates variables based on number of bytes device wrote
SIDE EFFECTS: interupt_ack updated; updates vrngs buffer indicators
*/
void viorng_isr(int irqno, void * aux) {
    struct viorng_device * const vrng = aux;
    uint32_t curr_status = vrng->regs->interrupt_status;    // determine interupt and save
    if (curr_status == 0) return;

    vrng->regs->interrupt_ack = curr_status;                // acknoledge the interupt

    if (vrng->used->idx != vrng->idx_prev_used) {                             // ignore if called when read yet to be used
        vrng->idx_prev_used = vrng->used->idx;
        vrng->buf_loc = 0;
        vrng->buf_len = vrng->used->ring[0].len;            // set our current buffer length to the length of our virtq used
        condition_broadcast(&vrng->vrng_cond);
    }
}
