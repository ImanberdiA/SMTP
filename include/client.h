#ifndef CLIENT_H
#define CLIENT_H

#include <sys/queue.h>

#include <maildir.h>
#include <fd_set.h>

struct client_message;
struct client_session;

struct client {
    struct maildir maildir;
    char *host;
    TAILQ_HEAD(, client_message) messages;
    LIST_HEAD(, client_session) sessions;
};

void client_initialize(struct client *client,
    char const* maildir_path, char const* host);
void client_subscribe(struct client *client, struct fd_set *fd_set);
void client_notify(struct client *client, struct fd_set const *fd_set);
void client_finalize(struct client *client);

#endif


/*! \file */
