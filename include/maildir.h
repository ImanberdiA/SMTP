#ifndef MAILDIR_H
#define MAILDIR_H

#include <dirent.h>
#include <sys/queue.h>

#include <fd_set.h>

struct maildir_message;

struct maildir {
    char* path;

    int inotify_fd;
    DIR *dir;

    STAILQ_HEAD(, maildir_message) messages;
};

void maildir_initialize(struct maildir *maildir, char const *path);
void maildir_subscribe(struct maildir *maildir, struct fd_set *fd_set);
void maildir_notify(struct maildir *maildir, struct fd_set const *fd_set);
char *maildir_discover_message(struct maildir *maildir);
void maildir_finalize(struct maildir *maildir);

#endif


/*! \file */
