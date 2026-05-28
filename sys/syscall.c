// syscall.c - System call handling
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#include <stdint.h>
#ifdef SYSCALL_TRACE
#define TRACE
#endif

#ifdef SYSCALL_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "console.h"
#include "device.h"
#include "error.h"
#include "filesys.h"
#include "heap.h"
#include "intr.h"
#include "memory.h"
#include "misc.h"
#include "process.h"
#include "scnum.h"
#include "string.h"
#include "thread.h"
#include "timer.h"
#include "io.h"

#ifndef PATH_MAX
// Maximum path name length. Must be less than HEAP_ALLOC_MAX.
#define PATH_MAX 255
#endif

// for ab1043 age calculations
#define NSEC_PER_SEC (1000000000UL)
#define NSEC_PER_MIN (60UL * NSEC_PER_SEC)
#define NSEC_PER_HR (60UL * NSEC_PER_MIN)
#define NSEC_PER_DAY (24UL * NSEC_PER_HR)
#define TICK_TO_DAYS(t) ((t) / NSEC_PER_DAY)

#define DAYS_TO_YR_APPX(d) ((d) * 400 / 146097)
#define LEAP_YEAR(y) (((y)%4 == 0 && (y)%100 != 0) || ((y)%400 == 0))
// ---INTERNAL GLOBAL VARIABLES--- //
static unsigned long long birth_date = 0;
static int birth_date_set = 0;

// ---EXPORTED FUNCTION DECLARATIONS--- //
extern void handle_syscall(struct trap_frame *tfr);  // called from excp.c

// ---INTERNAL FUNCTION DECLARATIONS--- //

static long syscall(const struct trap_frame *tfr);
static int sysexit(void);
static int sysexec(int fd, int argc, char ** argv);
static int sysfork(const struct trap_frame * tfr);
static int syswait(int tid);
static int sysprint(const char * msg);
static int sysusleep(unsigned long us);
static int sysdelete(const char * path);
static int syscreate(const char * path);
static int sysopen(int fd, const char * path);
static int sysclose(int fd);
static long sysread(int fd, void * buf, size_t bufsz);
static long syswrite(int fd, const void * buf, size_t len);
static int sysioctl(int fd, int cmd, uintptr_t arg_uma);
static int syspipe(int * wfd, int * rfd);
static int sysiodup(int oldfd, int newfd);
static int sysab1043(unsigned long long dob);
static int allocfd(struct process * proc, int reqfd, int notfd);

// ---EXPORTED FUNCTION DEFINITIONS--- //

/*
INPUTS: struct trap_frame * tfr - pointer to the trap frame
OUTPUTS: no output
DESCRIPTION: moves pc past ecall; dispatches syscall handler, stores return value for user program.
SIDE EFFECTS: tfr->sepc is advanced
*/
void handle_syscall(struct trap_frame * tfr) {
    tfr->sepc += 4;                     // increment pc past ecall point
    tfr->a0 = syscall(tfr);             // syscall will pass any errors back up
}
// ---INTERNAL FUNCTION DEFINITIONS--- //

/*
INPUTS: const struct trap_frame * tfr - constant pointer to the trap frame
OUTPUTS: outputs error code on failure no output on success
DESCRIPTION: reads syscall number from trap frame and passes to proper syscall
SIDE EFFECTS: no side effectss
*/
long syscall(const struct trap_frame * tfr) {
    int call_num = tfr->a7;           // register a7 holds the syscall number
    switch(call_num) {
        case SYSCALL_EXIT:
            sysexit();
            return 0;
        case SYSCALL_EXEC: return sysexec((int)tfr->a0, (int)tfr->a1, (char **)tfr->a2);
        case SYSCALL_FORK: return sysfork(tfr);
        case SYSCALL_WAIT: return syswait((int)tfr->a0);
        case SYSCALL_PRINT: return sysprint((const char *)tfr->a0);
        case SYSCALL_USLEEP: return sysusleep((unsigned long)tfr->a0);
        case SYSCALL_CREATE: return syscreate((const char *)tfr->a0);
        case SYSCALL_DELETE: return sysdelete((const char *)tfr->a0);
        case SYSCALL_OPEN: return sysopen((int)tfr->a0, (const char *)tfr->a1);
        case SYSCALL_CLOSE: return sysclose((int)tfr->a0);
        case SYSCALL_READ: return sysread((int)tfr->a0, (void *)tfr->a1, (size_t)tfr->a2);
        case SYSCALL_WRITE: return syswrite((int)tfr->a0, (void *)tfr->a1, (size_t)tfr->a2);
        case SYSCALL_IOCTL: return sysioctl((int)tfr->a0, (int)tfr->a1, (uintptr_t)tfr->a2);
        case SYSCALL_PIPE: return syspipe((int *)tfr->a0, (int *)tfr->a1);
        case SYSCALL_IODUP: return sysiodup((int)tfr->a0, (int)tfr->a1);
        case SYSCALL_AB1043: return sysab1043((unsigned long long)tfr->a0);

        default: return -ENOTSUP;
    }
    return -ENOTSUP;       // should never reach this
}

