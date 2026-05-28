// rtc.c - Goldfish RTC driver
// 
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef RTC_TRACE
#define TRACE
#endif

#ifdef RTC_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "misc.h"
#include "device.h"
#include "console.h"
#include "string.h"
#include "heap.h"
#include "ioimpl.h"

#include "error.h"

#include <stdint.h>

// ---INTERNAL TYPE DEFINITIONS--- //

struct rtc_regs {
    uint32_t time_low;  // read first, latches time_high
    uint32_t time_high; //
};

struct rtc_device {
    volatile struct rtc_regs * regs;
    struct io io;
};

// ---INTERNAL FUNCTION DEFINITIONS--- //

static int rtc_open(struct io ** ioptr, void * aux);
static void rtc_reclaim(struct io * io);
static long rtc_read(struct io * io, void * buf, long bufsz);
static uint64_t read_real_time(volatile struct rtc_regs * regs);

// ---INTERNAL GLOBAL VARIABLES AND CONSTANTS--- //

static const struct iointf rtc_intf = {
    .implname = "rtc",
    .read = &rtc_read
};

// ---EXPORTED FUNCTION DEFINITIONS--- //
 
/*
INPUTS: void * mmio_base -
OUTPUTS:
DESCRIPTION:
SIDE EFFECTS:
*/
void attach_rtc(void * mmio_base) {
    struct rtc_device * rtc = kcalloc(1, sizeof(*rtc));         // completed in discussion
    rtc->regs = mmio_base;
    ioinit(&rtc->io, &rtc_intf, sizeof(uint64_t), 0);
    register_device("rtc", -1, &rtc_open, rtc);
}

/*
INPUTS: struct io ** ioptr -
        void * aux -
OUTPUTS:
DESCRIPTION:
SIDE EFFECTS:
*/
int rtc_open(struct io ** ioptr, void * aux) {  // takes the io object of the device pas by refrence and 
    struct rtc_device * rtc = aux;          // completed in discussion
    *ioptr = ioaddref(&rtc->io);
    return 0;
}

/*
INPUTS: struct io * io -
        void * buf -
        long bufsz - the size of the ring buffer
OUTPUTS: we return the number of succesfully read bytes as a long
DESCRIPTION: 
SIDE EFFECTS:
*/
long rtc_read(struct io * io, void * buf, long bufsz) {
    struct rtc_device * rtc = (void*)io - offsetof(struct rtc_device, io);     // completed in discussion
    uint64_t time;
    
    if (bufsz == 0) return 0;       // return the number of succefully read bytes

    time= read_real_time(rtc->regs);
    memcpy(buf,&time, sizeof(time));
    return sizeof(time);        // return the number of succefully read bytes
}

/*
INPUTS: volatile sturct rtc regs * regs -
OUTPUTS: 
DESCRIPTION:
SIDE EFFECTS:
*/
uint64_t read_real_time(volatile struct rtc_regs * regs) {
    uint32_t lo, hi;            // completed in discussion
    lo = regs->time_low;
    hi = regs->time_high;
    return ((uint64_t)hi << 32 ) | lo ;
}