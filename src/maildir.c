#include <maildir.h>

#include <die.h>
#include <masprintf.h>
#include <ensure_directory.h>
#include <logger.h>

#include <sys/inotify.h>
#include <sys/unistd.h>

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <stdbool.h>

struct maildir_message {
    STAILQ_ENTRY(maildir_message) link;
    char name[];
};

static void enqueue(struct maildir *maildir, char const* name) {
    for (struct maildir_message *message = STAILQ_FIRST(&maildir->messages);
         message; message = STAILQ_NEXT(message, link))
    { if (!strcmp(message->name, name)) { return; } }

    struct maildir_message *message =
        malloc(sizeof(*message) + strlen(name) + 1);
    if (!message) {
        die("`malloc(%zu)` failed: %s\n",
            sizeof(*message) + strlen(name) + 1, strerror(errno));
    }
    strcpy(message->name, name);

    STAILQ_INSERT_TAIL(&maildir->messages, message, link);
}

void maildir_initialize(struct maildir *maildir, char const *path) {
    maildir->path = strdup(path);
    if (!maildir->path) {
        die("`strdup(\"%s\")` failed: %s\n", path, strerror(errno));
    }

    if (ensure_directory(maildir->path)) {
        die("`ensure_directory(\"%s\")` failed: %s\n",
            maildir->path, strerror(errno));
    }

    char *out_path = masprintf("%s/out", maildir->path);

    if (ensure_directory(out_path)) {
        die("`ensure_directory(\"%s\")` failed: %s\n",
            out_path, strerror(errno));
    }

    maildir->inotify_fd = inotify_init();
    if (maildir->inotify_fd == -1) {
        die("`inotify_init()` failed: %s\n", strerror(errno));
    }
    if (inotify_add_watch(maildir->inotify_fd, out_path, IN_MOVED_TO) == -1) {
        die("`inotify_add_watch(%d, \"%s\", IN_MOVED_TO)` failed: %s\n",
            maildir->inotify_fd, out_path, strerror(errno));
    }

    maildir->dir = opendir(out_path);
    if (!maildir->dir) {
        die("`opendir(\"%s\")` failed: %s\n", out_path, strerror(errno));
    }

    free(out_path);

    STAILQ_INIT(&maildir->messages);
}

void maildir_subscribe(struct maildir *maildir, struct fd_set *fd_set) {
    fd_set_add(fd_set, maildir->inotify_fd, POLLIN, -1);
    if (maildir->dir) {
        int fd = dirfd(maildir->dir);
        if (fd == -1) {
            die("`dirfd((DIR*)%p)` failed: %s\n",
                (void*)maildir->dir, strerror(errno));
        }
        fd_set_add(fd_set, fd, POLLIN | POLLHUP, -1);
    }
}

void maildir_notify(struct maildir *maildir, struct fd_set const *fd_set) {
    if (fd_set_get_events(fd_set, maildir->inotify_fd) & POLLIN) {
        struct inotify_event *event;
        char buffer[sizeof(*event) + NAME_MAX + 1];
        event = (void*)buffer;
        ssize_t size = read(maildir->inotify_fd, event, sizeof(buffer));
        if (size == -1) {
            die("`read(%d, (struct inotify_event*)%p, %zu)` failed: %s\n",
                maildir->inotify_fd, (void*)event, sizeof(buffer),
                strerror(errno));
        }
        while (size > 0) {
            // It's unclear if partial event reads are possible.
            assert(size >= sizeof(*event));
            size_t event_size = sizeof(*event) + event->len;
            assert(size >= event_size);

            enqueue(maildir, event->name);

            size -= event_size;
            event = (void*)((char*)event + event_size);
        }
    }

    if (maildir->dir) {
        int fd = dirfd(maildir->dir);
        if (fd == -1) {
            die("`dirfd((DIR*)%p)` failed: %s\n",
                (void*)maildir->dir, strerror(errno));
        }
        if (fd_set_get_events(fd_set, fd) & (POLLIN | POLLHUP)) {
            errno = 0;
            struct dirent *dirent = readdir(maildir->dir);
            if (!dirent) {
                if (errno) {
                    die("`readdir((DIR*)%p)` failed: %s\n",
                        (void*)maildir->dir, strerror(errno));
                }

                if (closedir(maildir->dir)) {
                    die("`closedir((DIR*)%p)` failed: %s\n",
                        (void*)maildir->dir, strerror(errno));
                }
                maildir->dir = NULL;
            } else if (strcmp(dirent->d_name, ".") &&
                       strcmp(dirent->d_name, ".."))
            { enqueue(maildir, dirent->d_name); }
        }
    }
}

char *maildir_discover_message(struct maildir *maildir) {
    if (maildir->dir) { return NULL; }
    struct maildir_message *message = STAILQ_FIRST(&maildir->messages);
    if (!message) { return NULL; }

    char *path = masprintf("%s/out/%s", maildir->path, message->name);

    STAILQ_REMOVE_HEAD(&maildir->messages, link);
    free(message);

    return path;
}

void maildir_finalize(struct maildir *maildir) {
    while (true) {
        struct maildir_message *message = STAILQ_FIRST(&maildir->messages);
        if (!message) { break; }
        STAILQ_REMOVE_HEAD(&maildir->messages, link);
        free(message);
    }

    if (maildir->dir) {
        if (closedir(maildir->dir)) {
            die("`closedir((DIR*)%p)` failed: %s\n",
                (void*)maildir->dir, strerror(errno));
        }
    }

    if (close(maildir->inotify_fd)) { 
        die("`close(%d)` failed: %s\n", maildir->inotify_fd, strerror(errno));
    }

    free(maildir->path);
}


/*! \file */
