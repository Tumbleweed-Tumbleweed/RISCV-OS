// io.c - Generic I/O objects
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef IO_TRACE
#define TRACE
#endif

#ifdef IO_DEBUG
#define DEBUG
#endif

#include "memory.h"
#include "io.h"
#include "ioimpl.h"
#include <stddef.h>
#include "error.h"
#include "heap.h"
#include "misc.h"
#include "string.h"
#include "thread.h"
#include "intr.h"

// ---IO EXPORTED FUNCTION DEFINITIONS--- //
struct io * ioinit (struct io * io, const struct iointf * intf, unsigned int blksz, unsigned int refcnt) {
    io->intf = intf;
    io->blksz = blksz;
    io->refcnt = refcnt;
    return io;
}

unsigned int ioblksz(const struct io * io) {
    return io->blksz;
}

unsigned int iorefcnt(const struct io * io) {
    return io->refcnt;
}

struct io * ioaddref(struct io * io) {
    io->refcnt += 1;

    if (io->refcnt == 0)
        panic("Too many io refs");
    
    return io;
}

void iodropref(struct io * io) {
    assert (io->refcnt != 0);
    io->refcnt -= 1;

    if (io->refcnt == 0 && io->intf->reclaim != NULL)
        io->intf->reclaim(io);
}

long ioread(struct io * io, void * buf, long bufsz) {
    assert (io != NULL);
    assert (io->refcnt != 0);
    assert (buf != NULL || bufsz == 0);

    if (io->intf->read == NULL)
        return -ENOTSUP;
    
    if (bufsz < 0)
        return -EINVAL;
    
    if (bufsz != 0 && bufsz < io->blksz)
        return -EINVAL;
    
    return io->intf->read(io, buf, ROUND_DOWN(bufsz, io->blksz));
}

/*
INPUTS: struct io * io -
        void * buf -
        long bufsz -
OUTPUTS: no outputs
DESCRIPTION: 
SIDE EFFECTS: 
*/
long iofill(struct io * io, void * buf, long bufsz) {
    // @TODO YOUR CODE HERE
    return 0;
}

int iogetc(struct io * io) {
    long rlen;
    unsigned char c;

    rlen = ioread(io, &c, 1);
    return (rlen != 1) ? -1 : c;
}

long iowrite(struct io * io, const void * buf, long buflen) {
    assert (io != NULL);
    assert (io->intf != NULL);
    assert (io->refcnt != 0);
    assert (buf != NULL || buflen == 0);

    if (io->intf->write == NULL)
        return -ENOTSUP;
    
    if (buflen < 0)
        return -EINVAL;
    
    if (buflen != 0 && buflen < io->blksz)
        return -EINVAL;
    
    return io->intf->write(io, buf, ROUND_DOWN(buflen, io->blksz));
}

int ioputc(struct io * io, char c) {
    long wlen;

    wlen = iowrite(io, &c, 1);
    return (wlen != 1) ? -1 : (unsigned char)c;
}

long iofetch(struct io * io, unsigned long long pos, void * buf, long buflen) {
    assert (io != NULL);
    assert (io->intf != NULL);
    assert (io->refcnt != 0);
    assert (buf != NULL || buflen == 0);

    if (io->intf->fetch == NULL)
        return -ENOTSUP;
    
    if (buflen < 0)
        return -EINVAL;
    
    if (buflen != 0 && buflen < io->blksz)
        return -EINVAL;
    
    if (pos % io->blksz != 0 || buflen % io->blksz != 0)
        return -EINVAL;
    
    return io->intf->fetch(io, pos, buf, buflen);
}

long iostore(struct io * io, unsigned long long pos, const void * buf, long buflen) {
    assert (io != NULL);
    assert (io->intf != NULL);
    assert (io->refcnt != 0);
    assert (buf != NULL || buflen == 0);

    if (io->intf->store == NULL)
        return -ENOTSUP;
    
    if (buflen < 0)
        return -EINVAL;
    
    if (buflen != 0 && buflen < io->blksz)
        return -EINVAL;
    
    if (pos % io->blksz != 0 || buflen % io->blksz != 0)
        return -EINVAL;
    
    return io->intf->store(io, pos, buf, buflen);
}

int ioctl(struct io * io, int op, void * arg) {
    assert (io != NULL);
    assert (io->refcnt != 0);

    if (op == IOC_GETBLKSZ)
        return io->blksz;
    
    if (io->intf->ioctl != NULL)
        return io->intf->ioctl(io, op, arg);
    else
        return -ENOTSUP;
}

