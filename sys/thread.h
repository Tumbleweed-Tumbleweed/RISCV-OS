// thread.h - Thread creation and synchronization
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _THREAD_H_
#define _THREAD_H_

struct thread; // opaque decl.
struct process; // forward opaque decl. (process.h)

// THREADS
//

extern char thrmgr_initialized;
extern void thrmgr_init(void);

// Initializes the thread manager. This function must be called before any other
// functions declared in thread.h. The global variable /thrmgr_initialized/,
// which is statically initialized to 0, is set to 1 by thrmgr_init(); it must
// not be modified externally.
//
// On return from thrmgr_init(), the currently executing thread (current thread)
// is the distinguished thread /main/ with TID 0. Additional threads may be
// created using spawn_thread().
//
// On return thrmgr_init() guarantees:
// - /thrmgr_initialized/ is set to 1.
//
// * This function must be called once at system initialization time with
//   interrupts disabled before any other functions declared in thread.h.
// * this function may allocate physical memory pages.
//
// See spawn_thread().


extern int running_thread(void);

// Returns the TID if the current (calling) thread.
//


extern int spawn_thread (
    const char * name,
    void (*entry)(void),
    ...);

// Creates a new thread and adds it to the ready list. The name of the spawned
// thread is given by /name/, which must be either a pointer to a
// null-terminated string or NULL. If NULL, the thread name is
// implementation-defined. Otherwise, if /name/ is not NULL, it must be a
// pointer to to a null-terminated string that remains unchanged during the
// lifetime of the thread.
//
// The spawned thread has no associated process. Use thread_attach_process() to
// associate a process with it.
//
// The new thread will start execution in /entry/, which must be a pointer to
// executable code, when it is scheduled to run. Up to 8 optional arguments may
// be passed to spawn_thread(). These arguments will be passed to /entry/ as
// function arguments in the same order when it is started. That is, if
// spawn_thread() is invoked as,
//
//     spawn_thread("myname", (void(*)()) &myfunc, "one", 2);
//
// and /myfunc/ is a function declared as,
//
//    void myfunc(const char * s, int n);
//
// then the new thread will start executing as if myfunc() had been called as:
//
//    myfunc("one", 2);
//
// If /entry/ is a function and it returns, this is equivalent to calling
// exit_running_thread() in the context of the new thread.
//
// Note: spawn_thread() does *not* suspend the calling thread. The thread
// created by spawn_thread() will run some time after the calling thread calls
// one of the functions that suspends the calling thread, such as thread_yield()
// or condition_wait().

// The calling thread is designated as the parent of the new thread. The
// spawn_thread() function returns the TID of the new thread or one of the
// following negative error codes:
//
//   [-EMTHR] A new thread could not be created because the maximum number of
//            threads in system has been reached. The maximum number of threads
//            is given by the compile-time paramter NTHR.
//
// The calling thread may wait for the new thread to exit using join_thread().
//
// On entry spawn_thread() assumes:
// - /name/ is a pointer to a null-terminated string or NULL.
// - /entry/ is a pointer to executable code.
//
// On successful return spawn_thread() guarantees:
// - A new thread is created in the system.
// - The return value of spawn_thread() is the TID of the new thread.
// - The name of the new thread is either /name/, if /name/ is not NULL, or an
//   implementation-defined null-terminated string.
// - The new thread will be scheduled to run at some point after the calling
//   thread is suspended.
// - The new thread will begin execution at the address given by /entry/.
// - The first eight optional arguments to spawn_thread() will be passed to
//   /entry/ as function arguments.
// - If /entry/ is a pointer to a function and that function returns, the effect
//   shall be the same as if the new thread called thread_exit().
// - The calling thread is the parent of the new thread.
//
// Performance guarantees:
// - spawn_thread() succeeds if the number of threads in the system is fewer
//   than NTHR (compile-time parameter) and there is sufficient memory to
//   allocate a thread stack and at least 256 bytes of additional memory.
//
// * This function must _not_ be called from an ISR.
//
// See also: exit_running_thread(), join_thread().


