// excp.c - Exception handing
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#include <stddef.h>

#include "console.h"
#include "intr.h"
#include "misc.h"
#include "riscv.h"
#include "string.h"
#include "thread.h"
#include "trap.h"

#include "memory.h"
#include "process.h"

// ---IMPORTED FUNCTION DECLARATIONS--- //
extern void handle_syscall(struct trap_frame* tfr);  // syscall.c

// ---INTERNAL GLOBAL VARIABLES--- //

// Array of exception names indexed by their exception code
static const char* const excp_names[] = {
    [RISCV_SCAUSE_INSTR_ADDR_MISALIGNED] = "Misaligned instruction address",
    [RISCV_SCAUSE_INSTR_ACCESS_FAULT] = "Instruction access fault",
    [RISCV_SCAUSE_ILLEGAL_INSTR] = "Illegal instruction",
    [RISCV_SCAUSE_BREAKPOINT] = "Breakpoint",
    [RISCV_SCAUSE_LOAD_ADDR_MISALIGNED] = "Misaligned load address",
    [RISCV_SCAUSE_LOAD_ACCESS_FAULT] = "Load access fault",
    [RISCV_SCAUSE_STORE_ADDR_MISALIGNED] = "Misaligned store address",
    [RISCV_SCAUSE_STORE_ACCESS_FAULT] = "Store access fault",
    [RISCV_SCAUSE_ECALL_FROM_UMODE] = "Environment call from U mode",
    [RISCV_SCAUSE_ECALL_FROM_SMODE] = "Environment call from S mode",
    [RISCV_SCAUSE_INSTR_PAGE_FAULT] = "Instruction page fault",
    [RISCV_SCAUSE_LOAD_PAGE_FAULT] = "Load page fault",
    [RISCV_SCAUSE_STORE_PAGE_FAULT] = "Store page fault"};

// ---EXPORTED FUNCTION DEFINITIONS--- //
void handle_smode_exception(unsigned int cause, struct trap_frame* tfr) {
    const char* name = NULL;
    char msgbuf[80];

    if (0 <= cause && cause < sizeof(excp_names) / sizeof(excp_names[0]))
        name = excp_names[cause];

    if (name != NULL) {
        switch (cause) {
            case RISCV_SCAUSE_LOAD_PAGE_FAULT:
            case RISCV_SCAUSE_STORE_PAGE_FAULT:
            case RISCV_SCAUSE_INSTR_PAGE_FAULT:
            case RISCV_SCAUSE_LOAD_ADDR_MISALIGNED:
            case RISCV_SCAUSE_STORE_ADDR_MISALIGNED:
            case RISCV_SCAUSE_INSTR_ADDR_MISALIGNED:
            case RISCV_SCAUSE_LOAD_ACCESS_FAULT:
            case RISCV_SCAUSE_STORE_ACCESS_FAULT:
            case RISCV_SCAUSE_INSTR_ACCESS_FAULT:
                snprintf(msgbuf, sizeof(msgbuf),
                    "%s at %p for %p in S mode", name,
                    (void*)tfr->sepc, (void*)csrr_stval());
                break;
            default:
                snprintf(msgbuf, sizeof(msgbuf),
                    "%s at %p in S mode", name, (void*)tfr->sepc);
        }
    } else {
        snprintf(msgbuf, sizeof(msgbuf),
            "Exception %d at %p in S mode", cause, (void*)tfr->sepc);
    }

    panic(msgbuf);
}

/*
INPUTS: unsigned int cause - indicates the reason for entering exception handler
        struct trap_frame * tfr - pointer to the trap frame
OUTPUTS: no outputs
DESCRIPTION: handles exceptionst that occur in usermodes
SIDE EFFECTS: may exit current process depending on exception; may call a syscall that may alter memory
*/
void handle_umode_exception(unsigned int cause, struct trap_frame * tfr) {
    switch (cause) {
        case RISCV_SCAUSE_LOAD_PAGE_FAULT:
        case RISCV_SCAUSE_STORE_PAGE_FAULT:             // lazy page allocation
            if (handle_umode_page_fault(tfr, csrr_stval()) == 0) process_exit();
            break;

        case RISCV_SCAUSE_ECALL_FROM_UMODE:             // uses ecall to pass through syscalls
            handle_syscall(tfr);
            break;

        default:                // for all other cases exit current process
            process_exit();
            break;
    }
}
