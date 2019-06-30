#include <client.h>

#include <session.h>
#include <message.h>
#include <masprintf.h>
#include <die.h>

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

struct client_message {
    TAILQ_ENTRY(client_message) link;
    struct message *self;
};

struct client_session {
    LIST_ENTRY(client_session) link;
    struct session self;
};

// 
void client_initialize(struct client *client,
    char const* maildir_path, char const* host)
{
    maildir_initialize(&client->maildir, maildir_path);

    client->host = strdup(host);
    if (!client->host) {
        die("`strdup(\"%s\")` failed: %s\n",
            host, strerror(errno));
    }

    TAILQ_INIT(&client->messages);

    LIST_INIT(&client->sessions);
}

void client_subscribe(struct client *client, struct fd_set *fd_set) {
    maildir_subscribe(&client->maildir, fd_set);

    for (struct client_message *message = TAILQ_FIRST(&client->messages);
         message; message = TAILQ_NEXT(message, link))
    { message_subscribe(message->self, fd_set); }

    for (struct client_session *session = LIST_FIRST(&client->sessions);
         session; session = LIST_NEXT(session, link))
    { session_subscribe(&session->self, fd_set); }
}

void client_notify(struct client *client, struct fd_set const *fd_set) {
    maildir_notify(&client->maildir, fd_set);
    while (true) {
        char *path = maildir_discover_message(&client->maildir);
        if (!path) { break; }

        struct client_message *message = malloc(sizeof(*message));
        if (!message) {
            die("`malloc(%zu)` failed: %s\n",
                sizeof(*message), strerror(errno));
        }

        message->self = message_create(path);
        TAILQ_INSERT_TAIL(&client->messages, message, link);

        free(path);
    }

    for (struct client_message *message = TAILQ_FIRST(&client->messages);
         message; )
    {
        struct client_message* next = TAILQ_NEXT(message, link);
        message_notify(message->self, fd_set);
        switch (message->self->state) {
        case MESSAGE_LOADING_HEADERS:
            break;
        case MESSAGE_HEADERS_LOADED:
            for (struct message_destination *destination =
                    TAILQ_FIRST(&message->self->destinations);
                 destination; destination = TAILQ_NEXT(destination, link))
            {
                struct client_session *session = 
                    LIST_FIRST(&client->sessions);
                while (session &&
                       (destination->host_len != 
                            strlen(session->self.destination_host) ||
                        strncmp(session->self.destination_host,
                                destination->host, destination->host_len)))
                { session = LIST_NEXT(session, link); }
                if (!session) {
                    session = malloc(sizeof(*session));
                    if (!session) {
                        die("`malloc(%zu)` failed: %s\n",
                            sizeof(*session), strerror(errno));
                    }
                    char *destination_host = masprintf("%.*s",
                        (int)destination->host_len, destination->host);
                    session_initialize(&session->self,
                        client->host, destination_host);
                    free(destination_host);
                    LIST_INSERT_HEAD(&client->sessions, session, link);
                }
                session_enqueue_message(&session->self, message->self);
            }
            // fallthrough
        case MESSAGE_LOADING_FAILED:
            TAILQ_REMOVE(&client->messages, message, link);
            message_release(message->self);
            free(message);
            break;
        default:
            assert(false);
        }
        message = next;
    }

    for (struct client_session *session = LIST_FIRST(&client->sessions);
         session; )
    {
        struct client_session* next = LIST_NEXT(session, link);
        session_notify(&session->self, fd_set);
        if (session->self.state == SESSION_CLOSED) {
            LIST_REMOVE(session, link);
            session_finalize(&session->self);
            free(session);
        }
        session = next;
    }
}

void client_finalize(struct client *client) {
    while (true) {
        struct client_session *session = LIST_FIRST(&client->sessions);
        if (!session) { break; }
        LIST_REMOVE(session, link);
        session_finalize(&session->self);
        free(session);
    }

    while (true) {
        struct client_message *message = TAILQ_FIRST(&client->messages);
        if (!message) { break; }
        TAILQ_REMOVE(&client->messages, message, link);
        message_release(message->self);
        free(message);
    }

    free(client->host);

    maildir_finalize(&client->maildir);
}

/*! \file */
