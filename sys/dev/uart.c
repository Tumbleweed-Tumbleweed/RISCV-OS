// uart.c - NS8550-compatible UART
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef UART_TRACE
#define TRACE
#endif

#ifdef UART_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "intr.h"
#include "heap.h"
#include "thread.h"
#include "console.h"
#include "device.h"
#include "misc.h"
#include "ioimpl.h"
#include "error.h"

#include <stdint.h>


// ---COMPILE-TIME CONSTANT DEFINITIONS--- //

#ifndef UART_RBUFSZ
#define UART_RBUFSZ 64
#endif

#ifndef UART_INTR_PRIO
#define UART_INTR_PRIO 1
#endif

#ifndef UART_DEVNAME
#define UART_DEVNAME "uart"
#endif


// ---INTERNAL TYPE DEFINITIONS--- //

struct uart_regs {
    union {
        char rbr; // DLAB=0 read
        char thr; // DLAB=0 write
        uint8_t dll; // DLAB=1
    };
    
    union {
        uint8_t ier; // DLAB=0
        uint8_t dlm; // DLAB=1
    };
    
    union {
        uint8_t iir; // read
        uint8_t fcr; // write
    };

    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr;
    uint8_t msr;
    uint8_t scr;
};

#define LCR_DLAB (1 << 7)
#define LSR_OE (1 << 1)
#define LSR_DR (1 << 0)
#define LSR_THRE (1 << 5)
#define IER_DRIE (1 << 0)
#define IER_THREIE (1 << 1)

#define EBUSY 2

// ---Simple fixed-size ring buffer--- //

struct ringbuf {
    unsigned int hpos; // head of queue (from where elements are removed)
    unsigned int tpos; // tail of queue (where elements are inserted)
    char data[UART_RBUFSZ];
};

// ---UART device structure--- //

struct uart_device {
    volatile struct uart_regs * regs;
    int irqno;          // containt the srcno we use when registering with plic

    struct io io;       // essentially io is a virtual class passed onto uart

    struct ringbuf rxbuf;       // receive ring buffer
    struct ringbuf txbuf;       // transmit ring buffer

    struct condition rx_cond;       // condition for rx_buffer
    struct condition tx_cond;       // condition for tx_buffer
    struct rwlock tx_lock;          // lock for the tx_buffer
};

// ---INTERNAL FUNCTION DEFINITIONS--- //

static int uart_open(struct io ** ioptr, void * aux);
static void uart_reclaim(struct io * io);
static long uart_read(struct io * io, void * buf, long bufsz);
static long uart_write(struct io * io, const void * buf, long len);
static void uart_isr(int srcno, void * aux);

// ---Ring buffer (struct rbuf) functions--- //

static void rbuf_init(struct ringbuf * rbuf);
static int rbuf_empty(const struct ringbuf * rbuf);
static int rbuf_full(const struct ringbuf * rbuf);
static void rbuf_putc(struct ringbuf * rbuf, char c);
static char rbuf_getc(struct ringbuf * rbuf);

// ---INTERNAL GLOBAL VARIABLES--- //

static const struct iointf uart_intf = {
    .implname = "uart",
    .read = &uart_read,
    .write = &uart_write,
    .reclaim = &uart_reclaim
};

// ---EXPORTED FUNCTION DEFINITIONS--- //

/*
INPUTS: void * mmio_base -
        int irqno - interupt source number
OUTPUTS: no output
DESCRIPTION:
SIDE EFFECTS:
*/
void attach_uart(void * mmio_base, int irqno) {
    static unsigned short instcnt = 0; // number of UARTs
    struct uart_device * uart;

    uart = kcalloc(1, sizeof(*uart));

    uart->regs = mmio_base;
    uart->irqno = irqno;

    condition_init(&uart->rx_cond, "uart_rx_cond");
    condition_init(&uart->tx_cond, "uart_tx_cond");
    rwlock_init(&uart->tx_lock, "uart_tx_lock");

    // Initialize hardware device

    uart->regs->ier = 0;
    uart->regs->lcr = LCR_DLAB;
    // fence o,o ?
    uart->regs->dll = 0x01;
    uart->regs->dlm = 0x00;
    // fence o,o ?
    uart->regs->lcr = 0; // DLAB=0

    register_device(UART_DEVNAME, instcnt++, &uart_open, uart);
    ioinit(&uart->io, &uart_intf, 1, 0);
}


/*
INPUTS: struct io ** ioptr - pointer to the io pointer
        void * aux - pointer to the uart_device
OUTPUTS: returns 0 to indicate successful opening
DESCRIPTION: prepares the uart_device passed for I/O functionality
SIDE EFFECTS: *ioptr contains a pointer to the uart_device I/O object
*/
int uart_open(struct io ** ioptr, void * aux) {
    struct uart_device * const uart = aux;
    trace("%s()", __func__);

    if (iorefcnt(&uart->io) != 0) return -EBUSY;
    
    rbuf_init(&uart->rxbuf);            // Reset receive and transmit buffers
    rbuf_init(&uart->txbuf);

    uart->regs->rbr;            // Read RBR to flush any stale data in hardware buffer
                                // forces a read because uart->regs is volatile

    uart->regs->ier = IER_DRIE;           // set IER DRIE bit to 1 to indicate intreupts enabled

    enable_intr_source(uart->irqno, UART_INTR_PRIO, uart_isr, uart);            

    *ioptr = ioaddref(&uart->io);       // sets pointer to uart I/O object 
    return 0;
}


