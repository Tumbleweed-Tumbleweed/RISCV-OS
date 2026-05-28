// thread.c - Thread creation and synchronization
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef THREAD_TRACE
#define TRACE
#endif

#ifdef THREAD_DEBUG
#define DEBUG
#endif

#include "thread.h"

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#include "heap.h"
#include "string.h"
#include "riscv.h"
#include "intr.h"
#include "error.h"
#include "misc.h"
#include "timer.h"

#include "process.h"
#include "memory.h"

// ---COMPILE-TIME PARAMETERS--- //
#ifndef NTHR // maximum number of threads
#define NTHR 32
#endif

#ifndef SCHED_SLICE_MS // scheduler time slice
#define SCHED_SLICE_MS 20
#endif

// ---EXPORTED GLOBAL VARIABLES--- //
char thrmgr_initialized = 0;

// ---INTERNAL TYPE DEFINITIONS---//
enum thread_state {
    THREAD_UNDEFINED = 0,
    THREAD_WAITING,
    THREAD_RUNNING,
    THREAD_READY,
    THREAD_EXITED
};

struct thread_context {
    uintptr_t s[12];
    void * ra;
    void * sp;
};

struct thread_stack_anchor {
    struct thread * ktp;
    void * kgp;
};

// [MP3cp2] The /thread_stack_anchor/ structure is placed at the base of the
// stack to allow us to restore the thread pointer when entering the kernel from
// U mode.
// @TODO MP3CP2 CHANGES - ANDREW
struct thread {
    struct thread_context ctx;                  // must be first (thrasm.s)
    int id;                                     // index into thrtab[] - Thread id number
    enum thread_state state;                    // enum of threads current states
    const char * name;                          // pointer to thread name
    struct thread_stack_anchor * stack_anchor;  // pointer to the top of threads stack (origin)
    void * stack_lowest;                        // pointer to the bottom of threads stack (end of stack)
    struct process * proc;                      // pointer to current thread process
    struct thread * parent;                     // pointer to threads parent
    struct thread * list_next;                  // pointer to the next thread
    struct condition * wait_cond;               // threads condition variable
    struct condition child_exit;                // holds condition for parent for when child exits

    // (you may add additional structure members here)
    struct lock_status {                        // struct that tracks how many of each lock thread currently holds
        struct rwlock *lock;
        long cnt;           // number of times lock is held
        int readwrite;      // 0 for read, 1 for write
    } lock_hold[16];
};

// ---INTERNAL MACRO DEFINITIONS---//

// Pointer to running thread, which is kept in the tp (x4) register.
#define TP ((struct thread*)get_thread_pointer())

// Macro for changing thread state. If compiled for debugging (DEBUG is
// defined), prints function that changed thread state.
#define set_thread_state(t,s) do { \
    debug("Thread <%s:%d> state changed from %s to %s by <%s:%d> in %s", \
        (t)->name, (t)->id, \
        thread_state_name((t)->state), \
        thread_state_name(s), \
        TP->name, TP->id, \
        __func__); \
    (t)->state = (s); \
} while (0)

// ---INTERNAL FUNCTION DECLARATIONS--- //
static void init_main_thread(void);
static void init_idle_thread(void);

// Initializes the main and idle threads. called from threads_init().

static const char * thread_state_name(enum thread_state state)
    __attribute__ ((unused)); // may be unused

// Returns a string representing a thread state. Used by debug and trace
// statements, so marked unused to avoid compiler warnings.

static void tlclear(struct thread_list * list);
static int tlempty(const struct thread_list * list);
static struct thread * tlpeek(const struct thread_list * list);
static void tlinsert(struct thread_list * list, struct thread * thr);
static struct thread * tlremove(struct thread_list * list);
static void tlappend(struct thread_list * l0, struct thread_list * l1);
static void tlprepend(struct thread_list * l0, struct thread_list * l1);

static void idle_thread_func(void);

