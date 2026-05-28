// syscall.S - System call function definitions
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "scnum.h"

        .text

        .global _exit
        .type   _exit, @function
_exit:
        li      a7, SYSCALL_EXIT
        ecall
        ret

        .global _exec
        .type   _exec, @function
_exec:
        li      a7, SYSCALL_EXEC
        ecall
        ret

        .global _fork
        .type   _fork, @function
_fork:
        li      a7, SYSCALL_FORK
        ecall
        ret

        .global _wait
        .type   _wait, @function
_wait:
        li      a7, SYSCALL_WAIT
        ecall
        ret

        .global _print
        .type   _print, @function
_print:
        li      a7, SYSCALL_PRINT
        ecall
        ret

        .global _usleep
        .type   _usleep, @function
_usleep:
        li      a7, SYSCALL_USLEEP
        ecall
        ret

        .global _open
        .type   _open, @function
_open:
        li      a7, SYSCALL_OPEN
        ecall
        ret

        .global _create
        .type   _create, @function
_create:
        li      a7, SYSCALL_CREATE
        ecall
        ret

        .global _delete
        .type   _delete, @function
_delete:
        li      a7, SYSCALL_DELETE
        ecall
        ret

        .global _close
        .type   _close, @function
_close:
        li      a7, SYSCALL_CLOSE
        ecall
        ret

        .global _read
        .type   _read, @function
_read:
        li      a7, SYSCALL_READ
        ecall
        ret

        .global _write
        .type   _write, @function
_write:
        li      a7, SYSCALL_WRITE
        ecall
        ret

        .global _ioctl
        .type   _ioctl, @function
_ioctl:
        li      a7, SYSCALL_IOCTL
        ecall
        ret

        .global _pipe
        .type   _pipe, @function
_pipe:
        li      a7, SYSCALL_PIPE
        ecall
        ret

        .global _iodup
        .type   _iodup, @function
_iodup:
        li      a7, SYSCALL_IODUP
        ecall
        ret

        .end