/*
INPUTS: no inputs
OUTPUTS: should not return anything unless error occurs
DESCRIPTION: terminates the currently running process
SIDE EFFECTS: no side effects
*/
int sysexit(void) {
    process_exit();
    return -EACCESS;           // should never be reached
}

/*
INPUTS: int fd - index in processes file descriptor tables
        int argc - number of strings in argv
        char ** argv - array of pointers to NULL terminated strings
OUTPUTS: returns the result of process_exec() on success; negative error code on failure
DESCRIPTION: Validates and prepares a process to be called by process_exec()
SIDE EFFECTS: calls process_exec on success; which will change memory
*/
int sysexec(int fd, int argc, char ** argv) {
    struct process * curr_proc = running_thread_process();

    if (fd < 0 || fd >= PROC_IOMAX || curr_proc->iotab[fd] == NULL) return -EBADF;       // ensure fd is valid

    if (enforce_vptr(argv, (size_t)((argc + 1) * sizeof(char *)), PTE_R | PTE_U) < 0) return -EFAULT;      // make sure we only access valid datas

    for (int i = 0; i < argc; i++) {            // validate each string is not malicious
        if (validate_vstr(argv[i], PTE_R | PTE_U) < 0) return -EFAULT;
    }

    struct io *drop = curr_proc->iotab[fd];
    curr_proc->iotab[fd] = NULL;
    return process_exec(drop, argc, argv);
}

/*
INPUTS: struct trap_frame * tfr - pointer to the trap frame
OUTPUTS: return the outputs of TID of child process on success; negative error code on failure
DESCRIPTION: a system call to fork from the user
SIDE EFFECTS: creates a new process; new thead; clones adress space of calling process
*/
int sysfork(const struct trap_frame * tfr) {
    if (tfr == NULL) return -EINVAL;
    return process_fork(tfr);
}

/*
INPUTS: int tid - current thread ID
OUTPUTS: returns result of join thread on success; outputs error code on failure
DESCRIPTION: assuming valid thread ID call join_thread on current thread
SIDE EFFECTS: yeilds thread on success
*/
int syswait(int tid) {
    trace("%s(%d)", __func__, tid);
    if (0 <= tid) {
        return join_thread(tid);
    }
    return -EINVAL;
}

/*
INPUTS: const char *msg - message to be printeds
OUTPUTS: returns 0 on success; negative error code on failures
DESCRIPTION: validates passed message and then prints it
SIDE EFFECTS: no side effects
*/
int sysprint(const char *msg) {
    if (validate_vstr(msg, PTE_R | PTE_U) < 0) return -EFAULT;
    kprintf("%s", msg);
    return 0;
}

/*
INPUTS: unsigned long us - time in micro seconds for current thread to sleep
OUTPUTS: returns 0 on success
DESCRIPTION: puts current thread to sleep for us microseconds
SIDE EFFECTS: no side effects
*/
int sysusleep(unsigned long us) {
    sleep_us((unsigned int)us);
    return 0;
}

