// shell.c - A basic shell for 391 OS
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "../syscall.h"
#include "../string.h"
#include "../shell.h"
#include "../error.h"
#include "../io.h"

// ---INTERNAL CONSTANT DEFINITIONS--- //
#define BUFSIZE 256
#define MAXARGS 64
#define SKIP_SPACES(buf) while(*buf == ' ') buf++
#define RST_IO()                    \
    do {                            \
        _close(STDIN);              \
        _iodup(CONSOLEOUT, STDIN);  \
        _close(STDOUT);             \
        _iodup(CONSOLEOUT, STDOUT); \
    } while (0)

// ---INTERNAL FUNCTION DECLARATIONS--- //
static void __attribute__ ((noreturn)) exec(int argc, char* argv[]);
static void handle_sq(int argc, char* argv[]);
static void handle_bg(int argc, char* argv[]);
static int handle_file_input(char* path);
static int handle_file_output(char* path);
static void handle_pipe(int argc, char* argv[]);
static void __attribute__ ((noreturn)) parse_and_exec(char* head);

// Executes the file in argv[0], passing in argc and argv. If argv[0] is not a
// valid file path, it should prepend '/c/' before executing.
//
// On entry exec() assumes:
// - /argc/ >= 1.
// - /argv[0]/ is non-NULL and points to a NUL-terminated string.
//
// This function should not return.
// - on success, executes argv[0], clearing the current memory space.
// - on failure, print an error message and immediately exit.
static void exec(int argc, char* argv[]) {
    int fd; // index in processes file descriptor tables
    char file_path[BUFSIZE];

    if (strchr(argv[0], '/') != NULL) {
        fd = _open(-1, argv[0]); // opens the path into first available
    }
    else {
        // start path with "/c/""
        file_path[0] = '/';
        file_path[1] = 'c';
        file_path[2] = '/';

        // append the argv path to path
        int i = 0;
        while (argv[0][i] != '\0' && i < BUFSIZE -4) {
            file_path[i + 3] = argv[0][i];
            i++;
        }

        // appends a null at the end of the string so it knows its over
        file_path[i + 3] = '\0';

        fd = _open(-1, file_path); // opens the file path at first available spot

    }
    // fd still returning negative error
    if (fd < 0) {
        _exit();
    }

    _exec(fd, argc, argv);

    // exec failed, shouldn't happen
    _close(fd);
    _exit();
}

// Execs with /argc/ and /argv/, waits for execution to complete, then 
// returns
//
// On entry handle_file_output() assumes:
// - /argc/ >= 1.
// - /argv[0]/ is non-NULL and points to a NUL-terminated string.
//
// On return handle_sq() guarantees (on both success and failure):
// - A process with /argc/ and /argv/ has been exec'd and has completed
//   execution.
static void handle_sq(int argc, char* argv[]) {
    int child_tid = _fork();

    if (child_tid < 0) {
        dprintf(CONSOLEOUT, "CHILD FORK FAILED\n");
        _exit();
    }

    // when child_tid is 0 were inside the child
    if (child_tid == 0) {
        exec(argc, argv);
    }

    // wait if were in the parent
    _wait(child_tid);
    
    return;
}

// Execs with /argc/ and /argv/, returns immediately
//
// On entry handle_file_output() assumes:
// - /argc/ >= 1.
// - /argv[0]/ is non-NULL and points to a NUL-terminated string.
//
// On return handle_bg() guarantees (on both success and failure):
// - A process with /argc/ and /argv/ has been exec'd (may not have 
//   finished execution).
static void handle_bg(int argc, char* argv[]) {
    int child_tid = _fork();

    if (child_tid < 0) {
        dprintf(CONSOLEOUT, "CHILD FORK FAILED\n");
        _exit();
    }

    // when child_tid is 0 were inside the child
    if (child_tid == 0) {
        exec(argc, argv);
    }

    // same as sq except it does not wait
    return;
}

