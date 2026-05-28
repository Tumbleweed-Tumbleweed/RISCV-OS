#include "../syscall.h"
#include "../string.h"
#include "../shell.h"
#include "../error.h"
#include "../io.h"

// From shell
#define BUFSIZE 256
#define MAXARGS 64

void main(int argc, char** argv) {
    char buffer[BUFSIZE];               //
    int open_output;                    // fd index given by open
    long read_output;                   //
    int reading = 1;                    //

    // Reads files and outputs it to STDOUT. If no files are provided, reads from STDIN instead.

    if (argc > 1) {             // Must read from the files as specified by the arguments in argv if argc > 1.
        for (int i = 1; i < argc; i++) {
            reading = 1;
            char file_path[256];
            char *path = argv[i];

            if (argv[i][0] != '/') {
                file_path[0] = '/';
                int j = 0;

                while (argv[i][j] != '\0' && j < 254) {
                    file_path[j + 1] = argv[i][j];
                    j++;
                }
                file_path[j + 1] = '\0';
                path = file_path;
            }

            open_output = _open(-1, path); 
            while (reading) {
                read_output = _read(open_output, buffer, BUFSIZE);              // tries to read from fd from open
                if (read_output < 0) {                  // check if the read was valid
                    dprintf(CONSOLEOUT, "READ FAILED");
                    reading = 0;
                } else if (read_output == 0) {
                    reading = 0;
                } else {                                // outputs it to STDOUT
                    _write(STDOUT, buffer, read_output);
                }
            }
            _close(open_output);                    // maybe check if close suceeds if we have more time
        }
        _exit();
    } else if (argc == 1) {               // Must read from STDIN if no other arguments are passed in (argc = 1).
        while (reading) {               // read from STDIN
            read_output = _read(STDIN, buffer, BUFSIZE);
            if (read_output < 0) {                      // check if the read was valid
                dprintf(CONSOLEOUT, "READ FAILED");
                reading = 0;
            } else if (read_output == 0) {
                reading = 0;
            } else {          // outputs it to STDOUT
                _write(STDOUT, buffer, read_output);
            }
        }
        _exit();
    } else {                    // argc < 1
        dprintf(CONSOLEOUT, "ERROR: argc < 1");
        _exit();
    } 
    _exit();                                // should exit beforehand but just in case
}