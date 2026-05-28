// io.h - User I/O Constants
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _IO_H_
#define _IO_H_

#include "error.h"
#include "string.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

struct io {
    const struct iointf * intf;

    // The /intf/ member is a pointer to /iointf/ structure, defined below, that
    // contains the I/O object implementation name and pointers to functions
    // implementing the I/O object operations.

    unsigned int blksz;

    // All I/O operations operate on units of /blksz/ bytes. For UARTs, files,
    // and pipes, this unit is 1 byte. For block storage devices such as vioblk,
    // the unit is the block size. The value 0 is reserved and must not be used.
    // Values greater than INT_MAX are reserved and must not be used.

    unsigned int refcnt;

    // I/O object lifetimes are managed using a reference count system. The
    // /refcnt/ member stores the number of independent references to the I/O
    // object. See the ioaddref() and iodropref() functions declared and
    // documented in io.h. When a reference count decremented by iodropref()
    // becomes 0, iodropref() calls the /reclaim/ function of the I/O object.
    //
    // An I/O object may have a reference count of 0, however, the only
    // operations permitted on such an object are iorefcnt() and ioaddref(). The
    // rationale for allowing objects with a reference count of 0 is to allow an
    // object to be pre-initialized. For example, each device instance can use a
    // single pre-allocated I/O object that is returned whenever the device is
    // opened. Such an I/O object can be pre-initialized so that, when the
    // device is opened, it is only necessary to increment the reference count.
};

struct iointf {
    const char * implname;

    // The /implname/ member must be a short string giving the name of the I/O
    // object implementation. It must be a pointer to a valid null-terminated
    // string. This string may be used for diagnostics. It is not user-visible
    // (not returned by any function in io.h).

    void (*reclaim)(struct io * io);

    // Called to signal that there are no references remaining to the object and
    // that no further functions will be called on this object. An
    // implementation should reclaim any resources used by the I/O object. An
    // implementation may assume:
    // - The function is invoked via the /read/ function pointer of a valid I/O
    //   object with a non-zero reference count.
    // - The function pointer (io->intf->read) points to the function called.

    long (*read)(struct io * io, void * buf, long bufsz);

    // Called via ioread() to perform a _read_ operation on an I/O object. An
    // implementation may assume:
    // - The function is invoked via the /read/ function pointer of a valid I/O
    //   object with a non-zero reference count.
    // - If (bufsz != 0), /buf/ is a valid pointer in the active address space
    //   to a buffer large enough to hold /bufsz/ bytes. If (bufsz == 0), /buf/
    //   may be NULL.
    // - /bufsz/ is a non-negative multiple of the I/O object's block size.
    //
    // Implementation requirements and permitted behavior:
    //
    // - If an I/O object does not support the _read_ operation, an
    //   implementation may either set the /read/ function pointer to NULL or
    //   return -ENOTSUP.
    // - An implementation must write no more than /bufsz/ bytes to /buf/.
    // - An implementation must return a negative error code if the _read_
    //   operation was not successful.
    // - An implementation must return the number of bytes written to /buf/ if
    //   the operation was successful.
    // - The number of bytes written to /buf/ must be a multiple of the I/O
    //   object's block size.
    // - If the implementation returns 0 and /bufsz/ was non-zero, then all
    //   future _read_ operations should also return 0. A zero return value is
    //   intended to signal that more more data is available, nor will be
    //   available in the future, to read. File-like I/O objects should return 0
    //   when the end of file is reached. Stream-like I/O objects should return
    //   0 when a connection is broken and no more data will be available.

    long (*write)(struct io * io, const void * buf, long buflen);

    // Called via iowrite() to perform a _write_ operation on an I/O object. An
    // implementation may assume:
    // - The function is invoked via the /store/ function pointer of a valid I/O
    //   object with a non-zero reference count.
    // - If (buflen != 0), /buf/ is a valid pointer in the active address space
    //   to a buffer large enough to hold /buflen/ bytes. If (buflen == 0),
    //   /buf/ may be NULL.
    // - /buflen/ is a non-negative multiple of the I/O object's block size.
    //
    // Implementation requirements and permitted behavior:
    //
    // - If an I/O object does not support the _write_ operation, an
    //   implementation may either set the /write/ function pointer to NULL or
    //   return -ENOTSUP.
    // - An implementation must write no more than /buflen/ bytes from /buf/.
    // - An implementation must return a negative error code if the _write_
    //   operation was not successful.
    // - An implementation must return the number of bytes written from /buf/ if
    //   the operation was successful.
    // - The number of bytes written from /buf/ must be a multiple of the I/O
    //   object's block size.
    // - If the implementation returns 0 and /buflen/ was non-zero, then all
    //   future _write_ operations should also return 0. A zero return value is
    //   intended to signal that more more data is available, nor will be
    //   available in the future, to read. File-like I/O objects should return 0
    //   when the end of file is reached and the file system does not support
    //   increasing the file size. Stream-like I/O objects should return 0 when
    //   a connection is broken and no more data will be accepted.

