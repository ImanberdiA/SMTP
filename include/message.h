#ifndef MESSAGE_H
#define MESSAGE_H

#include <fd_set.h>

#include <sys/queue.h>

#include <stddef.h>

enum message_state {
    MESSAGE_LOADING_FAILED,
    MESSAGE_LOADING_HEADERS,
    MESSAGE_HEADERS_LOADED,
    MESSAGE_LOADING_BODY,
    MESSAGE_BODY_LOADED,
};

struct message_header {
    TAILQ_ENTRY(message_header) link;

    char* name;
    size_t name_len;

    char* value;
    size_t value_len;
};

struct message_recepient {
    TAILQ_ENTRY(message_recepient) link;

    char *user;
    size_t user_len;
};

struct message_destination {
    TAILQ_ENTRY(message_destination) link;

    char *host;
    size_t host_len;

    TAILQ_HEAD(, message_recepient) recepients;
};

struct message {
    enum message_state state;

    char *path;
    int fd;

    size_t offset;

    size_t capacity;
    size_t size;
    char *buffer;

    char *headers_;
    size_t headers_len;

    TAILQ_HEAD(, message_header) headers;

    char *sender;
    size_t sender_len;

    TAILQ_HEAD(, message_destination) destinations;

    char *body;
    size_t body_len;

    size_t ref_count;
};

struct message *message_create(char const *path);
struct message *message_retain(struct message *message);
void message_subscribe(struct message *message, struct fd_set *fd_set);
void message_notify(struct message *message, struct fd_set const *fd_set);
void message_start_loading_body(struct message *message);
void message_mark_as_sent(struct message *message,
    char const* destination_host);
void message_release(struct message *message);

#endif

/*! \file */
