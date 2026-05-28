// elf.h - ELF executable loader
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _ELF_H_
#define _ELF_H_

#include "io.h"

// ELF EXECUTABLE LOADER
//
// elf_load() loads a 64-bit little-endian RISC-V ELF executable from a
// `struct io *`.
//
// [CP1] If virtual memory is not enabled, elf_load() loads
// PT_LOAD segments directly to the addresses given by ELF p_vaddr. No virtual
// memory allocation or mapping is performed.
//
// [CP2] If virtual memory is enabled, elf_load() allocates
// and maps memory pages for each PT_LOAD segment before loading segment data.
// Region permissions are then set from ELF segment flags.
//
// In all configurations, elf_load() validates ELF structure and rejects invalid
// or unsupported executable formats.
//



extern int elf_load(struct io * io, void (**eptr)(void));

// Loads an executable ELF image from /io/ and returns the process entry point
// through /eptr/.
//
// On entry elf_load() assumes:
// - /io/ is a valid `struct io *`.
// - /eptr/ is a non-NULL pointer to entry-point pointer storage.
//
// On success elf_load() guarantees:
// - PT_LOAD segments from the ELF image are loaded into memory.
// - /eptr/ is set to the ELF entry point.
// - The return value is 0.
//
// On error elf_load() returns a negative error code for:
// - Any negative error for invalid ELF formats.
// - Any negative error returned by underlying io operations.
//
// If elf_load() returns an error, it does not need to handle rollback of partial
// memory updates made before the error was detected.
//
// * This function must _not_ be called from an ISR.
// * [CP2] This function may allocate physical pages and modify page mappings.

#endif // _ELF_H_
