// ngfs.h - A FAT-like file system
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _NGFS_H_
#define _NGFS_H_

#include <stdint.h>

struct filesystem; // extern decl.
struct io; // extern decl.

extern int mount_ngfs(const char * mpname, struct io * bkgio);

// Mounts an instance of the Next Generation File System. Allows the filesys 
// functions to access the files in /bkgio/ under the name /mpname/.
//
// On entry mount_devfs() assumes:
// - /mpname/ is non-NULL and points to a NUL-terminated string.
// - /bkgio/ is non-NULL io object that is an interface for a storage device.
// - /bkgio/ refers to a storage device that is a valid NGFS image.
//
// On return devmgr_init() guarantees (on success):
// - An ngfs object is initialized and accessible via the filesys function
//   /open_file/, /create_file/, and /delete_file/ under the name /mpname/.
// - on failure, mount_ngfs() returns a negative error code.

#define NGFS_BLKSZ                  512UL
#define NGFS_DENSZ                  32UL
#define NGFS_MAX_FILENAME_LEN       (NGFS_DENSZ - sizeof(uint8_t) - 2*sizeof(uint32_t))
#define NGFS_FAT_ENTRIES_PER_BLOCK  (NGFS_BLKSZ / sizeof(uint32_t))
#define NGFS_ROOT_DATA_BLOCK        0

#define NGFS_BLOCK_FREE             (uint32_t)0
#define NGFS_BLOCK_END              (uint32_t)(-1)

/*
Overall filesystem image layout

+------------------+
|       FAT        |
+------------------+
|       FAT        |
+------------------+
|      ...         |
+------------------+
|  ROOT DIR BLK 0  |
+------------------+
|    Data Blk 1    |
+------------------+
|      ...         |
+------------------+

NOTE: The ((packed)) attribute is used to ensure that the struct is packed and 
there is no padding between the members or struct alignment requirements.
*/

struct ngfs_dir_entry {
    char name[NGFS_MAX_FILENAME_LEN+sizeof(char)]; 
    uint32_t size;
    uint32_t start_block;
} __attribute__((packed));

struct ngfs_data_block {
    uint8_t data[NGFS_BLKSZ];
}__attribute__((packed));

struct ngfs_fat {
    uint32_t fat[NGFS_FAT_ENTRIES_PER_BLOCK];
}__attribute__((packed));

#endif // _NGFS_H_