#ifndef SETTINGS_H
#define SETTINGS_H

struct settings {
    char *log_path;
    char *maildir_path;
    char *host;
};

extern struct settings settings;

void settings_initialize(int argc, char *argv[]);
void settings_finalize();

#endif


/*! \file */
