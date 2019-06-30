#include <message.h>

#include <die.h>
#include <logger.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <assert.h>

static void parse_header(struct message *message, char *line, size_t line_len)
{
    char *line_end = line + line_len;
    char *colon = line;
    while (colon < line_end && *colon != ':') { ++colon; }
    if (colon == line_end) {
        message->state = MESSAGE_LOADING_FAILED;
        logger_printf("malformed header: no ':' separating name from value\n"
            "  message %s skipped\n", message->path);
        return;
    }

    char *name = line;
    char *name_end = colon;
    while (name < name_end && strchr(" \t", name_end[-1])) { --name_end; }
    size_t name_len = name_end - name;

    char *value = colon + 1;
    char *value_end = line_end;
    while (value < value_end && strchr(" \t", *value)) { ++value; }
    while (value < value_end && strchr(" \t", value_end[-1])) { --value_end; }
    size_t value_len = value_end - value;

    struct message_header *header = malloc(sizeof(*header));
    if (!header) {
        die("`malloc(%zu)` failed: %s\n", sizeof(*header), strerror(errno));
    }

    header->name = name;
    header->name_len = name_len;

    header->value = value;
    header->value_len = value_len;

    TAILQ_INSERT_TAIL(&message->headers, header, link);
}

static void parse_headers(struct message *message) {
    char *headers_end = message->headers_ + message->headers_len;
    char *line = message->headers_;
    char *line_end = line;
    while (true) {
        static char const separator[] = "\r\n";
        static size_t const separator_len = sizeof(separator) - 1;
        while (headers_end - line_end >= separator_len) {
            if (!strncmp(line_end, separator, separator_len)) { break; }
            ++line_end;
        }
        if (headers_end - line_end < separator_len) { line_end = headers_end; }
        if (headers_end - line_end > separator_len &&
            strchr(" \t", line_end[separator_len]))
        {
            line_end += separator_len + 1;
            continue;
        }

        parse_header(message, line, line_end - line);
        if (message->state == MESSAGE_LOADING_FAILED) { break; }

        if (line_end == headers_end) { break; }
        line_end += separator_len;
        line = line_end;
    }
}

static void parse_sender_and_destinations(struct message *message) {
    for (struct message_header *header = TAILQ_FIRST(&message->headers); header; )
    {
        struct message_header *next = TAILQ_NEXT(header, link);

        static char const sender_header_name[] = "X-Original-From";
        static size_t const sender_header_name_len =
            sizeof(sender_header_name) - 1;
        if (header->name_len == sender_header_name_len &&
            !strncmp(sender_header_name, header->name, header->name_len))
        {
            message->sender = header->value;
            message->sender_len = header->value_len;
            goto remove;
        }
        
        static char const recepient_header_name[] = "X-Original-To";
        static size_t const recepient_header_name_len =
            sizeof(recepient_header_name) - 1;
        if (header->name_len == recepient_header_name_len &&
            !strncmp(recepient_header_name, header->name, header->name_len))
        {
            char *value = header->value;
            char *value_end = value + header->value_len;
            char *at = value;
            while (at < value_end && *at != '@') { ++at; }
            if (at == value_end) {
                message->state = MESSAGE_LOADING_FAILED;
                logger_printf("malformed 'X-Original-To' header: "
                    "not an email address\n  potential recepient skipped\n");
                return;
            }

            char *user = value;
            size_t user_len = at - user;

            char *host = at + 1;
            size_t host_len = value_end - host;

            struct message_destination *destination =
                TAILQ_FIRST(&message->destinations);
            while (destination && 
                   (destination->host_len != host_len ||
                    strncmp(host, destination->host, destination->host_len)))
            { destination = TAILQ_NEXT(destination, link); }
            if (!destination) {
                destination = malloc(sizeof(*destination));
                if (!destination) {
                    die("`malloc(%zu)` failed: %s\n",
                        sizeof(*destination), strerror(errno));
                }
                destination->host = host;
                destination->host_len = host_len;
                TAILQ_INIT(&destination->recepients);
                TAILQ_INSERT_TAIL(&message->destinations, destination, link);
            }

            struct message_recepient *recepient =
                TAILQ_FIRST(&destination->recepients);
            while (recepient &&
                   (recepient->user_len != user_len ||
                    strncmp(user, recepient->user, recepient->user_len)))
            { recepient = TAILQ_NEXT(recepient, link); }
            if (!recepient) {
                recepient = malloc(sizeof(*recepient));
                if (!recepient) {
                    die("`malloc(%zu)` failed: %s\n",
                        sizeof(*recepient), strerror(errno));
                }
                recepient->user = user;
                recepient->user_len = user_len;
                TAILQ_INSERT_TAIL(&destination->recepients, recepient, link);
            }

            goto remove;
        }

        goto next;

    remove:
        TAILQ_REMOVE(&message->headers, header, link);
        free(header);

    next:
        header = next;
    }
}

