# RISC-V Operating System
 
This repository contains a RISC-V operating system kernel built from the ground up for QEMU's virt machine. It was developed across a series of machine problems for ECE 391 (Computer Systems Engineering), starting from a bare bootloader and ending with a kernel that supports virtual memory, user processes, a custom file system, and a working shell with Unix-style utilities.
 
## Features
 
- Interrupt-driven drivers for the PLIC, UART, RTC, VirtIO entropy device, and VirtIO block device
- Cooperative multithreading with condition variables and read/write locks
- Sv39 virtual memory with lazy page allocation and per-process address spaces
- User processes with system calls for file I/O, process control, and pipes
- NGFS, a custom FAT-inspired file system, backed by a block cache
- An ELF loader for running user programs from disk
- fork(), pipe(), and a shell supporting sequential execution, background execution, I/O redirection, and piping
- Unix-style utilities: cat, ls, rm, touch, wc, xargs, date, echo
## Repository Structure
 
```
sys/                Kernel source
sys/dev/             Device drivers (UART, RTC, VirtIO block, VirtIO entropy)
sys/fs/              File system implementations (NGFS, TarFS)
sys/board/           Board and platform initialization
usr/                 User-space runtime and programs
usr/progs/           Shell and command-line utilities
usr/games/           Included test programs (Trek, Rogue, Zork)
util/                Host-side tools for building file system images
```
 
