// io.h - I/O Objects
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _IO_H_
#define _IO_H_

#include <stddef.h> // size_t
#include <stdint.h> // uintptr_t

struct io; // opaque

// The /io/ structure represents an objects capable of input and output
// operations, which includes files and devices. The structure definition is
// opaque to _users_ of I/O objects. Parts of the system that _implement_ an I/O
// object should include ioimpl.h, which contains the definition of the /io/
// structure.


// IO OBJECT OPERATIONS
//

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
extern int iogetc(struct io * io);

// Reads from an I/O object into a buffer.
//
// * this function may call functions that allocate physical memory pages.
// * This function may switch to another thread context.
// * This function must _not_ be called from an ISR.
//
// See also:


extern long iowrite(struct io * io, const void * buf, long len);
extern int ioputc(struct io * io, char c);


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
#define IOC_RESET    1 // no arg, return value 0 or -ENOTSUP

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
// - int ioctl(io, IOC_RESET, NULL);
//
//   Resets the I/O object. Resets an I/O object to state equivalent to its
//   initial state. The operation is I/O object-specific.
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


// SPECIAL IO OBJECTS
//

extern struct io * create_nullio(void);

extern struct io * create_memio (
    void * buf, size_t size,
    void(*reclfn)(void*,size_t));

extern void create_iopipe(struct io ** wioptr, struct io ** rioptr);

#endif // _IO_H_