// IMPORTED QUASI-FUNCTION DECLARATIONS
// defined in thrasm.s
//

extern struct thread * switch_threads(struct thread * next_thread);
extern void start_thread(void);

// ---INTERNAL GLOBAL VARIABLES--- //
#define MAIN_TID 0
#define IDLE_TID (NTHR-1)

static struct thread main_thread;
static struct thread idle_thread;

extern char _main_stack_lowest[]; // from start.s
extern char _main_stack_anchor[]; // from start.s


static struct thread main_thread = {
    .id = MAIN_TID,
    .name = "main",
    .state = THREAD_RUNNING,
    .stack_anchor = (void*)_main_stack_anchor,
    .stack_lowest = _main_stack_lowest,
    .child_exit = { .name = "main_thread.child_exit" }
};

extern char _idle_stack_lowest[]; // from thrasm.s
extern char _idle_stack_anchor[]; // from thrasm.s

static struct thread idle_thread = {
    .id = IDLE_TID,
    .name = "idle",
    .state = THREAD_READY,
    .parent = &main_thread,
    .stack_anchor = (void*)_idle_stack_anchor,
    .stack_lowest = _idle_stack_lowest,
    .ctx.sp = _idle_stack_anchor,
    .ctx.ra = &idle_thread_func
};

static struct thread * thrtab[NTHR] = {
    [MAIN_TID] = &main_thread,
    [IDLE_TID] = &idle_thread
};

static struct thread_list ready_list = {
    .head = &idle_thread,
    .tail = &idle_thread
};

// ---EXPORTED THREAD FUNCTION DEFINITIONS--- //
int running_thread(void) {
    return TP->id;
}

void thrmgr_init(void) {
    trace("%s()", __func__);


    init_main_thread();
    init_idle_thread();
    set_thread_pointer(&main_thread);
    thrmgr_initialized = 1;
}

/*
INPUTS: const char * name - name of thread
        void (*entry)(void) - 
        variatic variables (1-8) -
OUTPUTS: returns the TID of the newly spawned thread (or an error code)
DESCRIPTION: creates and intitializes a brand new thread (always called from another thread)
SIDE EFFECTS: new memory is allocated; interupts disabled during process; ready list updated
*/
int spawn_thread (const char * name, void (*entry)(void), ...) {
    disable_interrupts();
    int tid;                            // start at 1 as idle and main are pre allocated to NTHR-1 and 0
    for (tid = 1; tid < IDLE_TID; tid++) {                 
        if (thrtab[tid] == NULL) break;
    }

    if (tid == IDLE_TID) {                  // if tid is 31 then we can not spawn any more threads
        enable_interrupts();
        return -EMTHR;                      // EMTHR indicates to system/user we are at max threads
    }

    struct thread * thr = kcalloc(1, sizeof(*thr));        //allocate thread struct memory
    if (thr == NULL) {
        enable_interrupts();
        return -ENOMEM;
    }

    void * stack = alloc_phys_page();                 // allocate the thread stack memory
    if (stack == NULL){
        kfree(thr);
        enable_interrupts();
        return -ENOMEM;
    }

    thr->id = tid;
    thr->state = THREAD_READY;
    thr->name = (name != NULL) ? name : "anon";             // same as shown in rwlock_init
    thr->stack_anchor = (struct thread_stack_anchor *)((char *)stack + PAGE_SIZE) - 1;
    thr->stack_anchor->ktp = thr;
    //thr->stack_anchor->kgp = NULL; //@TODO ????
    thr->stack_lowest = stack;
    thr->proc = NULL;
    thr->parent = TP;
    thr->list_next = NULL;
    thr->wait_cond = NULL;
    condition_init(&thr->child_exit, "thr_child_exit");

    thr->ctx.sp = thr->stack_anchor - 1;   //maybe -1
    thr->ctx.ra = start_thread; 

    va_list ap;                                     // variadic arguments intialization
    va_start(ap, entry);
    thr->ctx.s[0] = (uintptr_t)entry;
    thr->ctx.s[1] = va_arg(ap, uintptr_t);
    thr->ctx.s[2] = va_arg(ap, uintptr_t);
    thr->ctx.s[3] = va_arg(ap, uintptr_t);
    thr->ctx.s[4] = va_arg(ap, uintptr_t);
    thr->ctx.s[5] = va_arg(ap, uintptr_t);
    thr->ctx.s[6] = va_arg(ap, uintptr_t);
    thr->ctx.s[7] = va_arg(ap, uintptr_t);
    thr->ctx.s[8] = va_arg(ap, uintptr_t);
    va_end(ap);

    thrtab[tid] = thr;                         // on intilziation completion update table, ready list and re enable interupt
    tlinsert(&ready_list, thr);
    enable_interrupts();
    return tid;
}