    long (*store)(struct io * io, unsigned long long pos, const void * buf, long buflen);

    // Called via iostore() to perform a _store_ operation on an I/O object. An
    // implementation may assume:
    // - The function is invoked via the /store/ function pointer of a valid I/O
    //   object with a non-zero reference count.
    // - If (buflen != 0), /buf/ is a valid pointer in the active address space
    //   to a buffer large enough to hold /buflen/ bytes. If (buflen == 0),
    //   /buf/ may be NULL.
    // - /buflen/ is a non-negative multiple of the I/O object's block size.
    // - /pos/ is a multiple of the I/O object's block size.
    //
    // Implementation requirements and permitted behavior:
    //
    // - If an I/O object does not support the _store_ operation, an
    //   implementation may either set the /store/ function pointer to NULL or
    //   return -ENOTSUP.
    // - An implementation must store exactly /buflen/ bytes if /pos/ plus
    //   /buflen/ is not more than the storage I/O object's capacity as returned
    //   by ioctl(IOC_GETEND).
    // - If /pos/ plus /buflen/ exceeds the storage I/O object's capacity as
    //   returned by ioctl(IOC_GETEND), an implementation may either proceed as
    //   if /buflen/ were set to its maximum valid value (capacity minus /pos/),
    //   or abort the operation and return -EINVAL without storing any data.
    // - An implementation must return a negative error code if the _store_
    //   operation was not successful.

    long (*fetch)(struct io * io, unsigned long long pos, void * buf, long buflen);

    // Called via iofetch() to perform a _fetch_ operation on an I/O object. An
    // implementation may assume:
    // - The function is invoked via the /fetch/ function pointer of a valid I/O
    //   object with a non-zero reference count.
    // - If (buflen != 0), /buf/ is a valid pointer in the active address space
    //   to a buffer large enough to hold /buflen/ bytes. If (buflen == 0),
    //   /buf/ may be NULL.
    // - /buflen/ is a non-negative multiple of the I/O object's block size.
    // - /pos/ is a multiple of the I/O object's block size.
    //
    // Implementation requirements and permitted behavior:
    //
    // - If an I/O object does not support the _fetch_ operation, an
    //   implementation may either set the /fetch/ function pointer to NULL or
    //   return -ENOTSUP.
    // - An implementation must fetch exactly /buflen/ bytes if /pos/ plus
    //   /buflen/ is not more than the storage I/O object's capacity as returned
    //   by ioctl(IOC_GETEND).
    // - If /pos/ plus /buflen/ exceeds the storage I/O object's capacity as
    //   returned by ioctl(IOC_GETEND), an implementation may either proceed as
    //   if /buflen/ were set to its maximum valid value (capacity minus /pos/),
    //   or abort the operation and return -EINVAL without storing any data.
    // - An implementation must return a negative error code if the _fetch_
    //   operation was not successful.

    int (*ioctl)(struct io * io, int op, void * arg);

    // Called via ioctl() to perform a special operation on an I/O object. An
    // implementation may assume:
    // - The function is invoked via the /ioctl/ function pointer of a valid I/O
    //   object with a non-zero reference count.
    // - The IOC_GETBLKSZ operation is handled by ioctl(); the implementation
    //   does not need to handle this case.
    // - If the requested operation requires a pointer argument, then /arg/ is a
    //   valid, properly aligned pointer of the right size in the active address
    //   space.
    //
    // Implementation requirements and permitted behavior:
    // - If an I/O object does not support the _ioctl_ operation (except
    //   IOC_GETBLKSZ, which is handled by ioctl()), an implementation may
    //   either set the /ioctl/ function pointer to NULL or return -ENOTSUP.
    // - An implementation must return a negative error code if the operation
    //   was not successful.

    int (*ioctl_u)(struct io * io, int op, uintptr_t arg_uma);