extern const char * thread_name(int tid);

// Returns the name of a thread. The /tid/ argument gives the TID of the thread
// whose name is returned. If spawn_thread() was called with a non-NULL /name/
// argument, then the same pointer is returned by thread_name(). Otherwise, if
// spawn_thread() was called with a NULL /name/ argument, thread_name() returns
// the implementation-defined name assigned to the thread. In all cases, the
// returned value is a pointer to a null-terminated string that is valid for the
// lifetime of the thread.
//
// On entry thread_name() assumes:
// - /tid/ is the TID of an existing thread.
//
// On return thread_name() guarantees:
// - If the thread associated with /tid/ was created using spawn_thread() with a
//   non-NULL /name/ argument, the value returned is /name/.
// - If the thread associated with /tid/ was created using spawn_thread() with a
//   NULL /name/ argument, the value returned is a pointer to an
//   imlementation-defined null-terminated string.
//
// * This function may be called from an ISR.
//
// See also spawn_thread(), running_thread_name().


extern const char * running_thread_name(void);

// Returns the name of the running thread. This function call is equivalent to:
//
//    thread_name(running_thread())
//
// See also: thread_name(), running_thread().



extern void yield_running_thread(void);

// Suspends the current thread if there are other threads ready to run. Returns
// without suspending the calling thread if no other threads are runnable.
//
// * This function may switch to another thread context.
// * This function must _not_ be called from an ISR.
//
// See also: submit_running_thread().


extern void __attribute__ ((noreturn)) exit_running_thread(void);

// Termimates the currently running thread. This function does not return. If
// the calling thread is the main thread, the system halts. If the calling
// thread is not the main thread, its parent will be able to join the calling
// thread using join_thread(). If the calling thread has any children, it will
// reclaim the thread struct and space in thrtab of children which have exited
// and orphan the children which have not exited.
//
// After exiting, a child is still considered to exist in the system until it is
// joined by its parent.
//
// This function does not release any locks held by the thread. A thread *must*
// release any locks it holds before exiting.
//
// Performance guarantees:
// - The storage used for the calling thread's stack is returned to the system
//   shortly after exiting without waiting for the thread to be joined. (Some
//   resources associated with the thread, such as its TID, may be released only
//   after the thread is joined.)
//
// * This function switches to another thread context.
// * This function must _not_ be called from an ISR.
//
// See also: join_thread().


extern int join_thread(int u_tid);

// Waits for a child of the calling thread to exit. If /u_tid/ is not zero,
// join_thread() waits until the child identified by /u_tid/ exits. If that
// child has already exited, join_thread() returns immediately. If /u_tid/ is
// zero, join_thread() waits for _any_ of the calling thread's children to exit.
// If there is a child thread that has already exited, but has not yet been
// joined, join_thread() returns immediately. If successful, join_thread()
// returns the TID of the joined child. After being joined, all resources
// associated with the child thread are considered to be reclaimed by the system
// and the joined thread no longer counts towards the number of threads in the
// system.
//
// The /u_/ prefix in /u_tid/ indicates that this function checks it for
// validity. The parameter may be passed unchecked from a system call.
//
// If a non-zero /u_tid/ is not the TID of a child of the calling thread,
// join_thread() returns -ECHILD. If /u_tid/ is zero but the calling thread has
// no children, join_thread() also returns -ECHILD.
//
// * This function may switch to another thread context.
// * This function must _not_ be called from an ISR.
//
// On sucessful (non-error) return join_thread() guarantees:
// - The returned TID is is a child of the calling thread and the child has
//   exited but has not yet been joined.
//
// See also: exit_running_thread().


extern struct process * thread_process(int tid);

