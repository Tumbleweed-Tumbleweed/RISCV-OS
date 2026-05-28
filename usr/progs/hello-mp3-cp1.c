#include "io.h"
void main(struct io * termio) {
    // outputs to termio
    ioprintf(termio, "Hello, world!\n");
}
