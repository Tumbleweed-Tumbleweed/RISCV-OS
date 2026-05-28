# thrasm.s - Special functions called from thread.c
#
# Copyright (c) 2024-2026 University of Illinois
# SPDX-License-identifier: NCSA
#

        .text
        .global switch_running_thread
        .type   switch_running_thread, @function

switch_running_thread:

# void switch_running_thread(struct thread * next_thread);
# 
# Switches from the currently running thread to another thread and returns when
# the current thread is scheduled to run again. This function is called by
# running_thread_yield() defined in thread.c. Argument /next_thread/ is the
# thread to be resumed. Instead of returning, jumps to finish_switch(), which is
# also defined in thread.c, which returns via /ra/ to running_thread_yield().
# 
# tp = pointer to struct thread of current thread (to be suspended)
# a0 = pointer to struct thread of thread to be resumed
# 
# The first element of struct thread is:
#
#     struct thread_context {
#         uintptr_t s[12];
#         void * ra;
#         void * sp;
#     };
#

        sd      s0, 0*8(tp)
        sd      s1, 1*8(tp)
        sd      s2, 2*8(tp)
        sd      s3, 3*8(tp)
        sd      s4, 4*8(tp)
        sd      s5, 5*8(tp)
        sd      s6, 6*8(tp)
        sd      s7, 7*8(tp)
        sd      s8, 8*8(tp)
        sd      s9, 9*8(tp)
        sd      s10, 10*8(tp)
        sd      s11, 11*8(tp)
        sd      ra, 12*8(tp)
        sd      sp, 13*8(tp)

        mv      t0, tp
        mv      tp, a0
        mv      a0, t0

        ld      sp, 13*8(tp)
        ld      ra, 12*8(tp)
        ld      s11, 11*8(tp)
        ld      s10, 10*8(tp)
        ld      s9, 9*8(tp)
        ld      s8, 8*8(tp)
        ld      s7, 7*8(tp)
        ld      s6, 6*8(tp)
        ld      s5, 5*8(tp)
        ld      s4, 4*8(tp)
        ld      s3, 3*8(tp)
        ld      s2, 2*8(tp)
        ld      s1, 1*8(tp)
        ld      s0, 0*8(tp)
                
        j       finish_thread_switch

        .global start_thread
        .type   start_thread, @function

start_thread:
        mv a0, s1
        mv a1, s2
        mv a2, s3
        mv a3, s4
        mv a4, s5
        mv a5, s6
        mv a6, s7
        mv a7, s8
        jalr s0
        call exit_running_thread

# Statically allocated stack for the idle thread.

        .section        .data.stack, "wa", @progbits
        .balign          16
        
        .equ            IDLE_STACK_SIZE, 4096

        .global         _idle_stack_lowest
        .type           _idle_stack_lowest, @object
        .size           _idle_stack_lowest, IDLE_STACK_SIZE

        .global         _idle_stack_anchor
        .type           _idle_stack_anchor, @object
        .size           _idle_stack_anchor, 2*8

_idle_stack_lowest:
        .fill   IDLE_STACK_SIZE, 1, 0xBB

_idle_stack_anchor:
        .dword  0 # ktp
        .dword  0 # kgp
        .end