struct message *message_create(char const *path) {
    struct message *message = malloc(sizeof(*message));
    if (!message) {
        die("`malloc(%zu)` failed: %s\n", sizeof(*message), strerror(errno));
    }

    message->state = MESSAGE_LOADING_HEADERS;

    message->path = strdup(path);
    if (!message->path) {
        die("`strdup(\"%s\")` failed: %s\n", path, strerror(errno));
    }

    message->fd = -1;

    message->offset = 0;

    message->capacity = 0;
    message->size = 0;
    message->buffer = NULL;

    message->headers_ = NULL;
    message->headers_len = 0;

    TAILQ_INIT(&message->headers);

    message->sender = NULL;
    message->sender_len = 0;

    TAILQ_INIT(&message->destinations);

    message->body = NULL;
    message->body_len = 0;

    message->ref_count = 1;

    return message;
}

struct message *message_retain(struct message *message) {
    ++message->ref_count;
    return message;
}

void message_subscribe(struct message *message, struct fd_set *fd_set) {
    if (message->state != MESSAGE_LOADING_HEADERS && 
        message->state != MESSAGE_LOADING_BODY) { return; }

    if (message->fd == -1) {
        message->fd = open(message->path, O_RDONLY);
        // Если файл не существует, то open() вернет значение (-1)
        if (message->fd == -1) {
            logger_printf("`open(\"%s\", O_RDONLY)` failed: %s\n"
                "  message skipped\n", message->path, strerror(errno));
            message->state = MESSAGE_LOADING_FAILED;
            return;
        }

        int flags = fcntl(message->fd, F_GETFL);
        if (flags == -1) {
            die("`fcntl(%d, F_GETFL)` failed: %s\n",
                message->fd, strerror(errno));
        }
        if (fcntl(message->fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            die("`fcntl(%d, F_SETFL, %d | O_NONBLOCK)` failed: %s\n",
                message->fd, flags, strerror(errno));
        }

        if (lseek(message->fd, message->offset, SEEK_SET) == -1) {
            logger_printf("`lseek(%d, %zu, SEEK_SET)` failed: %s\n"
                "  message %s skipped\n",
                message->fd, message->offset, strerror(errno), message->path);
        }
    }

    fd_set_add(fd_set, message->fd, POLLIN, -1);
}

void message_notify(struct message *message, struct fd_set const *fd_set) {
    if (message->state != MESSAGE_LOADING_HEADERS && 
        message->state != MESSAGE_LOADING_BODY) { return; }

    if (!(fd_set_get_events(fd_set, message->fd) & POLLIN)) { return; }

    while (true) {
        if (message->size == message->capacity) {
            message->capacity = message->capacity * 5 / 3 + 1;
            message->buffer = realloc(message->buffer, message->capacity);
            if (!message->buffer) {
                die("`realloc((void*)%p, %zu)` failed: %s",
                    (void*)message->buffer, message->capacity,
                    strerror(errno));
            }
        }

        ssize_t read_size = read(message->fd,
            message->buffer + message->size,
            message->capacity - message->size);
        if (read_size == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                message->state = MESSAGE_LOADING_FAILED;
                logger_printf("`read(%d, (void*)%p, %zu)` failed: %s\n"
                    "  message %s skipped\n",
                    message->fd, (void*)(message->buffer + message->size),
                    message->capacity - message->size,
                    strerror(errno), message->path);
                goto cleanup;
            }
            return;
        }
        message->size += read_size;

        if (message->state == MESSAGE_LOADING_HEADERS) {
           if (read_size == 0) {
               message->state = MESSAGE_LOADING_FAILED;
               logger_printf("message %s loading failed: "
                   "no empty line separting headers from body\n  skipped\n",
                   message->path);
               goto cleanup;
           }

            static char const separator[] = "\r\n\r\n";
            // static char const separator[] = "\n\n";
            static size_t const separator_len = sizeof(separator) - 1;
            while (message->headers_len + separator_len <= message->size) {
                if (!strncmp(message->buffer + message->headers_len,
                             separator, separator_len))
                {
                    message->state = MESSAGE_HEADERS_LOADED;

                    message->offset += message->headers_len + separator_len;

                    message->headers_ = 
                        realloc(message->buffer, message->headers_len);
                    if (!message->headers_) {
                        die("`realloc((void*)%p, %zu)` failed: %s",
                            (void*)message->buffer, message->headers_len,
                            strerror(errno));
                    }
                    message->capacity = 0;
                    message->size = 0;
                    message->buffer = NULL;

                    parse_headers(message);
                    parse_sender_and_destinations(message);

                    goto cleanup;
                }
                ++message->headers_len;
            }
        } else if (message->state == MESSAGE_LOADING_BODY) {
            if (read_size == 0) {
                message->state = MESSAGE_BODY_LOADED;

                message->offset += message->size;

                message->body_len = message->size;
                message->body = realloc(message->buffer, message->body_len);
                if (!message->body) {
                    die("`realloc((void*)%p, %zu)` failed: %s",
                        (void*)message->buffer, message->body_len,
                        strerror(errno));
                }
                message->capacity = 0;
                message->size = 0;
                message->buffer = NULL;
                
                goto cleanup;
            }
        }
    }

cleanup:
    if (message->fd != -1) {
        if (close(message->fd)) {
            die("`close(%d)` failed: %s",
                message->fd, strerror(errno));
        }
        message->fd = -1;
    }

    if (message->buffer) {
        free(message->buffer);

        message->capacity = 0;
        message->size = 0;
        message->buffer = NULL;
    }
}

