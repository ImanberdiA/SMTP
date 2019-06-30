#ifndef SESSION_H
#define SESSION_H

#include <fd_set.h>
#include <message.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/queue.h>

#include <ares.h>

#include <stdbool.h>
#include <stdio.h>

enum session_state {
    SESSION_RESOLVING_DNS,
    SESSION_CONNECTING,
    SESSION_RECEIVING_GREETING,
    SESSION_SENDING_HELO,
    SESSION_SENDING_MAIL_OR_RCPT,
    SESSION_SENDING_DATA,
    SESSION_LOADING_MESSAGE_BODY,
    SESSION_SENDING_DATA_PAYLOAD,
    SESSION_SENDING_RSET,
    SESSION_SENDING_QUIT,
    SESSION_CLOSED,
};

struct session_message;

struct session {
    enum session_state state;

    char *host;
    char *destination_host;

    bool channel_initialized;
    ares_channel channel;

    struct ares_mx_reply *first_mx_reply;
    struct ares_mx_reply *mx_reply;

    struct hostent *hostent;
    size_t addr_index;

    int fd;
    struct sockaddr_storage sockaddr;

    size_t response_capacity;
    size_t response_size;
    char *response_buffer;
    size_t response_line_len;
    int response_code;

    size_t request_size;
    char *request_buffer;
    size_t request_offset;

    TAILQ_HEAD(, session_message) messages;
    struct message_recepient *message_recepient;
};

void session_initialize(struct session *session,
    char const *host, char const *destination_host);
void session_subscribe(struct session *session, struct fd_set *fd_set);
void session_notify(struct session *session, struct fd_set const *fd_set);
void session_enqueue_message(struct session *session, struct message* message);
void session_finalize(struct session *session);

#endif


/*! \file */
