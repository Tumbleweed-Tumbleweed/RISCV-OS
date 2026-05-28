// device.h - Device manager
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//
//
// The device manager provides a small registry that maps human-readable device
// names (e.g., "uart0", "vioblk0") to open functions that construct I/O objects
// (struct io).
//
// At a high level:
//
//   - Drivers call register_device() during initialization to publish a device.
//   - Clients call open_devcie() to obtain a struct io* that speaks the io.h
//     interface.
//
// The registry is intentionally simple:
//   - A device record stores a name string, an open callback, and an auxiliary
//     pointer passed back to the driver on open.
//   - There is no close operation in the manager; lifetime is dictated by the
//     returned I/O object reference count (ioaddref()/iodropref()).
//   - Names are unique. Devices are identified purely by their name.
//
// Conventions:
//   - /name/ is the base device name ("uart", "viotblk", etc).
//   - /instno/ formats the final name as "<name><instno>" when instno >= 0.
//     If instno < 0, the name is used verbatim.
//

#ifndef _DEVICE_H_
#define _DEVICE_H_

struct io; // opaque; see io.h/ioimpl.h for details.

extern char devmgr_initialized;

// Set to 1 after devmgr_init() is called. Initialization ordering is
// the caller's responsibility.

extern void devmgr_init(void);

// Initializes the device manager. After initialization, drivers may call
// register_device() to add entries to the registry.
//
// On entry devmgr_init() assumes:
// - It is called during system bring-up before device registration.
//
// On return devmgr_init() guarantees:
// - /devmgr_initialized/ is 1.

typedef int (*device_openfn_t)(struct io ** ioptr, void * aux);

// Device open callback type.
//
// The open function is responsible for constructing (or referencing) a concrete
// I/O object, initializing it with ioinit()/seekio_init(), and returning it via
// /ioptr/.
//
// All openfn() implementations registered with the device manager must follow
// the following specifications.
//
// On entry openfn() assumes:
// - /ioptr/ is non-NULL.
// - /aux/ is the pointer passed at registration time.
//
// On return openfn() guarantees (on success):
// - The pointer at /ioptr/ points to a valid I/O object.
// - The pointer returned via /ioptr/ is considered an independent reference for
//   the purposes of reference-counting.
// - The returned I/O object is ready to perform I/O operation supported by the
//   device.
// - on failure, openfn() returns a negative error code.

extern int register_device (
    const char * name,
    int instno,
    device_openfn_t openfn,
    void * ofaux
);

// Registers a device with the device manager.
//
// The registry name is computed as follows:
// - If /instno/ >= 0: the final device name is "<name><instno>".
// - If /instno/ < 0: the final device name is exactly /name/.
//
// The manager stores the computed name, /openfn/, and /ofaux/ in an internal
// record. The record persists for the life of the kernel (no unregister).
//
// On entry register_device() assumes:
// - devmgr_init() has been called and /devmgr_initialized/ is 1.
// - /name/ is non-NULL and points to a NUL-terminated string.
// - /openfn/ is non-NULL.
// - The caller provides any needed synchronization if registration may race.
//
// On return register_device() guarantees (on success):
// - The device name is unique in the registry and openable via open_device().
// - The device's open function and auxiliary pointer are stored.

extern int open_device(const char * name, struct io ** ioptr);

// Opens a device by name and returns an I/O object representing that device.
//
// This function looks up /name/ in the registry and calls the stored open
// function. The returned I/O object is the sole mechanism for interacting with
// the device; the device manager does not mediate I/O after open.
//
// On entry open_device() assumes:
// - devmgr_init() has been called and /devmgr_initialized/ is 1.
// - /name/ is non-NULL and points to a NUL-terminated string.
// - /ioptr/ is non-NULL.
//
// On return open_device() guarantees (on success):
// - *ioptr points to a valid I/O object with a non-zero reference count.

extern int mount_devfs(const char * mpname);

// Mounts the /devfs/ filesystem. Acts as an abstraction to the device manager,
// allowing users to access devices via filesys commands. See /open_file/ in
// filesys.h.
//
// On entry mount_devfs() assumes:
// - devmgr_init() has been called and /devmgr_initialized/ is 1.
// - /mpname/ is non-NULL and points to a NUL-terminated string.
//
// On return devmgr_init() guarantees (on success):
// - devfs is added to the mountpoint list under the name /mpname/.
// - on failure, mount_ngfs() returns a negative error code.

extern int device_exists(const char * name);

// TODO

extern int device_ioaddref_openfn(struct io ** ioptr, void * aux);

// TODO

#endif