void message_start_loading_body(struct message *message) {
    if (message->state == MESSAGE_LOADING_FAILED ||
        message->state == MESSAGE_LOADING_BODY ||
        message->state == MESSAGE_BODY_LOADED) { return; }
    assert(message->state == MESSAGE_HEADERS_LOADED);
    message->state = MESSAGE_LOADING_BODY;
}

void message_mark_as_sent(struct message *message,
    char const* destination_host)
{
    assert(message->state == MESSAGE_BODY_LOADED);
    
    for (struct message_destination *destination =
             TAILQ_FIRST(&message->destinations);
        destination; destination = TAILQ_NEXT(destination, link))
    {
        if (destination->host_len != strlen(destination_host) ||
            strncmp(destination_host, destination->host,
                                      destination->host_len)) { continue; }

        while (true) {
            struct message_recepient *recepient =
                TAILQ_FIRST(&destination->recepients);
            if (!recepient) { break; }
            TAILQ_REMOVE(&destination->recepients, recepient, link);
            free(recepient);
        }

        TAILQ_REMOVE(&message->destinations, destination, link);
        free(destination);
        break;
    }
}

void message_release(struct message *message) {
    if (--message->ref_count > 0) { return; }
    
    if (message->state == MESSAGE_BODY_LOADED &&
        TAILQ_EMPTY(&message->destinations))
    {
        if (unlink(message->path)) {
            die("`unlink(\"%s\")` failed: %s",
                message->path, strerror(errno));
        }
    }

    free(message->body);

    while (true) {
        struct message_destination *destination =
            TAILQ_FIRST(&message->destinations);
        if (!destination) { break; }

        while (true) {
            struct message_recepient *recepient =
                TAILQ_FIRST(&destination->recepients);
            if (!recepient) { break; }
            TAILQ_REMOVE(&destination->recepients, recepient, link);
            free(recepient);
        }

        TAILQ_REMOVE(&message->destinations, destination, link);
        free(destination);
    }

    while (true) {
        struct message_header *header = TAILQ_FIRST(&message->headers);
        if (!header) { break; }
        TAILQ_REMOVE(&message->headers, header, link);
        free(header);
    }

    free(message->headers_);

    free(message->buffer);

    if (message->fd != -1 && close(message->fd)) {
        die("`close(%d)` failed: %s",
            message->fd, strerror(errno));
    }

    free(message->path);

    free(message);
}


/*! \file */