int ioctl_u(struct io * io, int op, uintptr_t u_arg) {
    assert (io != NULL);
    assert (io->intf != NULL);
    assert (io->refcnt != 0);    
    
    if (io->intf->ioctl_u != NULL)
        return io->intf->ioctl_u(io, op, u_arg);
    else
        return -ENOTSUP;
}

// ---SEEKIO EXPORTED FUNCTION DEFINITIONS--- //
struct io * seekio_init (struct seekio * sio, const struct iointf * intf, unsigned int blksz, unsigned int refcnt) {
    sio->pos = 0;
    return ioinit(&sio->base, intf, blksz, refcnt);
}

long seekio_read(struct io * io, void * buf, long bufsz) {
    struct seekio * const sio = (struct seekio*)io;
    int result;
    long retlen;
    unsigned long long end;
    trace("%s(io=%s,bufsz=%ld)", __func__, io->intf->implname, bufsz);

    assert (sio->pos % io->blksz == 0);

    if (io->intf->fetch == NULL)
        return -ENOTSUP;
    
    result = ioctl(io, IOC_GETEND, &end);
    if (result < 0)
        return result;

    if (sio->pos == end)
        return 0;

    if (sio->pos > end)
        return -EINVAL;
    
    if (end - sio->pos < bufsz)
        bufsz = end - sio->pos;
    
    debug("Calling fetch with pos=%llu, bufsz=%ld", sio->pos, bufsz);
    retlen = io->intf->fetch(&sio->base, sio->pos, buf, bufsz);

    if (retlen <= 0)
        return retlen;
    
    assert (retlen == bufsz);
    sio->pos += retlen;
    return retlen;
}

long seekio_write(struct io * io, const void * buf, long len) {
    struct seekio * const sio = (struct seekio*)io;
    int result;
    long retlen;
    unsigned long long end, new_end;

    if (io->intf->store == NULL)
        return -ENOTSUP;
    
    result = ioctl(io, IOC_GETEND, &end);
    if (result < 0)
        return result;

    if (sio->pos + len > end) {
        new_end = sio->pos + len;
        if (ioctl(io, IOC_SETEND, &new_end) == 0)
            len = new_end - sio->pos;
        else if (sio->pos > end)
            return -EINVAL;
        else
            len = end - sio->pos;
    }
    
    if (len == 0)
        return 0;
    
    retlen = io->intf->store(&sio->base, sio->pos, buf, len);

    if (retlen <= 0)
        return retlen;
    
    assert (retlen == len);
    sio->pos += retlen;
    return retlen;
}

int seekio_ioctl(struct io * io, int op, void * arg) {
    struct seekio * const sio = (struct seekio*)io;
    unsigned long long * const ullarg = arg;

    switch (op) {
    case IOC_GETPOS:
        *ullarg = sio->pos;
        return 0;

    case IOC_SETPOS:
        if (*ullarg % io->blksz != 0)
            return -EINVAL;
        sio->pos = *ullarg;
        return 0;
    
    default:
        return -ENOTSUP;
    }
}

// ---NULLIO INTERNAL FUNCTION DECLARATIONS--- //
static long nullio_read(struct io * io, void * buf, long bufsz);
static long nullio_write(struct io * io, const void * buf, long len);
static long nullio_fetch(struct io * io, unsigned long long pos, void * buf, long len);
static long nullio_store(struct io * io, unsigned long long pos, const void * buf, long len);

// ---NULLIO INTERNAL GLOBAL VARIABLES--- //
static const struct iointf nullio_intf = {
    .implname = "null",
    .read = &nullio_read,
    .write = &nullio_write,
    .fetch = &nullio_fetch,
    .store = &nullio_store
};

static struct io nullio = {
    .intf = &nullio_intf,
    .blksz = 1,
    .refcnt = 0
};

// ---NULLIO INTERNAL FUNCTION DEFINITIONS--- //
struct io * create_nullio(void) {
    return ioaddref(&nullio);
}

long nullio_read(struct io * io, void * buf, long bufsz) {
    (void)io;
    (void)buf;
    (void)bufsz;
    return 0;
}

long nullio_write(struct io * io, const void * buf, long len) {
    (void)io;
    (void)buf;
    (void)len;
    return 0;
}