const char * thread_name(int tid) {
    assert (0 <= tid && tid < NTHR);
    assert (thrtab[tid] != NULL);
    return thrtab[tid]->name;
}

const char * running_thread_name(void) {
    return TP->name;
}

/*
INPUTS: no inputs
OUTPUTS: no outputs
DESCRIPTION: Ends current running thread; deals with its children; does not free its memory
SIDE EFFECTS: children of this thread that still run will become orphans, interupts disabled and yeild_running_thread it called
*/
void exit_running_thread(void) {
    if (TP == &main_thread) halt();                                     // if current thread is the main thread we halt
    disable_interrupts();
    set_thread_state(TP, THREAD_EXITED);                                // current thread state is set to EXITED

    for (int i = 0; i < 32; i++) {                                      // loop through and clear exited children
        if (thrtab[i] == NULL || thrtab[i]->parent != TP) continue;           // move on if no thread or if it is not a child of current thread
        if (thrtab[i]->state == THREAD_EXITED){
            struct thread *temp = thrtab[i];
            thrtab[i] = NULL;
            kfree(temp);
        } else thrtab[i]->parent = NULL;                                // if child is not exited kill its parent (make it an orphan)
    }

    if (TP->parent != NULL) condition_broadcast(&TP->parent->child_exit);
    yield_running_thread();
    enable_interrupts();                        // im pretty sure this line is never reached ???
}

/*
INPUTS: no inputs
OUTPUTS: no outputs
DESCRIPTION: prepares current thread to be put to sleep and wakes the next thread on the ready list to be switched to
SIDE EFFECTS: ready list may be updated; calls switch_running_thread; interupts disabled
*/
void yield_running_thread(void) {
    extern void switch_running_thread(struct thread *); // thrasm.s
    trace("%s() in <%s:%d>", __func__, TP->name, TP->id);           // The idle thread is always runnable, and the idle thread only calls
    assert (!tlempty(&ready_list));                                 // yield() if the ready_list is not empty.
    disable_interrupts();
    if (TP->state == THREAD_RUNNING) {                  //update status of current thread, if it is running still, set it to ready
        set_thread_state(TP, THREAD_READY);
        tlinsert(&ready_list, TP);
    }
    struct thread * waking_thread = tlremove(&ready_list);
    assert(waking_thread != NULL);                      // must have another to pass to or else something is wrong
    set_thread_state(waking_thread, THREAD_RUNNING);
    
    if (waking_thread->proc != NULL) switch_mspace(waking_thread->proc->mtag);          // thread must also yeild to correct memory space

    switch_running_thread(waking_thread);
    enable_interrupts();
}

// The finish thread function is only called from switch_running_thread()
// in thrasm.s. It is not declared in thread.h.