    // Called via ioctl_u() to perform the _ioctl_ operation on an I/O object.
    // The ioctl_u() function called when the request originates from a
    // user-space program. The /arg_uma/ argument is _unchecked_. An
    // implementation must ensure that /arg_uma/ is a valid user
    // program-accessible address before using it to address memory.
    //
    // An implementation of may assume:
    // - The function is invoked via the /ioctl/ function pointer of a valid I/O
    //   object with a non-zero reference count.
    // - The IOC_GETBLKSZ operation is handled by ioctl_u(); the implementation
    //   does not need to handle this case.
    //
    // Implementation requirements and permitted behavior:
    // - If an I/O object does not support the unchecked _ioctl_ operation
    //   (except IOC_GETBLKSZ, which is handled by ioctl_u()), an implementation
    //   may either set the /ioctl_u/ function pointer to NULL or return
    //   -ENOTSUP.
    // - An implementation must validate the size, alignment, and accessability
    //   of the the user program-supplied pointer /arg_uma/.
    // - An implementation must return -EACCESS if the pointer is not accessable
    //   by a user-space program in the active address space.
    // - An implementation must return a negative error code if the operation
    //   was not successful.
    //
};

struct io_term {
    struct io io; 
    // I/O abstraction

    struct io * rawio; 
    // "raw" I/O object
    
    int8_t cr_out; 
    // Output CRLF normalization
    
    int8_t cr_in; 
    // Input CRLF normalization
};

// An io_term object is a wrapper around a "raw" I/O object. It provides newline
// conversion and interactive line-editing for string input.
//
// ioterm_init initializes an io_term object for use with an underlying raw I/O
// object. The /iot/ argument is a pointer to an io_term struct to initialize
// and /rawio/ is a pointer to an io_intf that provides backing I/O.
// I/O term provides three features:
//
//     1. Input CRLF normalization. Any of the following character sequences in
//        the input are converted into a single \n:
//
//            (a) \r\n,
//            (b) \r not followed by \n,
//            (c) \n not preceeded by \r.
//
//     2. Output CRLF normalization. Any \n not preceeded by \r, or \r not
//        followed by \n, is written as \r\n. Sequence \r\n is written as \r\n.
//
//     3. Line editing. The ioterm_getsn function provides line editing of the
//        input.
//
// Input CRLF normalization works by maintaining one bit of state: cr_in.
// Initially cr_in = 0. When a character ch is read from rawio:
// 
// if cr_in = 0 and ch == '\r': return '\n', cr_in <- 1;
// if cr_in = 0 and ch != '\r': return ch;
// if cr_in = 1 and ch == '\r': return \n;
// if cr_in = 1 and ch == '\n': skip, cr_in <- 0;
// if cr_in = 1 and ch != '\r' and ch != '\n': return ch, cr_in <- 0.
//
// Ouput CRLF normalization works by maintaining one bit of state: cr_out.
// Initially, cr_out = 0. When a character ch is written to I/O term:
//
// if cr_out = 0 and ch == '\r': output \r\n to rawio, cr_out <- 1;
// if cr_out = 0 and ch == '\n': output \r\n to rawio;
// if cr_out = 0 and ch != '\r' and ch != '\n': output ch to rawio;
// if cr_out = 1 and ch == '\r': output \r\n to rawio;
// if cr_out = 1 and ch == '\n': no ouput, cr_out <- 0;
// if cr_out = 1 and ch != '\r' and ch != '\n': output ch, cr_out <- 0.

// IO OBJECT OPERATIONS
//
extern struct io * ioinit (
    struct io * io,
    const struct iointf * intf,
    unsigned int blksz,
    unsigned int refcnt
);

// Initializes an /io/ structure. This function may be called to inialize the
// /intf/, /blksz/, and /refcnt/ members of an /io/ struct using the supplied
// parameters. The /io/ argument must point to a region of memory large enough
// to hold an instance of a /io/ structure. The ioinit() function returns the
// first argument. The /intf/ argument must be a pointer to a valid /iointf/
// structure.

extern unsigned int ioblksz(const struct io * io);

// Returns the block size of the I/O object. All I/O operations must in multiples of
// this size.
//
// On entry ioblksz() assumes:
// - /io/ points to a valid I/O object.
//
// On return ioblksz() guarantees:
// - The returned block size is greater than or equal to 1.
// - The returned block size is not greater than INT_MAX.
//
// * This function may be called from an ISR.
//
// See also: ioread(), iowrite(), iofill(), iofetch(), iostore().


extern unsigned int iorefcnt(const struct io * io);

