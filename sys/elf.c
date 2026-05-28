// elf.c - ELF executable loader
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef ELF_TRACE
#define TRACE
#endif

#ifdef ELF_DEBUG
#define DEBUG
#endif

#include "elf.h"
#include "conf.h"
#include "io.h"
#include "string.h"
#include "memory.h"
#include "error.h"
#include "misc.h"
#include <stdint.h>

// Offsets into e_ident
#define EI_MAG0         0           // File identification
#define EI_MAG1         1           // File identification
#define EI_MAG2         2           // File identification
#define EI_MAG3         3           // File identification
#define EI_CLASS        4           // File class
#define EI_DATA         5           // Data encoding
#define EI_VERSION      6           // File version
#define EI_OSABI        7           // Operating system/ABI identification
#define EI_ABIVERSION   8           // ABI version
#define EI_PAD          9           // Start of padding bytes

// ELF header e_ident[EI_MAG] values
#define ELFMAG0 0x7f                // "magic number"
#define ELFMAG1 'E'                 // "magic number"
#define ELFMAG2 'L'                 // "magic number"
#define ELFMAG3 'F'                 // "magic number"

// ELF header e_ident[EI_CLASS] values
#define ELFCLASSNONE 0              // Invalid class
#define ELFCLASS32 1                // 32-bit objects
#define ELFCLASS64 2                // 64-bit objects

// ELF header e_ident[EI_DATA] values
#define ELFDATANONE 0               // Invalid data encoding
#define ELFDATA2LSB 1               // See below
#define ELFDATA2MSB 2               // See below

// ELF header e_ident[EI_VERSION] values
#define EV_NONE     0               // Invalid version
#define EV_CURRENT  1               // Current version

// ELF header e_ident[EI_OSABI] values
#define ELFOSABI_NONE 0             // No extensions or unspecified
#define ELFOSABI_HPUX 1             // Hewlett-Packard HP-UX
#define ELFOSABI_NETBSD 2           // NetBSD
#define ELFOSABI_LINUX 3            // Linux
#define ELFOSABI_SOLARIS 6          // Sun Solaris
#define ELFOSABI_AIX 7              // AIX
#define ELFOSABI_IRIX 8             // IRIX
#define ELFOSABI_FREEBSD 9          // FreeBSD
#define ELFOSABI_TRU64 10           // Compaq TRU64 UNIX
#define ELFOSABI_MODESTO 11         // Novell Modesto
#define ELFOSABI_OPENBSD 12         // 	Open BSD
#define ELFOSABI_OPENVMS 13         // Open VMS
#define ELFOSABI_NSK 14             // Hewlett-Packard Non-Stop Kernel

// ELF header e_type values
enum elf_et {
    ET_NONE = 0,
    ET_REL,
    ET_EXEC,
    ET_DYN,
    ET_CORE
};

// The /elf64_ehdr/ structure.
// ELF header struct
struct elf64_ehdr {
    /**  EI_MAG: VALUES(0x7F, "ELF"), CONSTANT SIGNATURE , 
    * EI_CLASS, EI_DATA: VALUES(1ELFCLASS32, 1ELFDATA2LSB) 32 BITS, LITTLE-ENDIAN, 
    * EI_VERSION: VALUES(1EV_CURRENT), ALWAYS 1 */
    unsigned char e_ident[16]; 
    uint16_t e_type;                // VALUES(2ET_EXEC), EXECUTABLE
    uint16_t e_machine;             // VALUES(28EM_ARM), ARM PROCESSOR
    uint32_t e_version;             // VALUES(1EV_CURRENT), ALWAYS 1
    uint64_t e_entry;               // VALUES(0x8000060), ADDRESS WHERE EXECUTION STARTS
    uint64_t e_phoff;               // VALUES(0x40), PROGRAM HEADERS' OFFSET
    uint64_t e_shoff;               // VALUES(0xB0), SECTION HEADERS' OFFSET IGNORE
    uint32_t e_flags;               // 
    uint16_t e_ehsize;              // VALUES(0x34), ELF HEADER'S SIZE
    uint16_t e_phentsize;           // VALUES(0x20), ELF HEADER'S SIZE
    uint16_t e_phnum;               // VALUES(1), COUNT OF PROGRAM HEADERS
    uint16_t e_shentsize;           // VALUES(0x28), SIZE OF A SINGLE SECTION HEADER IGNORE
    uint16_t e_shnum;               // VALUES(4), COUNT OF SECTION HEADERS IGNORE
    uint16_t e_shstrndx;            // VALUES(3*), INDEX OF THE NAMES' SECTION IN THE TABLE IGNORE
};

// The /elf_pt/ enumeration.
// Program header p_type values
enum elf_pt {
	PT_NULL = 0, 
	PT_LOAD,
	PT_DYNAMIC,
	PT_INTERP,
	PT_NOTE,
	PT_SHLIB,
	PT_PHDR,
	PT_TLS
};

// Program header p_flags bits
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

// The /elf64_phdr/ structure.
// Program header struct
struct elf64_phdr {
    uint32_t p_type;                // THE SEGMENT SHOULD BE LOADED IN MEMORY
    uint32_t p_flags;               // READABLE AND EXECUTABLE
    uint64_t p_offset;              // OFFSET WHERE IT SHOULD BE READ
    uint64_t p_vaddr;               // VIRTUAL ADDRESS WHERE IT SHOULD BE LOADED
    uint64_t p_paddr;               // PHYSICAL ADDRESS WHERE IT SHOULD BE LOADED
    uint64_t p_filesz;              // SIZE ON FILE
    uint64_t p_memsz;               // SIZE IN MEMORY
    uint64_t p_align;               // which the segments are aligned in memory and in the file
};

