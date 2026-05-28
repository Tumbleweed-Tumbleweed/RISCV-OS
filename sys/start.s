# start.s - Kernel startup
# 
# Copyright (c) 2024-2026 University of Illinois
# SPDX-License-identifier: NCSA
#

# Imported symbols

        .global _smode_trap_entry # defined in trap.s

# QEMU RISC-V virt system zero-stage bootloader jumps to 0x8000'0000 to start
# kernel. The linker script kernel.ld arranges for start.s to be placed here.
 
        .section        .text.start, "xa", @progbits
        .balign         4

# On entry, a0 contains the hart id and a1 contains a pointer to the DTB. Do not
# touch these registers so they get passed through to main().

smode_start:

        # Set trap handler for S mode (defined in trap.s)

        la      t0, _smode_trap_entry
        csrw    stvec, t0

        # Enable access to cycle, time, and instret counters in U mode

        csrs    scounteren, 7

        # Initialize sscratch (must be zero in S mode)

        csrw    sscratch, zero

        # Set initial frame and stack pointer for main thread. Set up ra so we
        # jump to sbi_shutdown() if main returns.

        mv      fp, zero
        la      sp, _main_stack_anchor
        la      ra, sbi_shutdown # sbi.s
        j       main

        .section        .data.stack, "wa", @progbits
        .balign		16
        
        .equ		MAIN_STACK_SIZE, 4096

        .global		_main_stack_lowest
        .type		_main_stack_lowest, @object
        .size		_main_stack_lowest, MAIN_STACK_SIZE

        .global		_main_stack_anchor
        .type		_main_stack_anchor, @object
        .size		_main_stack_anchor, 16

_main_stack_lowest:
        .fill   MAIN_STACK_SIZE, 1, 0xA5

_main_stack_anchor:
        .dword  0 # ktp
        .dword  0 # kgp
        .end
