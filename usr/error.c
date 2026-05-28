// error.c - Error numbers and functions
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "error.h"

#include <stddef.h>  // for NULL

static const char * const errdescrs[] = {
    [0] = "(success)",
    [EINVAL] = "EINVAL",
    [EBUSY] = "EBUSY",
    [ENOTSUP] = "ENOTSUP",
    [EIO] = "EIO",
    [EBADFMT] = "EBADFMT",
    [ENOENT] = "ENOENT",
    [EACCESS] = "EACCESS",
    [EBADF] = "EBADF",
    [EMFILE] = "EMFILE",
    [EMPROC] = "EMPROC",
    [EMTHR] = "EMTHR",
    [ECHILD] = "ECHILD",
    [ENOMEM] = "ENOMEM",
    [EEXIST] = "EEXIST",
    [EFAULT] = "EFAULT"
};

static const char * const errmsgs[] = {
    [0] = "Success",
    [EINVAL] = "Invalid argument",
    [EBUSY] = "Resource busy",
    [ENOTSUP] = "Not supported",
    [EIO] = "Input/output error",
    [EBADFMT] = "Invalid format",
    [ENOENT] = "No such file or device",
    [EACCESS] = "Permission denied",
    [EBADF] = "Bad file descriptor",
    [EMFILE] = "Too many open files",
    [EMPROC] = "Too many processes",
    [EMTHR] = "Too many threads",
    [ECHILD] = "No child thread",
    [ENOMEM] = "Out of memory",
    [EEXIST] = "File exists",
    [EFAULT] = "Bad address"
};

const char * error_name(int err) {
    const char * str;

    if (err < 0)
        err = -err;

    if (err < sizeof(errdescrs) / sizeof(errdescrs[0]))
        str = errdescrs[err];
    else
        str = NULL;

    return str ? str : "(unknown)";
}

const char * error_desc(int err) {
    const char * str;

    if (err < 0)
        err = -err;

    if (err < sizeof(errmsgs) / sizeof(errmsgs[0]))
        str = errmsgs[err];
    else
        str = NULL;

    return str ? str : "Unknown error";
}