// Returns a pointer to the process structure (`struct process *`) of the
// process associated with the thread specified by /tid/, or NULL if there is no
// associated process (kernel threads). A process may be associated with a
// thread using thread_attach_process().
//
// During a context switch, the active memory space is switched to the memory
// space of the resuming thread, if not NULL. If the resuming thread does not
// have an associated process, the active memory space is not switched.
//
// The returned pointer is valid for the lifetime of the thread.
//
// On entry thread_process() assumes:
// - /tid/ is the TID of an existing thread.
//
// On return thread_process() guarantees:
// - If the return value is NULL, the thread has no associated process.
// - If the return value is not NULL, the returned pointer points to a process
//   structure most recently associated with the specified process.
//
// * This function may be called from an ISR.
//
// See also: running_thread_process(), thread_attach_process(),
// thread_detach_process(), spawn_thread().


extern struct process * running_thread_process(void);

// Returns a pointer to the process structure of the process associated with the
// running thread. This function is equivalent to:
//
//    thread_process(running_thread())
//
// * This function may be called from an ISR.
//
// See also: thread_process(), running_thread().


extern void thread_attach_process(int tid, struct process * proc);

// Sets the process associated with a thread. The /tid/ argument specifies a
// thread and the /proc/ argument specifies the process to associate with that
// thread. The /proc/ argument must be a pointer to the process struct of an
// existing process. The process must be valid for the lifetime of the thread.
//
// On entry thread_attach_process() assumes:
// - /tid/ is the TID of an existing thread without an associated process.
// - /proc/ is a pointer to the process struct of an existing process.
//
// On return thread_attach_process() guarantees:
// - /proc/ is the process associated thread /tid/.
//
// * This function may be called from an ISR.
//
// See also: thread_process().


extern void * running_thread_stack_anchor(void);

// Returns a pointer to the thread stack anchor structure that is placed at the
// base of the running thread's stack. This function is used by functions that
// call trap_frame_jump() to set up the initial kernel stack pointer to be used
// on re-entry into the kernel from user mode. See process_exec() and
// process_fork() for usage.
//
// The thread_stack_anchor struct is defined in thread.c as
//
//     struct thread_stack_anchor {
//        struct thread * ktp;
//        void * kgp;
//     };
//
// This structure is accessed in _smode_trap_entry() in trap.s to recover the
// kernel thread pointer and kernel global pointer values on re-entry into the
// kernel from U mode. The thread stack anchor structure is initialized in
// spawn_thread for spawned threads and by thrmgr_init() for the main and idle
// threads. Because it should not be accessed anywhere else, it is defined in
// thread.c and (implicitly) in trap.s.
// 
// The returned pointer is valid for the lifetime of the running thread.
//
// * This function may be called from an ISR.
//
// See also: _smode_trap_entry in trap.s, spawn_thread(), process_exec(),
// process_fork().


// CONDITION VARIABLES
//

// The main synchronization mechanism between threads is the _condition
// variable_ represented by a /condition/ structure. All other synchronization
// mechanisms (e.g. readers-writer locks) are constructed using condition
// variables.

// The /thread_list/ structure is used internally by the thread manager and must
// not be accessed outside thread.c. (Inside thread.c, use only the provided
// tl-prefixed functions.) It is included here because it is required for the
// definition of the condition structure below.

struct thread_list {
    // NO DIRECT ACCESS!
    struct thread * head;
    struct thread * tail;
};

// A /condition/ structure represents a condition variable. The definition
// should be treated as opaque; it is provided here so that condition variables
// can be allocated statically. Do not access members of a /condition/ structure
// directly; all operations on condition variables should be via the functions
// declared below.

struct condition {
    // NO DIRECT ACCESS!
    const char * name;
	struct thread_list wait_list;
};

extern void condition_init(struct condition * cond, const char * name);

// Initializes a condition variable. The /cond/ argument must point to a region
// of memory large enough to hold an instance of a /condition/ structure. This
// function does _not_ allocate space for this structure.
//
// The name of the new condition variable is given by /name/, which must be
// either a pointer to a null-terminated string or NULL. If NULL, the thread
// name is implementation-defined. Otherwise, if /name/ is not NULL, it must be
// a pointer to to a null-terminated string that remains unchanged during the
// lifetime of the condition variable.