// Returns the number of independent references to the I/O object. A reference
// is _independent_ if it has a distinct lifetime from any other reference to
// the same object. An I/O object may have a reference count of 0, however, the
// only permitted operations on such an object are: ioblksz(), iorefcnt(), and
// ioaddref().
//
// On entry iorefcnt() assumes:
// - /io/ points to a valid I/O object.
//
// On return iorefcnt() gurantees:
// - The return value is the number of independent references to the object.
//
// * This function may be called from an ISR.
//
// See also: ioaddref(), iodropref().


extern struct io * ioaddref(struct io * io);

// Increments the reference count of an I/O object by 1. Returns the argument
// /io/. ioaddref() should be called whenever an independent copy of a reference
// to an I/O object is made. The return value is the same as the argument to
// encourage the use of ioaddref() to mark the creation of an independent copy:
//
//    struct io * someio;
//    struct io * someio_copy;
//    // /someio/ initialized to point to a new I/O object
//    someio_copy = ioaddref(someio); // indicates someio_copy is an ind. ref.
//
// On entry ioaddref() assumes:
// - /io/ points to a valid I/O object.
//
// On return ioaddref() gurantees:
// - The return value is /io/.
//
// * This function may be called from an ISR.
//
// See also: iorefcnt(), iodropref().


extern void iodropref(struct io * io);

// Decrements the reference count of an I/O object by 1. If the number of
// references becomes 0, iodropref() calls the /reclaim/ function of I/O object.
// iodropref() must be called to signal the end of an I/O object reference's
// lifetime. After calling iodropref(), /io/ is no longer considered to point to
// a valid I/O object, although other independent references to the object may
// still be valid. Example:
// 
//    struct io * someio;
//    struct io * someio_copy;
//    // /someio/ initialized to point to a new I/O object
//    someio_copy = ioaddref(someio); // refcnt == 2
//    iodropref(someio); // refcnt == 1, someio no longer valid
//    ioread(someio_copy, buf, bufsz); // OK, /someio_copy/ is indep. ref.
//    ioread(someio, buf, bufsz); // DISCOURAGED, /someio/ ref. was dropped
//
// On entry iodropref() assumes:
// - /io/ points to a valid I/O object with a non-zero reference count.
//
// On return iodropref() gurantees:
// - The object's /reclaim/ method was called if the reference count became 0
//
// * This function must _not_ be called from an ISR.
//
// See also: iorefcnt(), ioaddref().

extern long ioread(struct io * io, void * buf, long bufsz);
extern long iofill(struct io * io, void * buf, long len);

// Reads from an I/O object into a buffer.
//
// * this function may call functions that allocate physical memory pages.
// * This function may switch to another thread context.
// * This function must _not_ be called from an ISR.
//
// See also:


extern long iowrite(struct io * io, const void * buf, long len);

// Writes from a buffer to an I/O object.
//
// * this function may call functions that allocate physical memory pages.
// * This function may switch to another thread context.
// * This function must _not_ be called from an ISR.
//
// See also:


extern long iofetch(struct io * io, unsigned long long pos, void * buf, long len);

// Fetches data from a storage I/O object into a buffer.
//
// * this function may call functions that allocate physical memory pages.
// * This function may switch to another thread context.
// * This function must _not_ be called from an ISR.
//
// See also: iostore().


extern long iostore(struct io * io, unsigned long long pos, const void * buf, long len);

// Stores data from a buffer into a storage I/O object.
//
// * this function may call functions that allocate physical memory pages.
// * This function may switch to another thread context.
// * This function must _not_ be called from an ISR.
//
// See also: iofetch().


int ioctl(struct io * io, int op, void * arg);

#define IOC_GETBLKSZ 0 // no arg, return value is block size

#define IOC_GETEND 4 // arg is unsigned long long *
#define IOC_SETEND 5 // arg is const unsigned long long *
#define IOC_GETPOS 6 // arg is unsigned long long *
#define IOC_SETPOS 7 // arg is const unsigned long long *

#define IOC_MAPBUF 8 // arg is const void **