/*
INPUTS: struct thread * susp_thread - pointer to the thread that will be reclaimed
OUTPUTS: no outputs
DESCRIPTION: runs following a switch in the new threads context and frees the previous threads resources
SIDE EFFECTS: thread passed through is freed
*/
void finish_thread_switch(struct thread * susp_thread) {
    if (susp_thread == NULL) return;                // ensure we are actually passed a thread pointer

    if (susp_thread->state == THREAD_EXITED) {
        free_phys_page(susp_thread->stack_lowest);
        if (susp_thread->parent == NULL) {          // deal with orphaned children
            thrtab[susp_thread->id] = NULL;
            kfree(susp_thread);
        }
    }
    return;
}

/*
INPUTS: int u_tid - index of childs thread table location or 0 to indicate any child
OUTPUTS: returns the TID of the child that is returned
DESCRIPTION: the thread that calls this function waits until either the specified child or any child exits
SIDE EFFECTS: thread disables interupts and may condition wait, child thread will be removed from table and is freed
*/
int join_thread(int u_tid) {
    trace("%s(%d) in <%s:%d>", __func__, u_tid, TP->name, TP->id);
    if (u_tid < 0 || u_tid >= NTHR) return -ECHILD;          // validate input path

    disable_interrupts();
    if (u_tid != 0) {
        if (thrtab[u_tid] == NULL || thrtab[u_tid]->parent != TP){                  // if the given child is not a child our thread exit
            enable_interrupts();
            return -ECHILD;
        }

        while (thrtab[u_tid]->state != THREAD_EXITED) {
            enable_interrupts();                    // we must enable disable again here as condition wait will enable at the end of its function
            condition_wait(&TP->child_exit);
            disable_interrupts();

            if (thrtab[u_tid] == NULL) {            // as interupts were enabled we need to make sure that we never went null
                enable_interrupts();
                return -ECHILD;
            }
        }

        struct thread *temp = thrtab[u_tid];        // clean up and delete the child (BYE sBYE)
        thrtab[u_tid] = NULL;
        kfree(temp);
        enable_interrupts();
        return u_tid;
    
    } else {                                        // case where u_tid == 0
        while (1 == 1) {                            // we want to infinitely loop until a child has exited
            int c_joined = 0;                       // essentially a boolean variable letting us know if any children exist
            for (int i = 1; i < IDLE_TID; i++){     // we have to check for every index in the table to see if they are a child
                if (thrtab[i] == NULL) continue;
                if (thrtab[i]->parent != TP) continue;
                c_joined = 1;
                
                if(thrtab[i]->state == THREAD_EXITED)  {    // once child is exited free it and remove from tables
                    struct thread *temp = thrtab[i];
                    thrtab[i] = NULL;
                    kfree(temp);
                    enable_interrupts();
                    return i;
                }
            }

            if (c_joined == 0) {
                enable_interrupts();
                return -ECHILD;
            }
            enable_interrupts();
            condition_wait(&TP->child_exit);           // condition wait until the child exits
            disable_interrupts();
        }
        
    }
}

struct process * thread_process(int tid) {
    assert (0 <= tid && tid < NTHR);
    assert (thrtab[tid] != NULL);
    return thrtab[tid]->proc;
}

struct process * running_thread_process(void) {
    return TP->proc;
}

void thread_attach_process(int tid, struct process * proc) {
    assert (0 <= tid && tid < NTHR);
    assert (thrtab[tid] != NULL);
    // assert (thrtab[tid]->proc == NULL);
    // assert (proc != NULL);
    thrtab[tid]->proc = proc;
}

void * running_thread_stack_anchor(void) {
    return TP->stack_anchor;
}

// ---EXPORTED CONDITION VARIABLE FUNCTION DEFINITIONS--- //

void condition_init(struct condition * cond, const char * name) {
    memset(cond, 0, sizeof(*cond));
    tlclear(&cond->wait_list);
    cond->name = (name != NULL) ? name : "anon";
}

const char * condition_name(const struct condition * cond) {
    return cond->name;
}

