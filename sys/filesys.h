// filesys.h - File system manager
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _FILESYS_H_
#define _FILESYS_H_

#include "io.h"

struct filesystem; // opaque decl.

// FILE SYSTEM MANAGER
//
// The file system manager maps mountpoint names to mounted filesystem
// implementations (`struct filesystem *`) and dispatches file operations by
// mountpoint name.
//
// Paths used by the syscall layer are split into mountpoint and file
// components by parse_path().
//
// Unless otherwise stated, functions declared in filesys.h assume
// fsmgr_init() has already been called.
//
extern char fsmgr_initialized;
extern int fsmgr_init(void);

// Initializes the file system manager.
//
// This function must be called before other functions declared in filesys.h.
// The global variable /fsmgr_initialized/, which is statically initialized to
// 0, is set to 1 by fsmgr_init(); it must not be modified externally.
//
// On return fsmgr_init() guarantees:
// - /fsmgr_initialized/ is set to 1.
//
// * This function should be called once during system initialization.
// * This function must _not_ be called from an ISR.

extern int mount_filesys(const char * mpname, struct filesystem * fs);
// Mounts a filesystem implementation at a mountpoint name.
//
// On entry mount_filesys() assumes:
// - /mpname/ points to a null-terminated mountpoint name string.
// - /fs/ points to an initialized `struct filesystem`.
// - /mpname/ remains valid after mount_filesys() returns, for as long as the
//   mountpoint may be listed or used.
//
// On return mount_filesys() guarantees:
// - If successful, file operations on /mpname/ are dispatched to /fs/.
// - If /mpname/ is already mounted, mount_filesys() returns negative error code.
//
// * This function may allocate heap memory.
// * This function must _not_ be called from an ISR.
//
// See also: struct filesystem (fsimpl.h), open_file().

extern void flush_all_filesys(void);

// Flushes all mounted filesystems that provide a flush callback.
//
// On return flush_all_filesys() guarantees:
// - For each mounted filesystem with a non-NULL flush callback, the callback
//   has been invoked once.
//
// * This function must _not_ be called from an ISR.

extern int open_file(
    const char * mpname, const char * flname, struct io ** ioptr);
// Opens a file or listing from a mounted filesystem.
//
// On entry open_file() assumes:
// - /ioptr/ is a non-NULL pointer for returning an open `struct io *`.
// - If /mpname/ is NULL or points to an empty string, /flname/ is NULL.
//
// Behavior:
// - If /mpname/ is NULL or empty, open_file() opens a root listing io object
//   that enumerates mountpoint names.
// - Otherwise, open_file() dispatches to the mountpoint's `openfile` callback.
//
// On return open_file() guarantees:
// - If successful, /ioptr/ points to a valid io object and the return value is
//   0.
// - If /mpname/ is not mounted, returns a negative error.
// - If the mounted filesystem does not support openfile, returns 
//   negative error.
//
// * This function must _not_ be called from an ISR.
//
// See also: parse_path(), struct filesystem (fsimpl.h).

extern int create_file(const char * mpname, const char * flname);

// Creates a file at mountpoint /mpname/ with name /flname/.
//
// On entry create_file() assumes:
// - /mpname/ points to a null-terminated mountpoint name.
//
// On return create_file() guarantees:
// - If /flname/ is NULL, returns a negative error code.
// - If /mpname/ is not mounted, returns a negative error code.
// - If the mounted filesystem has no create callback, returns a negative error
//   code.
// - Otherwise, returns the result of the mounted filesystem create callback.
//
// * This function must _not_ be called from an ISR.

extern int delete_file(const char * mpname, const char * flname);

// Deletes a file at mountpoint /mpname/ with name /flname/.
//
// On entry delete_file() assumes:
// - /mpname/ points to a null-terminated mountpoint name.
//
// On return delete_file() guarantees:
// - If /flname/ is NULL, returns a negative error code.
// - If /mpname/ is not mounted, returns a negative error code.
// - If the mounted filesystem has no delete callback, returns a negative error
//   code.
// - Otherwise, returns the result of the mounted filesystem delete callback.
//
// * This function must _not_ be called from an ISR.

extern void parse_path(char * path, char ** mpnameptr, char ** flnameptr);

// Splits a path string into mountpoint and file components.
//
// On entry parse_path() assumes:
// - /path/ points to mutable, null-terminated storage.
// - /mpnameptr/ is non-NULL.
// - /flnameptr/ is non-NULL.
//
// Leading '/' characters in /path/ are skipped. The first remaining '/' (if
// present) is replaced with '\0'(null terminator) in-place.
//
// On return parse_path() guarantees:
// - /mpnameptr/ points to the mountpoint component inside /path/.
// - /flnameptr/ is NULL if there is no '/' after the mountpoint component.
// - /flnameptr/ points to the file component if '/' is present after the
//   mountpoint component (possibly an empty string).
//
// This function does not allocate memory and does not copy the path buffer.
//
// * This function modifies /path/ in place.
// * This function may be called from an ISR.

extern struct filesystem nullfs;

// A minimal filesystem implementation with name `"nullfs"`.
//
// The nullfs open callback accepts only /flname/ == NULL and, in that case,
// returns a null io object. Any non-NULL /flname/ is rejected with a negative
// error code.
// nullfs does not provide create, delete, or flush callbacks.
//
// See also: open_file(), struct filesystem (fsimpl.h).

#endif // _FILESYS_H_