// Performs a special operation on an I/O object. The /op/ parameter specifies
// the operation, one of the IOC-prefixed constants defined above. The operation
// may take an optional argument, whis is passed via /arg/.
//
// Unless otherwise indicated, ioctl() eturns 0 to indicate
// success. If the request operation is not supported, both functions
// return -ENOTSUP. In case of error, both functions return a negative error
// code, the negation of one of the constants defined on error.h.
//
// On entry ioctl() assumes:
// - /io/ points to a valid I/O object with a non-zero reference count.
// - if not NULL, /arg/ is a properly aligned pointer of the required type
//
// The following list describes specific system-defined ioctl() operations.
//
// - int ioctl(io, IOC_GETBLKSZ, NULL);
//   Gets the block size of an I/O object. All I/O operations on the object must
//   be in multiples of the block size. The size is returned via the return
//   value, _not_ via a pointer output argument like most ioctl() operations.
//   The returned block size may also be obtained using ioblksz(). This
//   operation is supported by all I/O objects. The block size is at least 1, so
//   the return value is never 0.
//
// - int ioctl(io, IOC_GETEND, unsigned long long * endposptr);
//   Gets size/capacity of a storage I/O object. The size/capacity in bytes is
//   equal to the last valid byte position within the object, which is the
//   position just after the last byte. The size is written as an integer of
//   unsigned long long type to the address given by /endposptr/. The
//   /endposptr/ argument must be a properly-aligned pointer to a region of
//   memory large enough to hold a variable of type unsigned long long. All I/O
//   objects implementing either the _fetch_ or _store_ operations must also
//   support the IOC_GETEND operation.
//
// - int ioctl(io, IOC_SETEND, unsigned long long * endposptr);
//   Resized a storage I/O object. The size/capacity in bytes is equal to the
//   last valid byte position within the object, which is the position just
//   after the last byte. The /endposptr/ argument must be properly aligned and
//   must point to a region of memory large enough to hold a variable of type
//   unsigned long long. Not all storage I/O objects support this operation.
//
// - int ioctl(io, IOC_GETPOS, unsigned long long * posptr);
//   Gets the byte position at which the next _read_ or _write_ operation would
//   occur in a storage I/O object. This position is known as the _current
//   position_ in a storage I/O object that supports either _read_ or _write_
//   operations. The current byte position is written as an integer of unsigned
//   long long type to the address given by /posptr/. The /posptr/ argument must
//   be a properly-aligned pointer to a region of memory large enough to hold a
//   variable of type unsigned long long.
//
// - int ioctl(io, IOC_SETPOS, unsigned long long * posptr);
//   Sets the byte position at which the next _read_ or _write_ operation would
//   occur in a storage I/O object. This position is known as the _current
//   position_ in a storage I/O object that supports either _read_ or _write_
//   operations. The /posptr/ argument must be properly aligned and must point
//   to a region of memory large enough to hold a variable of type unsigned long
//   long.

int ioctl_u(struct io * io, int op, uintptr_t arg_uma);

// Performs a special operation on an I/O object, allowing an untrusted
// argument. The function behaves identically to ioctl(), but, unlike ioctl(),
// /arg_uma/ may be an invalid pointer. The I/O object implementation is
// responsible for ensuring that /arg_uma/ is a valid, properly aligned,
// user-accessible pointer of the required type. Thus, ioctl() should be used
// for calls originating inside the kernel (from trusted code), while ioctl_u()
// should be used to service the /ioctl/ system call.
//
// In addition to the error codes returned by ioctl(), ioctl_u() returns
// -EACCESS if /arg_uma/ is not user accessible and -EINVAL if it is misaligned.
//
// See also: ioctl().


// put, get, print
static inline int ioputc(struct io * io, char c);

// Writes a characeter to an I/O abstraction. See iowrite for more details.

static inline int iogetc(struct io * io);

// Reads a characeter from an I/O abstraction. See ioread for more details.

int ioputs(struct io * io, const char * s);

// Writes a string to an I/O abstraction. See iowrite for more details.

long ioprintf(struct io * io, const char * fmt, ...);

// This function can take a variable amount of arguments. It formats and 
// prints a string to an I/O abstraction. See iowrite for more details.

long iovprintf(struct io * io, const char * fmt, va_list ap);

// Helper for ioprintf that calls vgprintf which writes the properly 
// formated string.

struct io * ioterm_init(struct io_term * iot, struct io * rawio);

// Initializes an io_term object for use with an underlying raw I/O object.

char * ioterm_getsn(struct io_term * iot, char * buf, size_t n);

// The ioterm_getsn function reads a line of input of up to /n/ characters from
// a terminal I/O object. The function supports line editing (delete and
// backspace) and limits the input to /n/ characters.

// definitions for putc and getc
static inline int ioputc(struct io * io, char c) {
    long wlen;

    wlen = iowrite(io, &c, 1);

    if (wlen < 0)
        return wlen;
    else if (wlen == 0)
        return -EIO;
    else
        return (unsigned char)c;
}

static inline int iogetc(struct io * io) {
    long rlen;
    char c;

    rlen = ioread(io, &c, 1);
    
    if (rlen < 0)
        return rlen;
    else if (rlen == 0)
        return -EIO;
    else
        return (unsigned char)c;
}

#endif // _IO_H_
