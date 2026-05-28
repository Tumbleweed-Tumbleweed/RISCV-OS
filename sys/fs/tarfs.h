// tarfs.h - A file system backed by a tar archive
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _TARFS_H_
#define _TARFS_H_

struct io; // extern decl.

extern int mount_tarfs(const char * mpname, struct io * bkgio);

#endif // _TARFS_H_