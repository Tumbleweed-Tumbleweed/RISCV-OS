// process.c - Processes and process manager
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef PROCESS_TRACE
#define TRACE
#endif

#ifdef PROCESS_DEBUG
#define DEBUG
#endif

#include "process.h"

#include "conf.h"
#include "elf.h"
#include "error.h"
#include "filesys.h"
#include "heap.h"
#include "memory.h"
#include "misc.h"
#include "riscv.h"
#include "string.h"
#include "thread.h"
#include "trap.h"
#include "io.h"
#include "intr.h"

// ---INTERNAL FUNCTION DECLARATIONS--- //
static int build_stack(void * stack, int argc, char ** argv);
static void fork_func(struct condition * forked, struct trap_frame * tfr);

// ---INTERNAL GLOBAL VARIABLES--- //
static struct process main_proc;

// ---EXPORTED GLOBAL VARIABLES--- //
char procmgr_initialized = 0;

// ---EXPORTED FUNCTION DEFINITIONS--- //
void procmgr_init(void) {
    assert(memory_initialized && heap_initialized);
    assert(!procmgr_initialized);

    main_proc.tid = running_thread();
    main_proc.mtag = active_mspace();
    thread_attach_process(main_proc.tid, &main_proc);
    procmgr_initialized = 1;
}

/*
INPUTS: struct io * exeio - pointer to the executable 
        int argc - number of arguments into argv
        char ** argv - string table of arguments
OUTPUTS: returns 0 on success; negative error code on failure
DESCRIPTION: takes current thread and have it execute new user process
SIDE EFFECTS: resets and allocates memory, calls trap_frame_jump
*/
int process_exec(struct io * exeio, int argc, char ** argv) {
    struct process *curr_proc = running_thread_process();

    // we must copy the argv array itself as well as the strings into a memory location which is accessible by the new user program we will execute.
    char *argv_copy[argc];
    char strings[argc][256];                    // 2d array conating argc number of max length 256 - 1 null terminated strings
    for (int i = 0; i < argc; i++) {            // copy the strings and add null terminator for each
        strncpy(strings[i], argv[i], 255);
        strings[i][255] = '\0';
        argv_copy[i] = strings[i];
    }

    reset_active_mspace();                      // reset currently active memory space
    void (*entry_pointer)(void);                // elf load executable; stores entry poin in entry_pointer
    int elf_er = elf_load(exeio, &entry_pointer);
    if (elf_er != 0) return elf_er;
    
    // update ref to exeio, and then drop exeio
    if (curr_proc->exeio != NULL) iodropref(curr_proc->exeio);   // drop the refrences of the executable file
    curr_proc->exeio = ioaddref(exeio);                          // update ref to exeio
    iodropref(exeio);
    

    uintptr_t stack_vma = UMEM_END_VMA - PAGE_SIZE;                     // place stack at end of virtual memory 
    void * stack_page = alloc_phys_page();
    if (stack_page == NULL) return -ENOMEM;
    memset(stack_page, 0, PAGE_SIZE);
    if (map_page(stack_vma,stack_page, PTE_R | PTE_W | PTE_U) == NULL) { // checked if map page works
        free_phys_page(stack_page);
        return -ENOMEM;
    }

    int stack_size = build_stack(stack_page, argc, argv_copy);          // build the stack using argv
    if (stack_size < 0) {                                               // if stack build failure unmap and return error
        unmap_and_free_range((void *)stack_vma, PAGE_SIZE);
        return stack_size;
    }

    struct trap_frame tfr;                                                          // build the trap frame so it is prepared for user mode
    memset(&tfr, 0, sizeof(tfr));
    tfr.sepc = (void*)(uintptr_t)entry_pointer;                                            // put entry pointer into sepc
    tfr.sstatus = ((csrr_sstatus() & ~RISCV_SSTATUS_SPP) | RISCV_SSTATUS_SPIE);     // indicates user mode, enables interrupts, 
    tfr.a0 = argc;                                                                  // set a0 to argc, trap fra
    tfr.a1 = stack_vma + (PAGE_SIZE - stack_size);                                  // set a1 to point to our copied argv
    tfr.sp = (void *)(stack_vma + PAGE_SIZE - stack_size);                          // points to next avail stack adress

    void *kernel_stack = (char *)running_thread_stack_anchor() - sizeof(struct trap_frame);
    csrw_sscratch((uintptr_t)kernel_stack);                                         // place kernel stack pointer into sscratch
    trap_frame_jump(&tfr, kernel_stack);                                            // call trap frame jump to user mode at entry of new user program
    return -EACCESS;                       // this should never be reached
}

