// main.c - main function of the kernel (called from start.s)
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "conf.h"
#include "console.h"
#include "elf.h"
#include "intr.h"
#include "plic.h"
#include "device.h"
#include "thread.h"
#include "timer.h"
#include "string.h"
#include "error.h"
#include "misc.h" // for halt()
#include "heap.h"
#include "io.h"

#include "fs/ngfs.h"
#include "fs/tarfs.h"
#include "filesys.h"
#include "process.h"

#ifndef STUDENT_CP1
#define INITEXE "shell"
#else
#define INITEXE "trek-mp3-cp1"
#define CONSOLEDEV "uart1"
#endif

#define CMNTNAME "c" // ngfs
#define DMNTNAME "d" // tarfs
#define DEVMNTNAME "dev"
#define CDEVNAME "vioblk1"
#define DDEVNAME "vioblk0"

static void exec_init();
static void mount_drive(char * mntname, char * devname,
    int (*mount)(const char * mpname, struct io * bkgio));

extern void board_init(unsigned int hartid, void * dtb); // from board/xxx.c
extern void attach_board_devices(void); // from board/xxx.c

void main(unsigned int hartid, void * dtb) {
    board_init(hartid, dtb);
    intrmgr_init();
    thrmgr_init();
    devmgr_init();

#ifndef STUDENT_CP1
	procmgr_init();
#endif

    attach_board_devices();
    enable_interrupts();

    mount_devfs(DEVMNTNAME);
    mount_drive(CMNTNAME, CDEVNAME, mount_ngfs);
    mount_drive(DMNTNAME, DDEVNAME, mount_tarfs);

    exec_init();
    flush_all_filesys();
}

void mount_drive(char * mntname, char * devname, 
    int (*mount)(const char * mpname, struct io * bkgio)) {
    
    struct io * hd;
    int result;

    result = open_device(devname, &hd);

    if (result < 0) {
        kprintf("Failed to open storage device %s: %s\n", 
            devname, error_desc(result));
        halt();
    }

    result = mount(mntname, hd);

    if (result != 0) {
        kprintf("mount(%s, bkgio(%s)) failed: %s\n",
            mntname, devname, error_desc(result));
        halt();
    }
}

void exec_init() {
    struct io * initexe;
    int result;
    
    result = open_file(CMNTNAME, INITEXE, &initexe);

    if (result != 0) {
        kprintf(INITEXE ": %s; terminating\n", error_name(result));
        halt();
    }

#ifndef STUDENT_CP1
    char * argv[] = { NULL };

    // Set up default descriptors
    current_process()->iotab[0] = create_nullio();
    current_process()->iotab[1] = create_nullio();
    open_device("uart1", &current_process()->iotab[2]);

    process_exec(initexe, 0, argv);
#else
    void (*entry)(void);
    int tid;
    struct io * uartio;

    result = open_device(CONSOLEDEV, &uartio);
    if (result != 0) {
        kprintf(CONSOLEDEV ": %s; terminating\n", error_name(result));
        halt();
    }

    // load the executable into memory
    result = elf_load(initexe, &entry);

    if (result != 0) {
        kprintf(INITEXE ": %s; terminating\n", error_name(result));
        halt();
    }

    // launch the executable
    tid = spawn_thread(INITEXE, entry, uartio);

    if (tid < 0) {
        kprintf("spawn thread: %s; terminating\n", error_name(result));
        halt();
    }

    join_thread(tid);
#endif
}

