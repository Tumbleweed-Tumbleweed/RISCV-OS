// scnum.h - System call numbers
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _SCNUM_H_
#define _SCNUM_H_

#define SYSCALL_EXIT 0    // terminate current process
#define SYSCALL_EXEC 1    // load a new executable image
#define SYSCALL_FORK 2    // create a child process
#define SYSCALL_WAIT 3    // wait for a child to exit
#define SYSCALL_PRINT 4   // print a message to the console
#define SYSCALL_USLEEP 5  // sleep for some number of microseconds

#define SYSCALL_CREATE 10  // create a file
#define SYSCALL_DELETE 11  // delete a file

#define SYSCALL_OPEN 15    // open fd
#define SYSCALL_CLOSE 16   // close fd
#define SYSCALL_READ 17    // read from fd
#define SYSCALL_WRITE 18   // write to fd
#define SYSCALL_IOCTL 19   // issue ioctl on fd
#define SYSCALL_PIPE 20    // create a pipe
#define SYSCALL_IODUP 21   // duplicate an fd

#define SYSCALL_AB1043 22  // age verification API

#endif  // _SCNUM_H_
