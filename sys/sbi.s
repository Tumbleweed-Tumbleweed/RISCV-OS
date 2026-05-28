# sbi.s - Supervisor binary interface
#
# Copyright (c) 2026 University of Illinois
# SPDX-License-identifier: NCSA
#

        .text

        .global sbi_set_timer
        .type   sbi_set_timer, @function

sbi_set_timer:
        li      a7, 0x0 # EID
        ecall
        ret


        .global sbi_console_putchar
        .type   sbi_console_putchar, @function

sbi_console_putchar:
        li      a7, 0x1 # EID
        ecall
        ret


        .global sbi_console_getchar
        .type   sbi_console_getchar, @function

sbi_console_getchar:
        li      a7, 0x2 # EID
        ecall
        ret


        .global sbi_shutdown
        .type   sbi_shutdown, @function

sbi_shutdown:
        li      a7, 0x8 # EID
        ecall
        # If shutdown ecall fails, use wfi in infinite loop
        csrw    sie, zero
1:      wfi
        j       1b