/*
INPUTS: struct condition * cond - pointer to condition struct containg waitlist
OUTPUTS: no outputs
DESCRIPTION: adds current thread to waitlist; suspends current thread
SIDE EFFECTS: current thread state set to THREAD_WAITING
*/
void condition_wait(struct condition * cond) {
    disable_interrupts();           // critcal code block we do not want this to have interupts
    trace("%s(<%s>) in <%s:%d>", __func__, cond->name, TP->name, TP->id);
    assert(TP->state == THREAD_RUNNING);

    TP->wait_cond = cond;
    set_thread_state(TP, THREAD_WAITING);
    tlinsert(&cond->wait_list, TP);
    enable_interrupts();

    yield_running_thread();                     // yeild thread as it is not waitings
}

/*
INPUTS: struct condition * cond - pointer to condition struct containg waitlist
OUTPUTS: no outputs
DESCRIPTION: Wakes up all threads on the waitlist, adds them to the ready list
SIDE EFFECTS: updates all threads in waitlsit to THREAD_READY
*/
void condition_broadcast(struct condition * cond) {
    disable_interrupts();           // critcal code block we do not want this to have interupts
    trace("%s(<%s>) in <%s:%d>", __func__, cond->name, TP->name, TP->id);
    if (tlempty(&cond->wait_list)) {             // Fast path: if there are no threads waiting, return.
        enable_interrupts();
        return;
    }
    struct thread_list updated_list;
    struct thread * thr;
    tlclear(&updated_list);                         // using this as init

    while (!tlempty(&cond->wait_list)) {
        thr = tlremove(&cond->wait_list);           // remove returns the removed thread so we dont need to peak
        set_thread_state(thr, THREAD_READY);
        thr->wait_cond = NULL;
        tlinsert(&updated_list, thr);                // append removed threads to the update_list
    }
    tlappend(&ready_list, &updated_list);                // append all broadcasted to ready_list
    enable_interrupts();
}

// ---EXPORTED READERS-WRITER LOCK FUNCTION DEFINITIONS--- //

// A readers-writer lock (rw-lock) is implemented using the rwlock structure:
//
// struct rwlock {
//     struct condition released;
//     struct thread * owner;
//     unsigned long cnt;
// };
//
// The three states are:
// - UNLOCKED when (owner == NULL && cnt == 0),
// - LOCKED-SHARED when (owner == NULL && cnt > 0), and
// - LOCKED-EXCLUISIVE when (owner != NULL && cnt > 0).
//
// The configuration (owner != NULL && cnt == 0) is not valid.
//

/*
INPUTS: struct rwlock * rwlk - pointer to the rwlock
        const char * name - pointer to the name you will set the lock to
OUTPUTS: no outputs
DESCRIPTION: intializes readers-writer lock; does not allocate memory
SIDE EFFECTS: once intialzied the new lock may be aquired
*/
void rwlock_init(struct rwlock * rwlk, const char * name) {
    rwlk->name = (name != NULL) ? name : "anon";            // if name passed is NULL
    rwlk->owner = NULL;                                     // will always start without an owner
    condition_init(&rwlk->released, "rwlock_released");     // intizialtize condition to ???                --x--
    rwlk->cnt = 0;                                          // will always start with no holders
}

const char * rwlock_name(const struct rwlock * rwlk) {
    return rwlk->name;
}