/*
INPUTS: const char *path - path for new file to be created on
OUTPUTS: returns 0 on success; negative error code on failure
DESCRIPTION: validates path and creates a new file on specified path
SIDE EFFECTS: new file created
*/
int syscreate(const char *path) {
    if (path == NULL) return -EINVAL;
    
    int valid = validate_vstr(path, PTE_R | PTE_U);
    if (valid < 0) return valid;
    
    size_t len = strlen(path);
    if (len == 0 || len > PATH_MAX) return -EINVAL;     // empty or too long
    
    char *kpath = kmalloc(len + 1);                     // copy path bc parse_path is destructive
    if (kpath == NULL) return -ENOMEM;
    strlcpy(kpath, path, len + 1);
    
    char *mpname, *flname;
    parse_path(kpath, &mpname, &flname);
    
    int result = create_file(mpname, flname);
    kfree(kpath);
    return result;
}

/*
INPUTS: const char *path - path to file to be delted
OUTPUTS: returns 0 on success; negative error code on failure
DESCRIPTION: validates path and deletes a new file on specified path
SIDE EFFECTS: a file is removed
*/
int sysdelete(const char *path) {
    if (path == NULL) return -EINVAL;
    
    // same deal as syscreate - gotta validate the user ptr first --x--
    int valid = validate_vstr(path, PTE_R | PTE_U);
    if (valid < 0) return valid;
    
    size_t len = strlen(path);
    if (len == 0 || len > PATH_MAX) return -EINVAL;
    
    char *kpath = kmalloc(len + 1);
    if (kpath == NULL) return -ENOMEM;
    strlcpy(kpath, path, len + 1);              // copy bc parse_path is destructive
    
    char *mpname, *flname;
    parse_path(kpath, &mpname, &flname);
    
    int result = delete_file(mpname, flname);
    kfree(kpath);
    return result;
}

/*
INPUTS: int fd - to be opened fd index
        const char *path - path to file to be opened
OUTPUTS: returns assigned fd on success; negative error code on failures
DESCRIPTION: validates path; opens file and assigns it a fd index
SIDE EFFECTS: adds io refrence to file
*/
int sysopen(int fd, const char *path) {
    if (path == NULL) return -EINVAL;
    
    int valid = validate_vstr(path, PTE_R | PTE_U);
    if (valid < 0) return valid;
    
    size_t len = strlen(path);
    if (len == 0 || len > PATH_MAX) return -EINVAL;
    
    char *kpath = kmalloc(len + 1);
    if (kpath == NULL) return -ENOMEM;
    strlcpy(kpath, path, len + 1);
    
    char *mpname, *flname;
    parse_path(kpath, &mpname, &flname);
    
    // open first, THEN allocate the slot --x-- avoids race if open_file yields
    struct io *io;
    int result = open_file(mpname, flname, &io);
    kfree(kpath);
    if (result < 0) return result;
    
    struct process *proc = running_thread_process();
    int afd = allocfd(proc, fd, -1);
    if (afd < 0) {
        iodropref(io);                                  // clean up since we cant use it
        return afd;
    }
    proc->iotab[afd] = io;
    return afd;
}

/*
INPUTS: int fd - to be closed fd index
OUTPUTS: returns 0 on success; negative error code on failure
DESCRIPTION: closes the requested file by dropping its refrence and opening the fd slot
SIDE EFFECTS: io refrence to file removed
*/
int sysclose(int fd) {
    // YOUR CODE HERE
    if (fd < 0 || fd >= PROC_IOMAX) return -EBADF;
    
    struct process *proc = current_process();
    if (proc->iotab[fd] == NULL) return -EBADF;
    
    // clear slot first then drop ref bc iodropref can reclaim and yield --x--
    struct io *io = proc->iotab[fd];
    proc->iotab[fd] = NULL;
    iodropref(io);
    return 0;
}

/*
INPUTS: fd - file or device
        buf - beffer to put read
        bufsz - bytes to read
OUTPUTS: bytes on sucess; negative error code on failure
DESCRIPTION: reads up to bufsz bytes from the file or device associated with fd into the user-space buffer buf
SIDE EFFECTS: writes into buf, calls current_process and enforce_vptr
*/
long sysread(int fd, void * buf, size_t bufsz) {
    struct io * io;
    int vptr_check; // checks if enforce_vptr fails

    if (fd < 0 || fd >= PROCESS_IOMAX) return -EBADF;           // fd checks

    io = current_process()->iotab[fd];                          // get io
    if (io == NULL) return -EBADF;

    // check vpts
    vptr_check = enforce_vptr(buf, bufsz, PTE_R | PTE_W | PTE_U);
    if (vptr_check < 0) {
        return vptr_check;
    }

    // read
    return ioread(io, buf, bufsz);
}

