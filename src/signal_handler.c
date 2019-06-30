#include <signal_handler.h>

#include <die.h>

#include <sys/signalfd.h>
#include <poll.h>
#include <unistd.h>

#include <string.h>
#include <errno.h>

struct signal_handler signal_handler;

void signal_handler_initialize() {
    sigset_t sigset;
    if (sigemptyset(&sigset)) { die("`sigemptyset(/*...*/)` failed\n"); }
    if (sigaddset(&sigset, SIGINT)) {
        die("`sigaddset(/*...*/, SIGINT)` failed\n");
    }
    if (sigaddset(&sigset, SIGQUIT)) {
        die("`sigaddset(/*...*/, SIGQUIT)` failed\n");
    }

    if (sigprocmask(SIG_BLOCK, &sigset, &signal_handler.sigset)) {
        die("`sigprocmask(SIG_BLOCK, /*...*/)` failed: %s\n", strerror(errno));
    }
    signal_handler.fd = signalfd(-1, &sigset, 0);
    if (signal_handler.fd == -1) {
        die("`signalfd(/*...*/)` failed: %s\n", strerror(errno));
    }

    signal_handler.termination_requested = false;
}

void signal_handler_subscribe(struct fd_set *fd_set) {
    fd_set_add(fd_set, signal_handler.fd, POLLIN, -1);
}

void signal_handler_notify(struct fd_set const *fd_set) {
    if (!fd_set_get_events(fd_set, signal_handler.fd)) { return; }

    struct signalfd_siginfo siginfo;
    if (read(signal_handler.fd, &siginfo,
             sizeof(siginfo)) != sizeof(siginfo))
    { die("`read(signal_handler->fd, /*...*/)` failed\n"); }
    switch (siginfo.ssi_signo) {
        case SIGINT:
        case SIGQUIT:
            signal_handler.termination_requested = true;
            return;
    }
    die("unexpected signal read from signal_handler->fd: %s\n",
        strsignal(siginfo.ssi_signo));
}

void signal_handler_finalize() {
    close(signal_handler.fd);
    if (sigprocmask(SIG_SETMASK, &signal_handler.sigset, NULL)) {
        die("`sigprocmask(SIG_SETMASK, /*...*/, NULL)` failed: %s\n",
            strerror(errno));
    }
}


/*! \file */