/*
INPUTS: struct rwlock * rwlk - pointer to the rwlock
        int exclusive - value indicating if the lock is attempting to be aquired exclusviely or not
OUTPUTS: no outputs
DESCRIPTION: attempts to aquire the lock for the current thread either as a writer (exclusive,) or reader (shared, 0)
SIDE EFFECTS: may yeild current thread in attempt to aquire lock
*/
void rwlock_acquire(struct rwlock * rwlk, int exclusive) {
    trace("%s(<%s>,%d)", __func__, rwlk->name, exclusive);
    // find which position in thread->lock_hold we can hold or are already holding lock data
    disable_interrupts();
    int pos = -1;
    for (int i = 0; i < 16; i++) {          // check if lock is already in thread array
        if (TP->lock_hold[i].lock == rwlk){
            pos = i;
            break;
        }
    }
    if (pos < 0){
        for (int i = 0; i < 16; i++) {     // find next empty position
            if (TP->lock_hold[i].lock == NULL){
                pos = i;
                break;
            }
        }
        TP->lock_hold[pos].lock = rwlk;             /// intitilaze lock_hold
        TP->lock_hold[pos].cnt = 0;
        TP->lock_hold[pos].readwrite = 0;
    }

    //pos = 0;        // for lock is always in first slot multi lock does not work
    if (exclusive == 0){                            // attempt as reader
        if (rwlk->owner == TP){                     // we already own exclusively
            rwlk->cnt++;
            TP->lock_hold[pos].cnt++;
            TP->lock_hold[pos].readwrite = 1;       // 1 here becuase we override into write as we already own
            enable_interrupts();
            return;
        }

        if (TP->lock_hold[pos].cnt > 0) {           // already hold shared
            rwlk->cnt++;
            TP->lock_hold[pos].cnt++;
            enable_interrupts();
            return;
        }

        while (rwlk->owner != NULL) {               // wait for lock to be available
            condition_wait(&rwlk->released);
            disable_interrupts();
        }
        rwlk->cnt++;                                // hold shared lock for first time
        TP->lock_hold[pos].cnt = 1;
        TP->lock_hold[pos].readwrite = 0;
        enable_interrupts();
        return;
    
    } else if (exclusive > 0){                      // attempt as writer
        if (rwlk->owner == TP) {                    // recursive lock case
            rwlk->cnt++;
            TP->lock_hold[pos].cnt++;
            TP->lock_hold[pos].readwrite = 1;
            enable_interrupts();
            return;
        }

        while (rwlk->cnt != 0) {                     // wait for shared lock to be gone
            condition_wait(&rwlk->released);
            disable_interrupts();
        }
        rwlk->owner = TP;                           // unlocked case
        rwlk->cnt++;
        TP->lock_hold[pos].cnt = 1;
        TP->lock_hold[pos].readwrite = 1;
    }
    enable_interrupts(); 
}

/*
INPUTS: struct rwlock * rwlk - pointer to the rwlock
OUTPUTS: no outputs
DESCRIPTION: releases the lock from a threads; thread can only call release for the number of times it has called aquire
SIDE EFFECTS: if lock is held recursively the thread may still hold the lock
*/
void rwlock_release(struct rwlock * rwlk) {
    trace("%s(<%s>)", __func__, rwlk->name);
    disable_interrupts();           //--x-- check for this and aquire
    int pos = -1;

    for (int i = 0; i < 16; i++){               // check if we are holding multiple locks per thread
        if (TP->lock_hold[i].lock == rwlk){
            pos = i;
            break;
        }
    }

    if (rwlk->owner == TP) {        // case where lock is exclusive
        if (TP != rwlk->owner) {                    // ensure current thread owns this lock
            enable_interrupts(); 
            return;
        }

        if (pos != -1) TP->lock_hold[pos].cnt--;                   // decrementing instead of instant broadcast allows for recursion
        rwlk->cnt--;

        if (rwlk->cnt == 0) {                        // only fully release when cnt is 0!
            rwlk->owner = NULL;
            condition_broadcast(&rwlk->released);
        }

    } else if (rwlk->owner == NULL){      // case where lock is shared
        if (rwlk->owner != NULL) {                      // if lock is owned then it is not shared
            enable_interrupts(); 
            return;
        }
        if (pos != -1) TP->lock_hold[pos].cnt--;
        rwlk->cnt--;

        if (rwlk->cnt == 0) {                        // only fully release when cnt is 0!                
            condition_broadcast(&rwlk->released);
        }
    }
    if (pos != -1 && TP->lock_hold[pos].cnt == 0) {                   // if our thread has finished all of its claims to lock than we take its lock privledges
        TP->lock_hold[pos].lock = NULL;
        TP->lock_hold[pos].readwrite = 0;
    }

    enable_interrupts();
}

