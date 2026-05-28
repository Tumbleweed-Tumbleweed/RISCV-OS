// fsimpl.h - File system implementation interface
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _FSIMPL_H_
#define _FSIMPL_H_

#include "io.h"

// FILE SYSTEM IMPLEMENTATION INTERFACE
//
// A filesystem implementation exposes operations through /struct filesystem/
// and is registered with the file system manager using mount_filesys().
//
// All callbacks return 0 for success or a negative error code on failure.
// Callback pointers may be NULL to indicate that the operation is unsupported.
//
struct filesystem {
    // Human-readable implementation name, used for debugging and tracing.
    const char * implname;

    // Opens a file or listing.
    //
    // The /fs/ argument specifies this filesystem object.
    // The /flname/ argument specifies the file component selected by the file
    // system manager. /flname/ may be NULL when the caller requests the
    // mountpoint listing interface.
    // The /ioptr/ argument specifies storage for the returned io object.
    //
    // On success:
    // - Returns 0.
    // - Stores an open `struct io *` in /ioptr/.
    int (*openfile)(
        struct filesystem * fs, const char * flname, struct io ** ioptr);

    // Creates a file named /flname/ in /fs/.
    //
    // If NULL, create operations are reported as unsupported by filesys.c.
    int (*createfile)(struct filesystem * fs, const char * flname);

    // Deletes a file named /flname/ in /fs/.
    //
    // If NULL, delete operations are reported as unsupported by filesys.c.
    int (*deletefile)(struct filesystem * fs, const char * flname);

    // Flushes pending filesystem state to backing storage.
    //
    // If NULL, the file system manager skips flushing this filesystem.
    void (*flush)(struct filesystem * fs);
};

#endif  // _FSIMPL_H_