// ELF header e_machine values (short list)
#define  EM_RISCV   243

/*
INPUTS: struct io * io - pointer to the io device
        void (**eptr)(void) - pointer to a pointer containing entry point
OUTPUTS: returns 0 on success; negative error code on failures
DESCRIPTION: validates, loads, and maps memory from and ELF file into binary
SIDE EFFECTS: allocates and maps pages into memory; sets page permission flags; entry point pointer is set 
*/
int elf_load(struct io * io, void (**eptr)(void)) {
    if (io == NULL ) return -EINVAL;             // ensure we are passed valid pointers
    // Loading process
    // 1 Header
    // THE ELF HEADER IS PARSED
    struct elf64_ehdr ehdr;
    
    long fetch = iofetch(io, 0, &ehdr, sizeof(struct elf64_ehdr));
    if (fetch < 0) return fetch;                                // if fetch fails return passed error code
    if (fetch != sizeof(struct elf64_ehdr)) return -EBADFMT;    // verify we fetch the full struct
     
    // Check e_ident
    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 ||                     // EI_MAG0-EI_MAG3
        ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr.e_ident[EI_MAG3] != ELFMAG3 
    ) return -EBADFMT;
    
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) return -EBADFMT;

    // specifies the encoding of both the data structures used by object file container and data contained in object file sections
    if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB) return -EBADFMT;

    // Version check
    if (ehdr.e_ident[EI_VERSION] != EV_CURRENT) return -EBADFMT;

    if (ehdr.e_version != EV_CURRENT) return -EBADFMT;
    
    // make sure its Executable file
    if (ehdr.e_type != ET_EXEC) return -EBADFMT;

    // required architecture for an individual file
    if (ehdr.e_machine != EM_RISCV) return -EBADFMT;
    
    // Chech ehdr si
    if (ehdr.e_ehsize != sizeof(struct elf64_ehdr)) return -EBADFMT;

    // PROGRAM HEADERS' OFFSET
    if (ehdr.e_phoff == 0) return -EBADFMT;

    // SIZE OF A SINGLE PROGRAM HEADER
    if (ehdr.e_phentsize != sizeof(struct elf64_phdr)) return -EBADFMT;

    // COUNT OF PROGRAM HEADERS
    if (ehdr.e_phnum == 0)  return -EBADFMT;

    // 2
    // Must validate each program segment and load valid segments into memory regions specified by ELF p_vaddr.
    // Must validate and return the ELF entry point.
    // THE FILE IS MAPPED IN MEMORY
    // ACCORDING TO ITS SEGMENT(S)

    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        struct elf64_phdr phdr;
        uint64_t offset = ehdr.e_phoff + ehdr.e_phentsize * i;
        
        if (offset < ehdr.e_phoff) return -EBADFMT;

        // fetch data from the excecutable and put into phdr
        fetch = iofetch(io, offset, &phdr, sizeof(struct elf64_phdr));
        if (fetch < 0) return fetch;                                        // verify that fetch did not fail
        if (fetch != sizeof(struct elf64_phdr)) return -EBADFMT;            // verify we fetch the full struct

        if (phdr.p_type == PT_LOAD) {
            // memory less than file error
            if (phdr.p_memsz < phdr.p_filesz) return -EBADFMT;

            // overflow checks
            if (phdr.p_vaddr + phdr.p_memsz < phdr.p_vaddr || phdr.p_offset + phdr.p_filesz < phdr.p_offset) return -EBADFMT;

            // ensure we are within virtual memory range
            if (phdr.p_vaddr < UMEM_START_VMA || phdr.p_vaddr + phdr.p_memsz > UMEM_END_VMA) return -EBADFMT;

            // allocate and map physical pages, return null if no memory remains
            if (alloc_and_map_range(phdr.p_vaddr, phdr.p_memsz, PTE_R | PTE_W | PTE_U) == NULL) return -ENOMEM;

            if (phdr.p_filesz > 0) {
                // Fetch elf segmemt into the memory at phdr.p_vaddr
                fetch = iofetch(io, phdr.p_offset, (void *)(uintptr_t)phdr.p_vaddr, phdr.p_filesz);
                if (fetch < 0) return fetch;
                if (fetch != phdr.p_filesz) return -EIO;                    // ensure full segment is loaded
            }
            // fills in the memory that is not written into with 0's
            if (phdr.p_memsz > phdr.p_filesz) memset((void *)(uintptr_t)(phdr.p_vaddr + phdr.p_filesz), 0, phdr.p_memsz - phdr.p_filesz);

            // update permissions/flags
            int flags = PTE_U;                                              // user flag should always be set
            if ((phdr.p_flags & PF_R) != 0) flags |= PTE_R;                 // determine if read flag should be set
            if ((phdr.p_flags & PF_W) != 0) flags |= PTE_W;                 // determine if write flag should be set
            if ((phdr.p_flags & PF_X) != 0) flags |= PTE_X;                 // determine if executeable flag should be set
            set_range_flags((void*)(uintptr_t)phdr.p_vaddr, phdr.p_memsz, flags);             // updates page table with proper flags
        }
    }
    
    // Entry check
    if (ehdr.e_entry == 0) return -EBADFMT;
    if (eptr != NULL) *eptr = (void (*)(void))(uintptr_t)ehdr.e_entry;

    return 0;
}