// ---INTERNAL FUNCTION DEFINITIONS--- //
void init_main_thread(void) {
    main_thread.stack_anchor->ktp = &main_thread;
    //main_thread.stack_anchor->kgp = NULL;       // @TODO maybe? spec says we dont use gp really
}

void init_idle_thread(void) {
    idle_thread.stack_anchor->ktp = &idle_thread;
}

const char * thread_state_name(enum thread_state state) {
    static const char * const names[] = {
        [THREAD_UNDEFINED] = "UNDEFINED",
        [THREAD_WAITING] = "WAITING",
        [THREAD_RUNNING] = "RUNNING",
        [THREAD_READY] = "READY",
        [THREAD_EXITED] = "EXITED"
    };

    if (0 <= (int)state && (int)state < sizeof(names)/sizeof(names[0]))
        return names[state];
    else
        return names[THREAD_UNDEFINED];
};

void tlclear(struct thread_list * list) {
    list->head = NULL;
    list->tail = NULL;
}

int tlempty(const struct thread_list * list) {
    return (list->head == NULL);
}

struct thread * tlpeek(const struct thread_list * list) {
    return list->head;
}

void tlinsert(struct thread_list * list, struct thread * thr) {
    if (thr == NULL)
        return;
    thr->list_next = NULL;

    if (list->tail != NULL) {
        assert (list->head != NULL);
        list->tail->list_next = thr;
    } else {
        assert(list->head == NULL);
        list->head = thr;
    }

    list->tail = thr;
}

struct thread * tlremove(struct thread_list * list) {
    struct thread * thr;

    thr = list->head;
    
    if (thr == NULL)
        return NULL;

    list->head = thr->list_next;
    
    if (list->head != NULL)
        thr->list_next = NULL;
    else
        list->tail = NULL;

    thr->list_next = NULL;
    return thr;
}

void tlappend(struct thread_list * l0, struct thread_list * l1) {
    if (l0->head != NULL) {
        assert(l0->tail != NULL);
        
        if (l1->head != NULL) {
            assert(l1->tail != NULL);
            l0->tail->list_next = l1->head;
            l0->tail = l1->tail;
        }
    } else {
        assert(l0->tail == NULL);
        l0->head = l1->head;
        l0->tail = l1->tail;
    }

    l1->head = NULL;
    l1->tail = NULL;
}

void tlprepend(struct thread_list * l0, struct thread_list * l1) {
    if (l1->head == NULL) {
        assert (l1->tail == NULL);
        return;
    }

    assert(l1->tail != NULL);
        
    if (l0->head != NULL) {
        assert(l0->tail != NULL);
        l1->tail->list_next = l0->head;
    } else {
        assert(l0->tail == NULL);
        l0->tail = l1->tail;
    }

    l0->head = l1->head;
    l1->head = NULL;
    l1->tail = NULL;
}

void idle_thread_func(void) {
    // The idle thread sleeps using wfi if the ready list is empty. Note that we
    // need to disable interrupts before checking if the thread list is empty to
    // avoid a race condition where an ISR marks a thread ready to run between
    // the call to tlempty() and the wfi instruction.

    for (;;) {
        // If there are runnable threads, yield to them.

        while (!tlempty(&ready_list))
            yield_running_thread();
        
        // No runnable threads. Sleep using the wfi instruction. Note that we
        // need to disable interrupts and check the runnable thread list one
        // more time (make sure it is empty) to avoid a race condition where an
        // ISR marks a thread ready before we call the wfi instruction.

        disable_interrupts();
        if (tlempty(&ready_list))
            asm ("wfi");
        enable_interrupts();
    }
}