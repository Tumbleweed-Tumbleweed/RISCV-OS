#include "../syscall.h"
#include "../string.h"
#include "../shell.h"
#include "../error.h"
#include "../io.h"
// From shell
#define BUFSIZE 256
#define MAXARGS 64
void main(int argc, char** argv) {
    // Reads items from STDIN, separated by spaces or newlines, and executes the command specified in its
    // arguments with those items as additional arguments.
    int fd; // index in processes file descriptor tables
    char file_path[BUFSIZE];
    char buffer[BUFSIZE];
    long read_output; 
    char * new_argv[MAXARGS + 1]; // add one to put the NULL at the end
    int exec_output; 
    int new_argc;

    // argument count check
    if (argc <= 1) {  
        dprintf(CONSOLEOUT, "ERROR: not enough arguments");
        _exit();
    }

    //  Must read from STDIN.
    read_output = _read(STDIN, buffer, BUFSIZE - 1);
    if (read_output < 0) {
        dprintf(CONSOLEOUT, "READ FAILED");
    }

    buffer[read_output] = '\0';

    // Must create argv and argc, which is a list of strings read from STDIN separated by spaces or newlines, and the number of strings, respectively
    // create a new list of arguments starting with the 2 index of the old argv
    new_argv[0] = argv[1];
    new_argc = 1;
    for (int i = 0; i < read_output && new_argc < MAXARGS; i++) {
        // loops through putting a '\0' (end string) whenever we see a space or new line
        if (buffer[i] == ' ') {
            buffer[i] = '\0';
        }
        else if (buffer[i] == '\n') {
            buffer[i] = '\0';
        }
        else if (buffer[i] == '\r') {
            buffer[i] = '\0';
        }
        // sets the start of the argument at the first spot or whenever the previous one was the end of the string
        else if (i == 0) {
            new_argv[new_argc] = &buffer[i];
            new_argc += 1;
        }
        else if (buffer[i - 1] == '\0') {
            new_argv[new_argc] = &buffer[i];
            new_argc += 1;
        }
    }

    // add NULL at the end so it knows when to stop
    new_argv[new_argc] = NULL;

    // Must execute the program as specified by argv[1], appending /c/ if needed, with argc and argv defined as specified above
    int slash = 0;                  // we must check if the file has a slash anywhere
    int j = 0;
    while (argv[1][j] != '\0') {
        if (argv[1][j] == '/') {
            slash = 1; 
            break; 
        }
        j++;
    }
    if (slash) {
        fd = _open(-1, argv[1]); // opens the path into first available
    }
    else {
        // start path with "/c/""
        file_path[0] = '/';
        file_path[1] = 'c';
        file_path[2] = '/';

        // append the argv path to path
        int i = 0;
        while (argv[1][i] != '\0' && i < BUFSIZE -4) {
            file_path[i + 3] = argv[1][i];
            i++;
        }

        // appends a null at the end of the string so it knows its over
        file_path[i + 3] = '\0';

        fd = _open(-1, file_path); // opens the file path at first available spot
    }

    if (fd < 0) {
        dprintf(CONSOLEOUT, "OPEN FAILED");
        _exit();
    }

    // call exec with the new arguments to call the new function and make sure it suceeded
    exec_output = _exec(fd, new_argc, new_argv);

    if (exec_output < 0) {
        dprintf(CONSOLEOUT, "EXEC FAILED");
    }

    // close then exit
    _close(fd);
    _exit();
}