/*
INPUTS: fd - file or device
buf - beffer to put read
len - bytes to read
OUTPUTS: bytes on sucess, -1 on failure
DESCRIPTION: writes up to len bytes to the file/device associated with fd
SIDE EFFECTS: writes to the file or device, calls current_process and enforce_vptr
*/
long syswrite(int fd, const void *buf, size_t len) {
    struct io * io;
    int vptr_check; // checks if enforce_vptr fails

    if (fd < 0 || fd >= PROCESS_IOMAX) return -EBADF;             // fd checks
    
    io = current_process()->iotab[fd];                              // get io
    if (io == NULL) return -EBADF;

    // check vpts
    vptr_check = enforce_vptr(buf, len, PTE_R | PTE_U);
    if (vptr_check < 0) {
        return vptr_check;
    }

    // write
    return iowrite(io, buf, len);
}

/*
INPUTS: int fd - file descriptor
        int op - operation code for intended operation
        uintptr_t arg_uma - argument pointer
OUTPUTS: returns ioctl result on success; negative error code on failures
DESCRIPTION: validates fd and performs various operations depending on op
SIDE EFFECTS: may write to user memory depedning on operation
*/
int sysioctl(int fd, int op, uintptr_t arg_uma) {
    // YOUR CODE HERE
    if (fd < 0 || fd >= PROC_IOMAX) return -EBADF;
    
    struct process *proc = current_process();
    if (proc->iotab[fd] == NULL) return -EBADF;
    
    // std ops go through ioctl() after we validate the user ptr
    // device-specific ops go to ioctl_u() and the device handles the ptr itself
    switch (op) {
    case IOC_GETPOS:
    case IOC_GETEND:
        // kernel will write the pos/end back through arg_uma so need W access
        // need R|W|U bc writable user pages always have R on riscv (W-only is a reserved encoding)
        // passing W|U alone would mean lazy-alloc creates an invalid W-only mapping
        if (enforce_vptr((void *)arg_uma, sizeof(unsigned long long), PTE_R | PTE_W | PTE_U) < 0)
            return -EFAULT;
        return ioctl(proc->iotab[fd], op, (void *)arg_uma);
    case IOC_SETPOS:
    case IOC_SETEND:
        // just reading the val from user so R is enogh
        if (enforce_vptr((void *)arg_uma, sizeof(unsigned long long), PTE_R | PTE_U) < 0)
            return -EFAULT;
        return ioctl(proc->iotab[fd], op, (void *)arg_uma);
    case IOC_GETBLKSZ:
    case IOC_RESET:
        return ioctl(proc->iotab[fd], op, NULL);   // no arg needed
    default:
        // @TODO --x-- does ioctl_u always validate arg_uma itself?? header says yea
        return ioctl_u(proc->iotab[fd], op, arg_uma);
    }
}

/*
INPUTS: int * wfdptr - pointer to the writer side fd
        int * rfdptr - pointer to the reader side fd
OUTPUTS: returns 0 on success; negative error code on failure
DESCRIPTION: creates a pipe between the two passed fd
SIDE EFFECTS: allocates memory; updates iotab
*/
int syspipe(int * wfdptr, int * rfdptr) {
    // null check first --x-- enforce_vptr might handle null but lets be safe
    if (wfdptr == NULL || rfdptr == NULL) return -EINVAL;
    
    // both ptrs need to be user writable since we write fds back to them
    if (enforce_vptr(wfdptr, sizeof(int), PTE_R | PTE_W | PTE_U) < 0) return -EFAULT;
    if (enforce_vptr(rfdptr, sizeof(int), PTE_R | PTE_W | PTE_U) < 0) return -EFAULT;
    
    struct process *proc = current_process();
    struct io *wio, *rio;
    
    // make the pipe first then deal with fd allocation
    create_iopipe(&wio, &rio);
    if (wio == NULL || rio == NULL) return -ENOMEM;
    
    // get the writer fd --x-- if *wfdptr is neg allocfd auto picks, else uses that exact slot
    // depends on allocfd having that return reqfd line we fixed earlier
    int wfd = allocfd(proc, *wfdptr, -1);
    if (wfd < 0) {
        iodropref(wio);
        iodropref(rio);
        return wfd;
    }
    
    // reader fd next, pass wfd as notfd so we dont double up on the same slot
    int rfd = allocfd(proc, *rfdptr, wfd);
    if (rfd < 0) {
        iodropref(wio);
        iodropref(rio);
        return rfd;
    }
    
    // hook everything up
    proc->iotab[wfd] = wio;
    proc->iotab[rfd] = rio;
    *wfdptr = wfd;
    *rfdptr = rfd;
    return 0;
}

