// process.h - Processes and process manager
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _PROCESS_H_
#define _PROCESS_H_

/*!
 * @brief Maximum number of I/O objects associated with a process
 */
#ifndef PROCESS_IOMAX
#define PROCESS_IOMAX 16
#endif

#include "conf.h"
#include "thread.h"
#include "memory.h" // mtag_t
#include "io.h" // struct io *

// ---EXPORTED TYPE DEFINITIONS--- //

struct process {
    int tid;                        // processes thread ID
    mtag_t mtag;                    // processes memory tag; (the Sv39 )
    struct io * exeio;              // pointer to the executable file
    struct io * iotab[PROC_IOMAX];  // pointer to the processes iotable
};

// ---EXPORTED FUNCTION DECLARATIONS--- //
extern char procmgr_initialized;
extern void procmgr_init(void);
extern int process_exec(struct io * exefile, int argc, char ** argv);
extern int process_fork(const struct trap_frame * tfr);
extern void __attribute__((noreturn)) process_exit(void);
static inline struct process * current_process(void);

// ---INLINE FUNCTION DEFINITIONS--- //
static inline struct process * current_process(void) {
    return running_thread_process();
}

#endif  // _PROCESS_H_