/*
INPUTS: const struct trap_frame * tfr - pointer to the trap frame
OUTPUTS: returns the child processes TID
DESCRIPTION: forks the current process by creating a child (clone) of itself
SIDE EFFECTS: allocates memory; creates a new thread and process; iotab memspace changed
*/
int process_fork(const struct trap_frame * tfr) {
    // init all process
    struct process * parent; // current process (TP->proc)
    struct process * child; // new child process
    struct condition fork_wait; // for the parent to wait while the child is using the trap frame
    int child_tid; // child's thread id

    // allocate new struct process for the child
    // makes the space in memory for child and sets to 0
    child = kcalloc(1, sizeof(struct process));
    if (child == NULL) return -ENOMEM;
    
    parent = running_thread_process();

    //  clone the current address space using clone_active_mspace(
    child->mtag = clone_active_mspace();

    // ensure reference counts are correct with ioaddref()
    if (parent->exeio != NULL) {
        child->exeio = ioaddref(parent->exeio);
    }
    
    // copy the parent’s I/O object pointers.
    for (int i = 0; i < PROC_IOMAX; i++) {
        if (parent->iotab[i] == NULL) {
            child->iotab[i] = NULL;
        }
        else {
            child->iotab[i] = ioaddref(parent->iotab[i]);
        }
    }

    // spawn the new thread and attach
    condition_init(&fork_wait, "fork_wait");

    int pie = disable_interrupts();
    child_tid = spawn_thread("forked", (void (*)(void))fork_func, &fork_wait, tfr);

    // if child thread fails then clean up everyting
    if (child_tid < 0) {
        mtag_t parent_space = active_mspace();                  // clear the childs memspace first
        switch_mspace(child->mtag);                             // we must switch to the childs memspace first to be able to discard it
        discard_active_mspace();
        switch_mspace(parent_space);

        // clear refrence count and i/o object pointers
        if (child->exeio != NULL) iodropref(child->exeio);

        for (int i = 0; i < PROC_IOMAX; i++) {
            if (child->iotab[i] != NULL) iodropref(child->iotab[i]);
        }
        // free child and return error
        kfree(child);
        restore_interrupts(pie);
        return child_tid;
    }
    
    // attach child to the new thread
    child->tid = child_tid;
    thread_attach_process(child_tid, child);
    
    // wait for child to be done
    condition_wait(&fork_wait);             // interrupts are back on

    // user-space _fork() returns the spawned child’s TID in the parent
    return child_tid;
}

/*
INPUTS: no inputs
OUTPUTS: no output
DESCRIPTION: terminates the currently running process and reclaims its resources
SIDE EFFECTS: process is killed; memory is free; if main process is exited system shutsdown
*/
void process_exit(void) {
    struct process *curr_proc = running_thread_process();

    if (curr_proc == &main_proc) {              // if the main process is being exited shutdown
        flush_all_filesys();
        shutdown();
    }
    
    if (curr_proc->exeio != NULL) {             // drop the refrences of the executable file
        iodropref(curr_proc->exeio);
        curr_proc->exeio = NULL;
    }

    for (int i = 0; i < PROC_IOMAX; i++){       // drop iotab refrences
        if (curr_proc->iotab[i] != NULL) {
            iodropref(curr_proc->iotab[i]);
            curr_proc->iotab[i] = NULL;
        }
    }

    discard_active_mspace();                    // discard the active memory space.   

    int proc_tid = curr_proc->tid;
    thread_attach_process(proc_tid, NULL);      // detach process from thread

    kfree(curr_proc);                           // free proccess memory
    exit_running_thread();                      // finally exit the user process
}

// ---INTERNAL FUNCTION DEFINITIONS---//
int build_stack(void * stack, int argc, char ** argv) {
    size_t stksz, argsz;
    uintptr_t * newargv;
    char * p;
    int i;

    // We need to be able to fit argv[] on the initial stack page, so _argc_
    // cannot be too large. Note that argv[] contains argc+1 elements (last one
    // is a NULL pointer).

    if (PAGE_SIZE / sizeof(char*) - 1 < argc)
        return -ENOMEM;
    
    stksz = (argc+1) * sizeof(char*);

    // Add the sizes of the null-terminated strings that argv[] points to.

    for (i = 0; i < argc; i++) {
        argsz = strlen(argv[i])+1;
        if (PAGE_SIZE - stksz < argsz)
            return -ENOMEM;
        stksz += argsz;
    }

    // Round up stksz to a multiple of 16 (RISC-V ABI requirement).

    stksz = ROUND_UP(stksz, 16);
    assert (stksz <= PAGE_SIZE);

    // Set _newargv_ to point to the location of the argument vector on the new
    // stack and set _p_ to point to the stack space after it to which we will
    // copy the strings. Note that the string pointers we write to the new
    // argument vector must point to where the user process will see the stack.
    // The user stack will be at the highest page in user memory, the address of
    // which is `(UMEM_END_VMA - PAGE_SIZE)`. The offset of the _p_ within the
    // stack is given by `p - newargv'.

    newargv = stack + PAGE_SIZE - stksz;
    p = (char*)(newargv+argc+1);

    for (i = 0; i < argc; i++) {
        newargv[i] = (UMEM_END_VMA - PAGE_SIZE) + ((void*)p - (void*)stack);
        argsz = strlen(argv[i])+1;
        memcpy(p, argv[i], argsz);
        p += argsz;
    }

    newargv[argc] = 0;
    return stksz;
}

/*
INPUTS: struct condition * done - pointer to the condition variable
        struct trap_frame * tfr - pointer to the trap frame
OUTPUTS: no output
DESCRIPTION: finalizes set up for child process and begins its exectuion
SIDE EFFECTS: execution of the process is continued in umode on child process
*/
void fork_func(struct condition * done, struct trap_frame * tfr) {
    struct trap_frame child_frame;
    memcpy(&child_frame, tfr, sizeof(child_frame));
    condition_broadcast(done);
    child_frame.a0 = 0;                // child is a copy of parent except its TID is 0 (TID == PID)

    void * kernel_stack = (char *)running_thread_stack_anchor() - sizeof(struct trap_frame);
    csrw_sscratch((uintptr_t)kernel_stack);
    trap_frame_jump(&child_frame, kernel_stack);
}