// The condition variable is initialized to a state with no threads are
// considered to be waiting on the condition variable.
//
// When a condition variable is no longer needed, the memory associated with the
// /condition/ structure may be reclaimed. After that point, a pointer to this
// structure is no longer considered to be a valid condition variable and must
// not be used. It is safe, however, to initialize and use a stack-allocated
// /condition/ structure, provided that the condition variable is only used
// during the lifetime of the structure.
//
// On entry condition_init() assumes:
// - /cond/ is a properly-aligned pointer to a region of memory large enough to
//   hold an instance of a /condition/ structure.
// - /name/ is a pointer to a null-terminated string or NULL.
//
// On return condition_init() guarantees:
// - /cond/ points to an initialized instance of a condition variable.
// - No threads are considered to be waiting on the condition variable.
//
// Performance guarantees:
// - The number of condition variables in the system is unlimited.
// - condition_init() does not allocate memory.
//
// * This function may be called from an ISR provided the same condition
//   variable is not accessed concurrently.
//
// See also condition_name(), condition_wait(), condition_broadcast().


extern const char * condition_name(const struct condition * cond);

// Returns the name of a condition variable. The /cond/ argument must be a
// properly initialized condition variable. If condition_init() was called with
// a non-NULL /name/ argument, then the same pointer is returned by
// condition_name(). Otherwise, if condition_init() was called with a NULL
// /name/ argument, condition_name() returns the implementation-defined name
// assigned to the condition variable. In all cases, the returned value is a
// pointer to a null-terminated string that is valid for the lifetime of the
// condition variable.
//
// On entry condition_name() assumes:
// - /cond/ is a pointer to properly initialized condition structure.
//
// On return condition_name() guarantees:
// - The return value is a pointer to a null-terminated string.
// - If the condition variable /cond/ was initialized with a non-NULL /name/
//   argument, the value returned is /name/.
// - If the condition variable /cond/ was initialized with a NULL /name/
//   argument, the value returned is a pointer to an imlementation-defined
//   null-terminated string.
//
// * This function may be called from an ISR.
//
// See also condition_init().

extern void condition_wait(struct condition * cond);

// Suspends the current thread until a condition is _signalled_ by a call to
// condition_broadcast() from another thread or from an interrupt service
// routine. The /cond/ argument must be a properly initialized condition
// variable. This function will cause a context switch and must not be called
// from an ISR. Note that interrupts are re-enabled during a context switch and
// restored to their original enabled/disabled state when the thread is resumed.
// In cases where a thread needs to wait for a condition to be signalled by an
// ISR, condition_wait() should be called with interrupts disabled to ensure
// avoid a race condition.
//
// Despite the name _condition variable_, there no no guarantee that any
// particular condition, in the broad sense of the word, holds when
// condition_wait() returns.
//
// On entry condition_wait() assumes:
// - /cond/ is a pointer to properly initialized condition structure (either
//   zero-initialized or initialized using condition_init()).
//
// On return condition_wait() guarantees:
// - The associated condition was signalled using condition_broadcast() at least
//   once between entry into condition_wait() and its return.
//
// Performance guarantees:
// - The number of threads waiting on a condition variable is unlimited.
// - The calling thread becomes RUNNABLE after the next call to
//   condition_broadcast(), necessarily made in another thread or in an ISR,
//   returns.
// - condition_wait() does not allocate memory.
//
// There are _no_ guarantees on the order in which waiting threads are resumed.
//
// * This function may switch to another thread context.
// * This function must _not_ be called from an ISR.
//
// See also: condition_init(), condition_broadcast().

extern void condition_broadcast(struct condition * cond);

// Wakes up all threads waiting on a condition. The /cond/ argument must point
// to a properly initialized `struct condition`. All threads currently waiting
// on this condition are woken up (i.e., they become runnable). Calling this
// function does not cause a context switch from the currently running thread
// and is safe to call from an ISR.
//
// On entry condition_wait() assumes:
// - /cond/ is a pointer to properly initialized condition structure.
//
// On return condition_wait() guarantees:
// - All threads that were waiting on the condition associated with /cond/ on
//   entry are now RUNNABLE.
// - No threads are considered to be waiting on the condition variable.
//
// Note that there are _no_ guarantees about the order in which woken threads
// will be executed.
//
// * This function may be called from an ISR.
//
// See also: condition_init(), condition_wait().

