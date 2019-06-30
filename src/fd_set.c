#include <fd_set.h>

#include <die.h>

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

void fd_set_initialize(struct fd_set *fd_set) {
    fd_set->size = 0;
    fd_set->capacity = 0;
    fd_set->items = NULL;
    fd_set->timeout = -1;
}

void fd_set_add(struct fd_set *fd_set, int fd, short events, int timeout) {
    if (fd_set->size == fd_set->capacity) {
        fd_set->capacity = fd_set->capacity * 5 / 3 + 1;
        assert(fd_set->capacity > fd_set->size);
        size_t byte_capacity = fd_set->capacity * sizeof(fd_set->items[0]);
        fd_set->items = realloc(fd_set->items, byte_capacity);
        if (!fd_set->items) {
            die("`realloc((void*)%p, %zu)` failed: %s\n",
                (void*)fd_set->items, byte_capacity, strerror(errno));
        }
    }
    struct pollfd *item = &fd_set->items[fd_set->size++];
    item->fd = fd;
    item->events = events;
    if (timeout >= 0 && (fd_set->timeout < 0 || timeout < fd_set->timeout)) {
        fd_set->timeout = timeout;
    } 
}

void fd_set_poll(struct fd_set *fd_set) {
    if (poll(fd_set->items, fd_set->size, fd_set->timeout) < 0) {
        die("`poll((struct pollfd*)%p, %zu, %d)` failed: %s\n",
            (void*)fd_set->items, fd_set->size, fd_set->timeout,
            strerror(errno));
    }
}

short fd_set_get_events(struct fd_set const *fd_set, int fd) {
    for (size_t i = 0; i < fd_set->size; ++i) {
        struct pollfd *item = &fd_set->items[i];
        if (item->fd == fd) { return item->revents; }
    }
    return 0;
}

void fd_set_clear(struct fd_set *fd_set) {
    fd_set->size = 0;
    fd_set->timeout = -1;
}

void fd_set_finalize(struct fd_set *fd_set) {
    free(fd_set->items);
}


/*! \file */
