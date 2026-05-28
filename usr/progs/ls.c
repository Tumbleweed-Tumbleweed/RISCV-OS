#include "../syscall.h"
#include "../string.h"
#include "../shell.h"
#include "../error.h"
#include "../io.h"

#define BUFSIZE 256

// helper that opens path as a listing object and prints each filename
static void list(const char * path) {
    int fd = _open(-1, path);
    if (fd < 0) {
        dprintf(STDOUT, "ls: %s: %d\n", path, fd);
        return;
    }

    // each _read on a listing returns one filename worth of bytes
    // null terminate ourselves since the listing doesnt include the null
    char buf[BUFSIZE];
    long r;
    while ((r = _read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[r] = '\0';
        dprintf(STDOUT, "%s\n", buf);
    }

    _close(fd);
}

void main(int argc, char** argv) {
    if (argc <= 1) {
        // no arg --x-- spec says default to root mountpoint which lists all mounts
        list("/");                              // FIX was "/c", spec wants "/"
    } else {
        for (int i = 1; i < argc; i++) {
            // if user passed multiple mountpoints, label each one like real ls does
            if (argc > 2)
                dprintf(STDOUT, "%s:\n", argv[i]);
            list(argv[i]);
        }
    }
    _exit();
}