//
// READERS-WRITER LOCK
//
// A readers-writer lock, or rw-lock for short, also called a shared-exclusive
// lock, allows threads to synchronize shared or exclusive access to an object.
// The The lock may either be _unlocked_ (not held by any threads), held
// _shared_ by one or more threads, or held _exclusvely_ by a single thread.
// Recursive locking is supported: A thread may acquire the lock as a reader
// multiple times, in which case it must also release it the same number of
// times in order to be considered no longer holding the lock. A thread may
// acquire the lock as a writer multiple times, in which case it must also
// release the lock the same number of times in order to be considered no longer
// holding the lock. If a thread acquired the lock multiple times, it is said to
// be holding the lock _with multiplicity_.
//
// The following important restrictions must be followed:
//
// - A thread must _not_ attempt to release a lock it is not holding,
// - A thread must _not_ attempt to acquire a lock exclusively if it is already
//   holding it shared.
// - None of the rw-lock functions may be called from an ISR.
//
// The /rwlock/ structure defined below implements an rw-lock. The definition is
// given here to allow locks to be allocated statically. The structure must only
// be manipulated using the functions declared below.

struct rwlock {
    // NO DIRECT ACCESS!
    const char * name;
    struct condition released;
    struct thread * owner;
    unsigned long cnt;
};

// EXPORTED FUNCTION DECLARATIONS
//

extern void rwlock_init(struct rwlock * rwlk, const char * name);

// Initializes an rw-lock in the _unlocked_ state. Any thread may then acquire
// the lock either in shared or exclusive mode.
//
// The name of the new rw-lock is given by /name/, which must be either a
// pointer to a null-terminated string or NULL. If NULL, the thread name is
// implementation-defined. Otherwise, if /name/ is not NULL, it must be a
// pointer to to a null-terminated string that remains unchanged during the
// lifetime of the rw-lock.
//
// When an rw-lock is no longer needed, the memory associated with the /rwlock/
// structure may be reclaimed. After that point, a pointer to this structure is
// no longer considered to be a valid lock and must not be used. It is safe,
// however, to initialize and use a stack-allocated /rwlock/ structure, provided
// that the lock is only used during the lifetime of the structure.
//
// On entry rwlock_init() assumes:
// - /rwlk/ is a properly-aligned pointer to a region of memory large enough to
//   hold an instance of a /rwlock/ structure.
// - /name/ is a pointer to a null-terminated string or NULL.
//
// On return rwlock_init() guarantees:
// - /rwlk/ points to an initialized instance of an rw-lock.
// - No threads are considered to be holding the lock.
//
// Performance guarantees:
// - The number of rw-locks in the system is unlimited.
// - rwlock_init() does not allocate memory.
//
// * This function may be called from an ISR provided the same rw-lock is not
//   accessed concurrently.
//
// See also: rwlock_name(), rwlock_acquire(), rwlock_release().


extern const char * rwlock_name(const struct rwlock * rwlk);

// Returns the name of an rw-lock. The /rwlk/ argument must be a properly
// initialized rw-lock. If rwlock_init() was called with a non-NULL /name/
// argument, then the same pointer is returned by rwlock_name(). Otherwise, if
// condition_init() was called with a NULL /name/, rwlock_name() returns the
// implementation-defined name assigned to the rw-lock. In all cases, the
// returned value is a pointer to a null-terminated string that is valid for the
// lifetime of the rw-lock.
//
// On entry rwlock_name() assumes:
// - /rwlk/ is a pointer to properly initialized rw-lock.
//
// On return rwlock_name() guarantees:
// - The return value is a pointer to a null-terminated string.
// - If rwlock_init() was called to initialize the rw-lock with a non-NULL
//   /name/ argument, the value returned is /name/.
// - If rwlock_init() was called to initialize the rw-lock with a NULL /name/
//   argument, the value returned is a pointer to an imlementation-defined
//   null-terminated string.
//
// * This function may be called from an ISR.
//
// See also rwlock_init().

