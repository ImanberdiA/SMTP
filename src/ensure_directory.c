#include <ensure_directory.h>

#include <sys/stat.h>

#include <errno.h>

int ensure_directory(char const *path) {
    if (mkdir(path, 0755)) {
        if (errno != EEXIST) { return -1; }
        struct stat st;
        if (stat(path, &st)) { return -1; }
        if (!S_ISDIR(st.st_mode)) {
            errno = EEXIST;
            return -1;
        }
    }
    return 0;
}


/*! \file */
