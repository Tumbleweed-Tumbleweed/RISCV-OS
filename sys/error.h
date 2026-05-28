// error.h - Error numbers and functions
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _ERROR_H_
#define _ERROR_H_

#define EINVAL          1 // Invalid argument
#define EBUSY           2 // Device or resource busy
#define ENOTSUP         3 // Operation not supported
#define EIO             4 // I/O error
#define EBADFMT         5 // Bad format
#define ENOENT          6 // No such file or directory
#define EACCESS         7 // Permission denied
#define EBADF           8 // Bad file descriptor
#define EMFILE          9 // Too many open files
#define EMPROC         10 // Too many processes
#define EMTHR          11 // Too many threads
#define ECHILD         12 // No child thread
#define ENOMEM         13 // Out of memory
#define EPIPE          14 // Broken pipe
#define EEXIST         15 // Object exists
#define EFAULT         16 // Bad address
#define ENAMETOOLONG   17 // Name too long


extern const char * error_name(int err);

// Returns a string with the error name (e.g. 2 => "EBUSY"). The returned
// pointer will always point to a valid null-terminated string.


extern const char * error_desc(int err);

// Returns a string with the error description (e.g. 2 => "Resource busy"). The
// returned pointer will always point to a valid null-terminated string.


#endif // _ERROR_H_