long nullio_fetch(struct io * io, unsigned long long pos, void * buf, long len) {
    assert (io == &nullio);
    (void)buf;

    if (pos != 0 || len != 0)
        return -EINVAL;
    
    return 0;
}

long nullio_store(struct io * io, unsigned long long pos, const void * buf, long len) {
    assert (io == &nullio);
    (void)buf;

    if (pos != 0 || len != 0)
        return -EINVAL;
    
    return 0;
}

// ---MEMIO INTERNAL TYPE DEFINITIONS--- //
struct memio {
    struct seekio base;
    unsigned long long end;
    void * buf;
    void (*reclfn)(void*,size_t);
};

// ---MEMIO INTERNAL FUNCTION DECLARATIONS--- //
static void memio_reclaim(struct io * io);
static long memio_fetch(struct io * io, unsigned long long pos, void * buf, long len);
static long memio_store(struct io * io, unsigned long long pos, const void * buf, long len);
static int memio_ioctl(struct io * io, int op, void * arg);

// ---MEMIO INTERNAL CONSTANT DEFINITIONS--- //
static const struct iointf memio_intf = {
    .implname = "memio",
    .reclaim = &memio_reclaim,
    .read = &seekio_read,
    .write = &seekio_write,
    .fetch = &memio_fetch,
    .store = &memio_store,
    .ioctl = &memio_ioctl
};

// ---MEMIO EXPORTED FUNCTION DEFINITIONS--- //
struct io * create_memio (void * buf, size_t size, void(*reclfn)(void*,size_t)) {
    struct memio * mio;
    
    assert (buf != NULL || size == 0);
    mio = kcalloc(1, sizeof(*mio));
    mio->buf = buf;
    mio->end = size;
    mio->reclfn = reclfn;

    return seekio_init (
        &mio->base, &memio_intf,
        /* blksz */ 1, /* refcnt */ 1);
}

// ---MEMIO INTERNAL FUNCTION DEFINITIONS--- //
void memio_reclaim(struct io * io) {
    struct memio * const mio = (struct memio*)io;

    if (mio->reclfn != NULL)
        mio->reclfn(mio->buf, mio->end);
    
    kfree(mio);
}

long memio_fetch(struct io * io, unsigned long long pos, void * buf, long len) {
    struct memio * const mio = (struct memio*)io;

    assert (io != NULL);
    assert (pos % io->blksz == 0); // ensured by iofetch()
    assert (len % io->blksz == 0); // ensured by iofetch()
    assert (0 <= len); // ensured by iofetch()

    if (mio->end < pos || mio->end - pos < len)
        return -EINVAL;
    
    memcpy(buf, mio->buf + pos, len);
    return len;
}

long memio_store(struct io * io, unsigned long long pos, const void * buf, long len) {
    struct memio * const mio = (struct memio*)io;

    assert (io != NULL);
    assert (pos % io->blksz == 0); // ensured by iostore()
    assert (len % io->blksz == 0); // ensured by iostore()
    assert (0 <= len); // ensured by iostore()

    if (mio->end < pos || mio->end - pos < len)
        return -EINVAL;
    
    memcpy(mio->buf + pos, buf, len);
    return len;
}

int memio_ioctl(struct io * io, int op, void * arg) {
    struct memio * const mio = (struct memio*)io;
    unsigned long long * const ullarg = arg;

    switch (op) {
    case IOC_GETPOS:
    case IOC_SETPOS:
        return seekio_ioctl(io, op, arg);
    
    case IOC_GETEND:
        *ullarg = mio->end;
        return 0;
    
    default:
        return -ENOTSUP;
    }

}

// ---IOPIPE INTERNAL TYPE DEFINITIONS--- //
struct iopipe {
    struct io wio;
    struct io rio;
    volatile unsigned short wpos;
    volatile unsigned short rpos;
    volatile char wopen;
    volatile char ropen;
    struct rwlock wlock;
    struct condition r_updated;
    struct condition w_updated;
    void * buf;                                 // pipe ring buffer
};

// ---IOPIPE INTERNAL FUNCTION DECLARATIONS--- //s
static void iopipe_wio_reclaim(struct io * io);
static void iopipe_rio_reclaim(struct io * io);
static long iopipe_write(struct io * io, const void * buf, long len);
static long iopipe_read(struct io * io, void * buf, long bufsz);
static void iopipe_reclaim(struct iopipe * p);