/*
INPUTS: int oldfd - file descriptor to be duplicated
        int newfd - destination file descriptor
OUTPUTS: returns assigned file descriptor on succes; negative error code on failure
DESCRIPTION: duplicates oldfd into newfd position; drops ref at newfd if exists
SIDE EFFECTS: updates iotab and refrences
*/
int sysiodup(int oldfd, int newfd) {
    // oldfd must be a valid existing fd
    if (oldfd < 0 || oldfd >= PROC_IOMAX) return -EBADF;
    // newfd can be negative (auto alloc) but cant exceed max
    if (newfd >= PROC_IOMAX) return -EBADF;
    
    struct process *proc = current_process();
    if (proc->iotab[oldfd] == NULL) return -EBADF;
    
    // if user asked for same fd just return it (no dup needed)
    if (oldfd == newfd) return newfd;
    
    // let allocfd find us a slot, skipping oldfd so we dont alias onto ourself
    int afd = allocfd(proc, newfd, oldfd);
    if (afd < 0) return afd;
    
    proc->iotab[afd] = ioaddref(proc->iotab[oldfd]);
    return afd;
}

/*
INPUTS: unsigned long long dob - date of birth of user to be set (only can be set on first call)
OUTPUTS: returns int indicating the age bracket of the user based on dob
DESCRIPTION: returns the current age of the user: the dob is only set on the first call
SIDE EFFECTS: dob is set permanetly on first call
*/
int sysab1043(unsigned long long dob){
    if (birth_date_set == 0) {                          // set global dob variable if not already set
        birth_date = dob;
        birth_date_set = 1;
    }

    struct io * rtcio;                                  // open rtc device to calculate age based on dob
    int error = open_device("rtc", &rtcio);
    if (error != 0) return error;

    unsigned long long time = 0;
    long check = ioread(rtcio, &time, sizeof(time));
    iodropref(rtcio);
    if (check < 0) return check;                        // pass through error code if error occurs
    if (check != (long)(sizeof(time))) return -EIO;

    long age = DAYS_TO_YR_APPX(TICK_TO_DAYS(time - birth_date));                // calculate ages and return the age bracket int
    if (age < 13) return 0;
    if (age < 16) return 1;
    if (age < 18) return 2;
    return 3;
}

/*
INPUTS: struct process *proc - process with the iotab to be searched by fds
        int reqfd - requested fd slot
        int notfd - fd slot to be skipped
OUTPUTS: returns allocated fd index; negative error code on failure
DESCRIPTION: helper to allocate fds, used by iodup and open
SIDE EFFECTS: no side effects
*/
static int allocfd(struct process *proc, int reqfd, int notfd) {
    //P.S. going based off what I think it should do and what I need from it
    // reqfd < 0 means auto-assign any free slot (matches spec behavior for sysopen/sysiodup)
    if (reqfd >= 0) {
        if (reqfd >= PROC_IOMAX || reqfd == notfd) return -EBADF;
        if (proc->iotab[reqfd] != NULL) return -EMFILE;
        return reqfd;                                   // !!! was missing, caused us to fall thru
    }
    // no specific fd requested so find lowest free slot, skipping notfd if passed
    for (int i = 0; i < PROC_IOMAX; i++) {
        if (i == notfd) continue;
        if (proc->iotab[i] == NULL) return i;
    }
    return -EMFILE;
}