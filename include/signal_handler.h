#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include <fd_set.h>

#include <stdbool.h>
#include <signal.h>

struct signal_handler {
    int fd;
    sigset_t sigset;
    bool termination_requested;
};

extern struct signal_handler signal_handler;

void signal_handler_initialize();
void signal_handler_subscribe(struct fd_set *fd_set);
void signal_handler_notify(struct fd_set const *fd_set);
void signal_handler_finalize();

#endif


/*! \file */
