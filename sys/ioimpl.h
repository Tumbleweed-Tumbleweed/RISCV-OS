// ioimpl.h - Generic I/O objects implementer header
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

// This header file is intended for parts of the system that _implement_ I/O
// objects. See io.h for the interface for _users_ of I/O objects.

#ifndef _IOIMPL_H_
#define _IOIMPL_H_

#include "io.h"

// IO and IOINTF
//

// An I/O object is a system object that supports a set of operations, most of
// which are concerned with input and output. The /io/ structure, defined below,
// represents the common elements of all I/O objects. Functions that operate on
// I/O objects pass a pointer to this structure. By including an /io/ structure
// inside a larger structure used to implement an I/O object, an implementation
// can recover a pointer to the larger structure. If the /io/ object is the
// first element of the larger structure, recovering a pointer to the enclosing
// structure from a (struct io*) entails only casting:
//
//     struct myio {
//         struct io base;
//         // ...
//     };
//
//     long myio_read(struct io * io, void * buf, long bufsz) {
//         struct someio * myio = (struct myio*)io;
//         // ...
//     }
//
// The /io/ structure is described below.
//

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


// SEEKIO
//

// It is often convenient to access a storage I/O object, which is an object
// implementing either the _fetch_ or _store_ operation, using _read_ or _write_
// operations as if it were a stream of bytes. Such accesses may be treated as
// occurring at a _current position_ within the storage object. The current
// position starts at 0 and advanced by the number of bytes read or written
// using by the _read_ or _write_ operations. This is the traditional behavior
// of Unix files.
//
// The seek-io object allows a storage I/O object to provide _read_ and _write_
// operations with minimal additional code. A storage I/O object, instead of
// containing an /io/ structure, should use a /seekio/ structure. The memory-
// based I/O object returned by create_memio(), for example, is defined:
//
//     struct memio {
//         struct seekio base; // instead of `struct io`
//         // ...
//     };
//
// The /read/ and /write/ function pointers of its /iointf/ structure should
// contain pointers to the seekio_read() and seekio_write() functions declared
// below. When these functions are called, they convert the _read_ or _write_
// operation into a _fetch_ or _store_ operation of the correct size at the
// current position. The seekio_ioctl() function may be supplied to implement
// the _ioctl_ operation or may be called from the storage I/O object's own
// _ioctl_ implementation to handle IOC_GETPOS and IOC_SETPOS operations.
//
// The underlying I/O object MUST support IOC_GETEND, as this is how seek-io
// verifies the pos argument before calling the underlying fetch or store 
// operation. If the underlying I/O object also supports IOC_SETEND, the 
// seek-io object will attempt to resize all writes past the end to extend
// the file size. If it extension is not supported, writes are truncated the
// same as reads.

struct seekio {
    struct io base;
    unsigned long long pos;
};

extern struct io * seekio_init (
    struct seekio * sio,
    const struct iointf * intf,
    unsigned int blksz,
    unsigned int refcnt
);

extern long seekio_read(struct io * io, void * buf, long bufsz);

// Reads from a seek-io object into a buffer at its internal position.
// Equivalent to iofetch, with internal position set to /pos/. Calls 
// IOC_GETEND to verify pos arguments.
//
// * This function may switch to another thread context.
// * This function must _not_ be called from an ISR.

extern long seekio_write(struct io * io, const void * buf, long len);

// Writes from a buffer to a seek-io object to its internal position.
// Equivalent to iostore, with internal position set to /pos/. Calls 
// IOC_GETEND to verify pos arguments.
//
// This function will also call the io's SETEND ioctl if it attempts to write
// past the end of the buffer. If SETEND succeeds, it will write as normal.
// If SETEND fails, it will return the number of bytes successfully written.
//
// * This function may switch to another thread context.
// * This function must _not_ be called from an ISR.

extern int seekio_ioctl(struct io * io, int op, void * arg);

// Performs a special operation on a seek-io object. See ioctl() for more 
// details on return codes.
//
// On entry seekio_ioctl() assumes:
// - /io/ points to a valid I/O object with a non-zero reference count.
//
// The following list describes specific system-defined ioctl() operations.
// supported by seekio_ioctl().
//
// - unsigned long long pos; ioctl(io, IOC_GETPOS, &pos); Gets the /pos/ of a
//   seek-io object. The current byte position is written as an integer of 
//   unsigned long long type to the address given by /arg/. The /arg/ argument 
//   must be a properly-aligned pointer to a region of memory large enough to
//   hold a variable of type unsigned long long.
//
// - unsigned long long pos; ioctl(io, IOC_SETPOS, &pos); Sets the /pos/ of a
//   seek-io object. /arg/ must point to an integer of type unsigned long long 
//   containing the new position. The /arg/ argument must be a properly-aligned 
//   pointer.

#endif // _IOIMPL_H_