extern void rwlock_acquire(struct rwlock * rwlk, int exclusive);

// Acquires an rw-lock. If (exclusive != 0), rwlock_acquire() attempts to
// acquire the rw-lock exclusively. Otherwise, rwlock_acquire() attempts to
// acquire the rw-lock as shared. Multiple threads may share an rw-lock
// simultaneously. No thread may acquire an rw-lock exclusively while it is held
// shared.
//
// A thread may hold an rw-lock either shared or exclusively. If the rw-lock is
// currently held by another thread exclusively, the running thread is suspended
// until the rw-lock can be acquired in the requested mode. If the rw-lock is
// not held exclusively by another thread and (exclusive == 0), rwlock_acquire()
// succeeds immediately and the calling thread is considered to be holding the
// rw-lock shared.
//
// A thread may acquire an rw-lock multiple times (recursive locking) in the
// same mode (shared or exclusive). If a thread attempts to acquire an rw-lock
// exclusively that it is already holding exclusively, rwlock_acquire() succeeds
// immediately. Similarly, if a thread attempts to acquire a shared rw-lock that
// it is already sharing, it succeeds immediately. A thread that acquires a lock
// multiple times is said to be holding it _with multiplicity_.
//
// If a thread holding an rw-lock exclusively attempts to acquire it as shared,
// the request is promoted to an exclusive request and succeeds immediately. The
// rw-lock lock is then considered to be held by the thread exclisively with
// multiplity greater than one.
//
// A thread may _not_ request a lock exclusively if it already holding it in
// shared mode.
//
// On entry rwlock_acquire_shared() assumes:
// - /rwlock/ is a pointer to properly initialized rw-lock.
// - If a thread holds the lock in shared mode, then (exclusive == 0).
//
// On return rwlock_acquire_shared() guarantees:
// - The current thread is considered to be holding the rw-lock in the requested
//   mode.
//
// Performance guarantees:
// - The number of threads waiting to acquire an rw-lock is unlimited.
// - If the calling thread is able to acquire an rw-lock in the request mode,
//   rwlock_acquire() returns immediately without a context switch.
// - If a thread is waiting to acquire an rw-lock, the rw-lock is released, and
//   no other threads are waiting to acquire the same rw-lock, the waiting
//   thread will succeed in acquiring the lock.
// - rwlock_acquire() does not allocate memory.
//
// * This function may switch to another thread context.
// * This function must _not_ be called from an ISR.
//
// See also: rwlock_init(), rwlock_release().

extern void rwlock_release(struct rwlock * rwlk);

// Releases a previously acquired rw-lock. The rw-lock may be either shared or
// exclusive, however, running thread _must_ be holding the rw-lock. That is,
// the number of times the running thread has called rwlock_acquire() minus the
// number of times it has called rwlock_release() on the rw-lock must be
// non-zero when calling rwlock_release(). If the running thread is holding the
// rw-lock with multiplicity, the number of times it is said to be holding the
// rw-lock is decreased by one.
//
// On entry rwlock_release() assumes:
// - The rw-lock is currently held by the running thread.
//
// On return rwlock_release() guarantees:
// - If the running thread was holding the rw-lock with multiplicity greater
//   than one, the multiplity is decreased by one.
// - If the running thread was holding the rw-lock with multiplicity one, then
//   on return it is no longer considered to be holding the rw-lock.
//
// Performance guarantees:
// - If on return from rwlock_release() a thread is no longer considered to be
//   holding the rw-lock and there are threads waiting to acquire that lock,
//   then either (a) exactly one of the threads waiting to acquire the rw-lock
//   exclusively will succeeds, or (b) all threads waiting to acquire a rw-lock
//   shared will succeed.
//
// * This function must _not_ be called from an ISR.
//
// See also: rwlock_acquire().

#endif // _THREAD_H_
