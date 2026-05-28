#include "../syscall.h"
#include "../string.h"
#include "../shell.h"
#include "../error.h"
#include "../io.h"
void main(int argc, char** argv) {
    // Creates the files passed as arguments by the user.
    // create the empty files from create the empty files
    int create_output;
    // if argc is one its just the command name and no files
    if (argc <= 1) { 
        dprintf(CONSOLEOUT, "ERROR: needs a file / not enough arguments");
        _exit();
    }

    // creates files from NGFS image
    for (int i = 1; i < argc; i++) {
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
            // create file
            create_output = _create(path);

            // checks if create was sucessful
            if (create_output != 0) {
                dprintf(CONSOLEOUT, "Create Failed");
            }
    }

    _exit();
}