// Redirects STDIN to /path/.
//
// On entry handle_file_input() assumes:
// - /path/ is non-NULL and points to a NUL-terminated string.
//
// On return handle_file_input() guarantees (on success):
// - STDIN is redirected to the file in /path/.
// - on failure, handle_file_input() returns a negative error code.
static int handle_file_input(char* path) {
    int dup_output;
    int open_output; // index in processes file descriptor tables
    char file_path[BUFSIZE];

    if (path[0] == '/') {
        open_output = _open(-1, path); // opens the path into first available
    }
    else {
        if (path[0] == 'c' && path[1] == '/') {
            file_path[0] = '/';
            int i = 0;
            while (path[i] != '\0' && i < BUFSIZE - 2) {
                file_path[i + 1] = path[i];
                i++;
            }
            file_path[i + 1] = '\0';
            open_output = _open(-1, file_path);
        } else {
            file_path[0] = '/';
            file_path[1] = 'c';
            file_path[2] = '/';

            // append the argv path to path
            int i = 0;
            while (path[i] != '\0' && i < BUFSIZE -4) {
                file_path[i + 3] = path[i];
                i++;
            }

            // appends a null at the end of the string so it knows its over
            file_path[i + 3] = '\0';

            open_output = _open(-1, file_path); // opens the file path at first available spot
        }

    }
    // open_output still returning negative error
    if (open_output < 0) {
        dprintf(CONSOLEOUT, "handle_file_input() Failed\n");
        return -1;
    }

    // make STDIN point to the open file and close both
    _close(STDIN);
    dup_output = _iodup(open_output, STDIN);

    _close(open_output);

    return dup_output;
}

// Redirects STDOUT to /path/. Attemptes to create /path/ if it does not exist.
//
// On entry handle_file_output() assumes:
// - /path/ is non-NULL and points to a NUL-terminated string.
//
// On return handle_file_output() guarantees (on success):
// - /path/ will exist and STDOUT will be redirected to be /path/.
// - on failure, handle_file_output() returns a negative error code.
static int handle_file_output(char* path) {
    int create_output;
    int dup_output;
    int open_output; // index in processes file descriptor tables
    char file_path[BUFSIZE];

    if (path[0] == '/') {
        open_output = _open(-1, path); // opens the path into first available
    
        if (open_output < 0) {
            create_output = _create(path);
            
            if (create_output != 0) {
                return create_output;
            }

            open_output = _open(-1, path);

            if (open_output < 0) {
                return open_output;
            }

            _close(STDOUT);
            dup_output = _iodup(open_output, STDOUT);
            _close(open_output);
            return dup_output;
        }
        _close(STDOUT);
        dup_output = _iodup(open_output, STDOUT);
        _close(open_output);
        return dup_output;
    }
    else {
        if (path[0] == 'c' && path[1] == '/') {
            file_path[0] = '/';
            int i = 0;
            while (path[i] != '\0' && i < BUFSIZE - 2) {
                file_path[i + 1] = path[i];
                i++;
            }
            file_path[i + 1] = '\0';
            open_output = _open(-1, file_path);
        } else {
            file_path[0] = '/';
            file_path[1] = 'c';
            file_path[2] = '/';

            // append the argv path to path
            int i = 0;
            while (path[i] != '\0' && i < BUFSIZE -4) {
                file_path[i + 3] = path[i];
                i++;
            }

            // appends a null at the end of the string so it knows its over
            file_path[i + 3] = '\0';

            open_output = _open(-1, file_path); // opens the file path at first available spot
        }
    
        if (open_output < 0) {
            create_output = _create(file_path);
            
            if (create_output != 0) {
                return create_output;
            }

            open_output = _open(-1, file_path);

            if (open_output < 0) {
                return open_output;
            }

            _close(STDOUT);
            dup_output = _iodup(open_output, STDOUT);
            _close(open_output);
            return dup_output;
        }
        _close(STDOUT);
        dup_output = _iodup(open_output, STDOUT);
         _close(open_output);
        return dup_output;
    }
    // shouldn't reach here
    return -1;
}

