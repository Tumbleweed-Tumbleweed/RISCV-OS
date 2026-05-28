// YOUR CODE HERE
#include "../syscall.h"
#include "../string.h"
#include "../shell.h"
#include "../error.h"
#include "../io.h"

// From shell
#define BUFSIZE 256
#define MAXARGS 64

void main(int argc, char** argv) {
    char buffer[BUFSIZE];
    int open_output; // fd index given by open
    long read_output;
    int reading = 1;
    int line_cnt = 0; // number of lines
    int word_cnt = 0; // number of words (aka # of spaces + 1)
    int char_cnt = 0; // number of characters
    int inside_word = 0; // 1 if we are in the middle of a word

    // Reads a file and outputs the number of lines, words, and characters in the file to STDOUT. If no file is
    // specified, it should read from STDIN.
    if (argc == 1) {
        // Must read from STDIN if no other arguments are passed in (argc = 1).
        // read from STDIN
        while (reading) {
            // tries to read from STDIN
            read_output = _read(STDIN, buffer, BUFSIZE);
            
            // check if the read was valid
            if (read_output < 0) {
                dprintf(CONSOLEOUT, "READ FAILED");
                reading = 0;
            }
            else if (read_output == 0) {
                reading = 0;
            }
            
            // implement the checks to count the lines, words, and chars
            // by looping through the buffer
            for (int i = 0; i < read_output; i++) {
                char_cnt++;
                
                switch (buffer[i]) {
                    // check char in the buffer and increment variables apporpriately
                    case '\0': 
                        
                        if (inside_word) {
                            inside_word = 0;
                        }
                        break;
                    
                    case '\n':
                        line_cnt += 1;
                        
                        if (inside_word) {
                            inside_word = 0;
                        }
                        break;
                    
                    case ' ':
                        if (inside_word) {
                            inside_word = 0;
                        }
                        break;
                    
                    case '\r':
                        if (inside_word) {
                            inside_word = 0;
                        }
                        break;
                    
                    default:
                        if (!inside_word) { 
                            // increment word count and start inside word everytime a word starts
                            word_cnt += 1;
                            inside_word = 1;
                        }
                }
            }
        }
        
        dprintf(STDOUT, "%d\t%d\t%d\n", line_cnt, word_cnt, char_cnt);
        _exit();
    }
    
    else if (argc > 1) {
        // Must read from the file as specified by the first argument (argv[1]), if it exists.
        char file_path[BUFSIZE];
        char *path = argv[1];
        if (argv[1][0] != '/') {
            file_path[0] = '/';
            int j = 0;
            while (argv[1][j] != '\0' && j < BUFSIZE - 2) {
                file_path[j + 1] = argv[1][j];
                j++;
            }
            file_path[j + 1] = '\0';
            path = file_path;
        }

        open_output = _open(-1, path);
        if (open_output < 0) {
            dprintf(CONSOLEOUT, "INVALID PATH");
            _exit();
        }
        
        while (reading) {
            // tries to read from argv[1]
            read_output = _read(open_output, buffer, BUFSIZE);
            
            // check if the read was valid
            if (read_output < 0) {
                dprintf(CONSOLEOUT, "READ FAILED");
                reading = 0;
            }
            else if (read_output == 0) {
                reading = 0;
            }
            
            // implement the checks to count the lines, words, and chars
            // by looping through the buffer
            for (int i = 0; i < read_output; i++) {
                char_cnt++;
                
                switch (buffer[i]) {
                    // check char in the buffer and increment variables apporpriately
                    case '\0': 
                        if (inside_word) {
                            inside_word = 0;
                        }
                        break;
                    
                    case '\n':
                        line_cnt += 1;
                        
                        if (inside_word) {
                            inside_word = 0;
                        }
                        break;
                    
                    case ' ':
                        if (inside_word) {
                            inside_word = 0;
                        }
                        break;

                    case '\r':
                        if (inside_word) {
                            inside_word = 0;
                        }
                        break;
                    
                    default:
                        if (!inside_word) {
                            word_cnt += 1;
                            inside_word = 1;
                        }
                    }
            }
        }
        
        _close(open_output);
        // Must write the number of lines, words, and characters in the file to STDOUT, separated by tabs.
        dprintf(STDOUT, "%d\t%d\t%d\n", line_cnt, word_cnt, char_cnt);
        _exit();
    }

    
    else {
        dprintf(CONSOLEOUT, "argc < 1");
        _exit();
    }
    
    _exit(); // just incase

}