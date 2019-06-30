#ifndef FD_SET_H
#define FD_SET_H

#include <poll.h>

#include <stddef.h>

struct fd_set {
    size_t size;
    size_t capacity;
    struct pollfd *items;
    int timeout;
};

void fd_set_initialize(struct fd_set *fd_set);
void fd_set_add(struct fd_set *fd_set, int fd, short events, int timeout);
void fd_set_poll(struct fd_set *fd_set);
short fd_set_get_events(struct fd_set const *fd_set, int fd);
void fd_set_clear(struct fd_set *fd_set);
void fd_set_finalize(struct fd_set *fd_set);

#endif


/*! \file */