// ---IOPIPE INTERNAL CONSTANT DEFINITIONS--- //
static const struct iointf iopipe_writer_intf = {
    .implname = "iopipe_wio",
    .reclaim = &iopipe_wio_reclaim,
    .write = &iopipe_write
};

static const struct iointf iopipe_reader_intf = {
    .implname = "iopipe_rio",
    .reclaim = &iopipe_rio_reclaim,
    .read = &iopipe_read
};

// ---IOPIPE EXTERNAL FUNCTION DEFINITIONS--- //

/*
INPUTS: struct io ** wioptr - pointer to the pointer writer io 
        struct io ** rioptr - pointer to the pointer read io
OUTPUTS: no outputs
DESCRIPTION: creates a one way pipe with a buffer the size of a page
SIDE EFFECTS: allocates memory
*/
void create_iopipe(struct io ** wioptr, struct io ** rioptr) {
    assert(wioptr != NULL && rioptr != NULL);

    struct iopipe * pipe = kcalloc(1, sizeof(*pipe));
    if (pipe == NULL) {                                         // if memory allocation fails set wioptr and rioptr NULL
        *wioptr = NULL;
        *rioptr = NULL;
        return;
    }

    pipe->buf = alloc_phys_page();                              // ring buffer should be size of pages
    if (pipe->buf == NULL) {
        kfree(pipe);
        *wioptr = NULL;
        *rioptr = NULL;
        return;
    }

    pipe->wpos = 0;                                             // initialize variables
    pipe->rpos = 0;
    pipe->wopen = 1;
    pipe->ropen = 1;
    rwlock_init(&pipe->wlock, "pipe_w_lock");
    condition_init(&pipe->w_updated, "pipe_w_upd");
    condition_init(&pipe->r_updated, "pipe_r_upd");

    ioinit(&pipe->wio, &iopipe_writer_intf, 1, 1);              // intitialize the io objects 
    ioinit(&pipe->rio, &iopipe_reader_intf, 1, 1);

    *wioptr = &pipe->wio;                                       // set pointers
    *rioptr = &pipe->rio;

}

// ---IOPIPE INTERNAL FUNCTION DEFINITIONS--- //

/*
INPUTS: struct io * io - pointer to writer io object
OUTPUTS: no outputs
DESCRIPTION: marks the writer portion of a pipe closed; if reader is closed as well, reclaim the pipe
SIDE EFFECTS: may free memory
*/
void iopipe_wio_reclaim(struct io * io) {
    struct iopipe * pipe = (struct iopipe *)((char*)io - offsetof(struct iopipe, wio));
    int pie = disable_interrupts();
    pipe->wopen = 0;

    condition_broadcast(&pipe->w_updated);                  // broadcast write as no more can be written been closed
    condition_broadcast(&pipe->r_updated);                  // broadcast read as no more is being wrriten when write closed

    if (pipe->ropen == 0) {                                 // if read is already closed we can free pipe
        restore_interrupts(pie);
        iopipe_reclaim(pipe);
        return;
    }
    restore_interrupts(pie);
}

/*
INPUTS: struct io * io - pointer to reader io object
OUTPUTS: no outputs
DESCRIPTION: marks the reader portion of a pipe closed; if writer is closed as well, reclaim the pipe
SIDE EFFECTS: may free memory
*/
void iopipe_rio_reclaim(struct io * io) {
    struct iopipe * pipe = (struct iopipe *)((char*)io - offsetof(struct iopipe, rio));
    int pie = disable_interrupts();
    pipe->ropen = 0;

    condition_broadcast(&pipe->w_updated);                  // let write know there is nothing more we will read
    
    if (pipe->wopen == 0) {                                 // if write is already closed we can free pipe
        restore_interrupts(pie);
        iopipe_reclaim(pipe);
        return;
    }
    restore_interrupts(pie);
}

