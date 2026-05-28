#include "../syscall.h"
#include "../string.h"
#include "../shell.h"

void main (int argc, char** argv)
{
    int i;

    for (i=1; i<argc; i++) {
        dprintf(STDOUT, "%s ", argv[i]);
    }
    dprintf(STDOUT, "\n");
}