/*
INPUTS: struct io * io - pointer to io object
OUTPUTS: no output
DESCRIPTION: when reclaim is called interupts are disabled from the passed uart_device
SIDE EFFECTS: the passed viorng_device from the io will no longer be able to signal interupts
*/
void uart_reclaim(struct io * io) {
    struct uart_device * const uart = (void*)io - offsetof(struct uart_device, io);
    trace("%s()", __func__);
    uart->regs->ier = 0;        // turn off interupt enable bit
    disable_intr_source(uart->irqno);
}


/*
INPUTS: struct io * io - pointer to our io structure
        void * buf - pointer to the buffer we read tos
        long bufsz - total number of chars to read
OUTPUTS: outputs the total number of read chars
DESCRIPTION: read from our rxbuf into the passed buf
SIDE EFFECTS:
*/
long uart_read(struct io * io, void * buf, long bufsz) {
    struct uart_device * const uart = (void*)io - offsetof(struct uart_device, io);
    long read = 0;
    if (bufsz == 0) return read;
    char * c = buf;

    while(read < bufsz) {               // cut down and only check for read < bufsz as we will never have an empty buf to read from
        disable_interrupts();
        while(rbuf_empty(&uart->rxbuf)) condition_wait(&uart->rx_cond);       // condition_wait until we have data to read
        c[read] = rbuf_getc(&uart->rxbuf);
        read++;
        enable_interrupts();
    }
    
    if (read > 0) uart->regs->ier |= IER_DRIE;
    return read;
}


/*
INPUTS: struct io * io - pointer to io object
        const void * buf - pointer to the ring buffer
        long buflen - length of the ring buffer
OUTPUTS: returns the number of bytes written from the ring buffer
DESCRIPTION: writes all characters in the passed buffer to the tranmistion buffer
SIDE EFFECTS:
*/
long uart_write(struct io * io, const void * buf, long buflen) {
    struct uart_device * const uart = (void*)io - offsetof(struct uart_device, io);
    long written = 0;
    if (buflen == 0) return written;
    const char * c = buf;
    rwlock_acquire(&uart->tx_lock, 1);          // writer = 1, we need the lock exclusive

    while(written < buflen){        // ensure we dont go beyond ring buffer
        disable_interrupts();
        while(rbuf_full(&uart->txbuf)) condition_wait(&uart->tx_cond);            // spin wait
        rbuf_putc(&uart->txbuf, c[written]);
        written++;
        if (written > 0) uart->regs->ier |= IER_THREIE;         // set THRE to 1 to indicate data is ready to be written
        enable_interrupts();
    }
    rwlock_release(&uart->tx_lock);
    return written;
}


/*
INPUTS: int srcno - uart_device interupt source number
        void * aux - pointer to pass in the uart_device
OUTPUTS: no output
DESCRIPTION: this isr either reads from rbr, writes to thr or and ends the interupt service routine
SIDE EFFECTS: iers THRE bit is set turned off if all bits from txbuf are written
*/
void uart_isr(int srcno, void * aux) {
    struct uart_device * const uart = aux;
    if(((uart->regs->lsr & LSR_DR) != 0)){     // if data is ready to be read place into rxbuf if not full
        char c = uart->regs->rbr;
        if(!rbuf_full(&uart->rxbuf)){
            rbuf_putc(&uart->rxbuf, c);
            condition_broadcast(&uart->rx_cond);
        }
    }

    if(((uart->regs->lsr & LSR_THRE) != 0)){     // while data is ready to be read place into rxbuf if not full
        if(!rbuf_empty(&uart->txbuf)){
            uart->regs->thr = rbuf_getc(&uart->txbuf);
            condition_broadcast(&uart->tx_cond);
        }
        if (rbuf_empty(&uart->txbuf)) uart->regs->ier &= ~IER_THREIE;       // set THRE to 0 as the buffer is now empty
    }

}

// ---GIVEN RING BUFFER FUNCTIONALITY--- //

void rbuf_init(struct ringbuf * rbuf) {
    rbuf->hpos = 0;
    rbuf->tpos = 0;
}

int rbuf_empty(const struct ringbuf * rbuf) {
    return (rbuf->hpos == rbuf->tpos);
}

int rbuf_full(const struct ringbuf * rbuf) {
    return (rbuf->tpos - rbuf->hpos == UART_RBUFSZ);
}

void rbuf_putc(struct ringbuf * rbuf, char c) {
    uint_fast16_t tpos;

    tpos = rbuf->tpos;
    rbuf->data[tpos % UART_RBUFSZ] = c;
    asm volatile ("" ::: "memory");
    rbuf->tpos = tpos + 1;
}

char rbuf_getc(struct ringbuf * rbuf) {
    uint_fast16_t hpos;
    char c;

    hpos = rbuf->hpos;
    c = rbuf->data[hpos % UART_RBUFSZ];
    asm volatile ("" ::: "memory");
    rbuf->hpos = hpos + 1;
    return c;
}