/*
INPUTS: struct io * io - pointer to writer io object
        const void * buf - buf to write from
        long buflen - number of bytes to write
OUTPUTS: returns bytes written on success; negative error code on failure
DESCRIPTION: write bytes into the pipe from the buffer
SIDE EFFECTS: may condition wait and therefore yield
*/
long iopipe_write(struct io * io, const void * buf, long buflen) {
    struct iopipe * pipe = (struct iopipe *)((char*)io - offsetof(struct iopipe, wio));
    const char * input = (const char *)buf;
    long written = 0;
    
    if (buflen < 0 || buf == NULL) return -EINVAL;                     // ensure passed parameters are valid
    if (buflen == 0) return written;

    rwlock_acquire(&pipe->wlock, 1);
    int pie = disable_interrupts();

    while (written < buflen) {                                      // loop as long as there is space to write tos
        if (pipe->ropen == 0){
            restore_interrupts(pie);
            rwlock_release(&pipe->wlock);
            if (written == 0) return -EPIPE;                        // if pipe is broken and nothing was written return an error otherwise return how much was written
            return written;
        }

        unsigned int curr_wpos = pipe->wpos;
        unsigned int ring_space = (PAGE_SIZE - 1) - ((unsigned int)(curr_wpos - pipe->rpos) % PAGE_SIZE);
        
        if (ring_space == 0 ) {                                 // if the ring buffer is full we condition wait until we have space
            condition_wait(&pipe->r_updated);
            continue;
        }

        unsigned int writeable = buflen - written;
        if (writeable > ring_space) writeable = ring_space;
        unsigned int page_end = PAGE_SIZE - curr_wpos;

        if (writeable <= page_end) {                          // if all writeable fits without wrapping write in one line
            memcpy((char *)pipe->buf + curr_wpos, input + written, writeable);
        } else {                                                // if all writeable needs to wrap page split memcpy
            memcpy((char*)pipe->buf + curr_wpos, input + written, page_end);
            memcpy((char*)pipe->buf, input + written + page_end, writeable - page_end);
        }

        pipe->wpos = ((curr_wpos + writeable) % (PAGE_SIZE));
        written += writeable;
        condition_broadcast(&pipe->w_updated);
    }

    restore_interrupts(pie);
    rwlock_release(&pipe->wlock);
    return written;
}

/*
INPUTS: struct io * io - pointer to reader io object
        void * buf - buf to read to
        long bufsz - number of bytes to read
OUTPUTS: returns bytes read on success; negative error code on failures
DESCRIPTION: reads byte from the pipe into the buffer
SIDE EFFECTS: may condition wait and therefore yield
*/
long iopipe_read(struct io * io, void * buf, long bufsz) {
    struct iopipe * pipe = (struct iopipe *)((char*)io - offsetof(struct iopipe, rio));
    char * output = (char *)buf;

    if (bufsz < 0 || buf == NULL) return -EINVAL;                     // ensure passed parameters are valid
    if (bufsz == 0) return 0;

    int pie = disable_interrupts();

    while (0 == 0){
        unsigned int curr_rpos = pipe->rpos;                                                // holds current read position
        unsigned int ring_unread = ((unsigned int)(pipe->wpos - curr_rpos) % PAGE_SIZE);            // holds the diffrence between wpos and rpos which is how much has yet to be read

        if (ring_unread > 0) {                // if pipe is readable, read all possible data
            unsigned int readable = bufsz;
            if (readable > ring_unread) readable = ring_unread;                 // the most we can read is MIN(bufsz, unread on buf)
            unsigned int page_end = PAGE_SIZE - curr_rpos;

            if (readable <= page_end) {                                         // if amount readable does not reach end no need to wrap
                memcpy(output, (char*)pipe->buf + curr_rpos, readable);
            } else {                                                            // if we need to read and loop back seprate memcpy
                memcpy(output, (char*)pipe->buf + curr_rpos, page_end);
                memcpy(output + page_end, (char*)pipe->buf, readable - page_end);
            }

            pipe->rpos = ((curr_rpos + readable) % (PAGE_SIZE));            // updated rpos and signal that un read pipe bytes have been read
            condition_broadcast(&pipe->r_updated);
            restore_interrupts(pie);
            return readable;
        }

        if (pipe->wopen == 0) {               // check if writer pipe closed, if it is no more will ever be written so nothing more can be read
            restore_interrupts(pie);
            return 0;
        }

        condition_wait(&pipe->w_updated);    // wait until writer updates the pipe buf
    }

    return 0;
}

/*
INPUTS: struct iopipe * p - pipe to be reclaimed
OUTPUTS: no outputs
DESCRIPTION: if the reader and writer pipe ends are closed then the pipe itself can be cleaned up
SIDE EFFECTS: memory will be freed on successes
*/
void iopipe_reclaim(struct iopipe * p) {
    if (p == NULL) return;
    if (p->buf != NULL) free_phys_page(p->buf);
    kfree(p);
}