// Creates a pipe and a reader and writer process. The writer's STDOUT is 
// redirected to the input of the pipe and the reader's STDIN is redirected
// to the output of the pipe. The writer will immediately exec with argc and
// argv, while the reader will return to continue parsing the remainder of the
// input.
//
// On entry handle_pipe() assumes:
// - /argc/ >= 1.
// - /argv[0]/ is non-NULL and points to a NUL-terminated string.
//
// On return handle_pipe() guarantees (on success):
// - The writer's STDOUT is redirected to the input of a newly created pipe and
//   has exec'ed with /argc/ and /argv/.
// - The reader's STDIN is redirected to the input of said pipe and has 
//   returned.
// - on failure, handle_pipe() exits immediately.
static void handle_pipe(int argc, char* argv[]) {
    int wfdptr = -1;
    int rfdptr = -1;

    int valid = _pipe(&wfdptr, &rfdptr);
    if (valid < 0) {                // create pipe and check
        dprintf(CONSOLEOUT,"pipe open failed\n");
        _exit();
    }

    int child_tid = _fork();
    if (child_tid < 0) {                 // create fork and check
        dprintf(CONSOLEOUT,"fork failed\n");
        _exit();
    }

    if (child_tid == 0) {                 // child thread will always be writer
        _close(rfdptr);
        _close(STDOUT);
        _iodup(wfdptr, STDOUT);
        _close(wfdptr);
        exec(argc, argv);
    }

    _close(STDIN);                     // parent thread reads
    _close(wfdptr);
    _iodup(rfdptr, STDIN);
    _close(rfdptr);

    return;
}

static int is_terminator(char c) {
    switch (c) {
        case ' ':
        case '\0':
        case SQ:
        case BG:
        case FIN:
        case FOUT:
        case PIPE:
            return 1;
        default:
            return 0;
    }
}

static char find_terminator(char* head, char** end) {
    *end = head;
    while (!is_terminator(**end)) (*end)++;
    return **end;
}

static void parse_and_exec(char* head) {
    int argc;
    char* argv[MAXARGS + 1]; // +1 for NULL termination
    char* end;
    char term;
    int res;

    SKIP_SPACES(head);

    // handle args
    for (argc = 0; argc < MAXARGS;) {
        term = find_terminator(head, &end);
        *end = '\0';
        
        if (head != end)
            argv[argc++] = head;
        
        if (term != ' ') break;

        end++;
        SKIP_SPACES(end);
        head = end;
    }

    if (argc == 0) _exit(); // nothing to do

	// Null-terminate the argument array
	argv[argc] = NULL;

    // at this point, anything remaining should be redirection
    while (term != '\0') {
        head = end + 1;
        SKIP_SPACES(head);
        switch (term) {
        case SQ: // run sequentially
            handle_sq(argc, argv);
            // prev cmd has been exec'd and finished
            // reset any redirections and exec the rest
            RST_IO();
            parse_and_exec(head);
            
        case BG: // run in background
            handle_bg(argc, argv);
            // prev cmd has been exec'd, and is running in the background
            // reset any redirections and exec the rest
            RST_IO();
            parse_and_exec(head);

        case FIN: // file input redirection
            term = find_terminator(head, &end);
            *end = '\0';
            res = handle_file_input(head);
            if (res < 0) _exit();
            // we may have more redirection, so we continue
            break;

        case FOUT: // file output redirection
            term = find_terminator(head, &end);
            *end = '\0';
            res = handle_file_output(head);
            if (res < 0) _exit();
            // we may have more redirection, so we continue
            break;

        case PIPE: // set up pipe
            handle_pipe(argc, argv);
            // writer has been exec'd and is writing into the pipe
            // now parse and exec reader, who is reading from the pipe
            parse_and_exec(head);
        }

        if (term == ' ') {
            end++;
            SKIP_SPACES(end);
            term = *end;
        }
    }

    exec(argc, argv);
}

void main() {
    char buf[BUFSIZE];

    buf[BUFSIZE-1] = '\0'; // terminate

    RST_IO();

    // Your starting prompt
	printf("FairlessStarting 391 Shell\n");

	for (;;) {
        // Your shell prompt
        // Make sure your prompt ends in one of '>', '#', '$', '%'
		printf(/* CHANGE ME */ "FAIRLESS SHELL> ");
		getsn(buf, BUFSIZE - 1);

		if (0 == strcmp(buf, "exit"))
			return;

        
        // Now exec the inputted string
        // 
        // For CP3, you should reset the loop 
        // after the exec call has finished
        //
        // YOUR CODE HERE
        // fork and then parse_and_exec
        int child = _fork();
        
        // child runs command
        if (child == 0) {
            parse_and_exec(buf);
        }

        // child giving error code
        if (child < 0) {
            dprintf(CONSOLEOUT, "FORK ERROR\n");
        }
        
        // wait for child
        _wait(child);

        RST_IO();
	}
}