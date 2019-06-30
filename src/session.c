#include <session.h>

#include <logger.h>
#include <die.h>

#include <arpa/nameser.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <unistd.h>

#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>

struct session_message {
    TAILQ_ENTRY(session_message) link;
    struct message *self;
};

static void a_search_callback(void *arg, int status, int timeouts,
    unsigned char *reply_data, int reply_size);

static void aaaa_search_callback(void *arg, int status, int timeouts,
    unsigned char *reply_data, int reply_size);

static void try_next_mx_reply(struct session *session) {
    session->mx_reply = session->mx_reply->next;
    if (!session->mx_reply) {
        session->state = SESSION_CLOSED;
        logger_printf("out of MX records to try for %s\n  session aborted",
            session->destination_host);
        return;
    }
    ares_search(session->channel, session->mx_reply->host,
        ns_c_in, ns_t_aaaa, aaaa_search_callback, session);
}

static void try_addr(struct session *session) {
start:;
    char *addr = session->hostent->h_addr_list[session->addr_index];
    sa_family_t sa_family = session->hostent->h_addrtype;
    if (!addr) {
        ares_free_hostent(session->hostent);
        if (sa_family == AF_INET6) {
            logger_printf("out of IPv6 addresses to try for %s\n",
                session->mx_reply->host);
            ares_search(session->channel, session->mx_reply->host,
                ns_c_in, ns_t_a, a_search_callback, session);
            return;
        }
        logger_printf("out of IPv4 addresses to try for %s\n",
            session->mx_reply->host);
        try_next_mx_reply(session);
        return;
    }

    struct sockaddr *sa = (void*)&session->sockaddr;

    if (session->fd != -1 && sa->sa_family != sa_family) {
        if (close(session->fd)) {
            die("`close(%d)` failed: %s\n", session->fd, strerror(errno));
        }
        session->fd = -1;
    }

    socklen_t addrlen;
    if (sa_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (void*)sa;
        addrlen = sizeof(*sin6);
        memset(sin6, 0, addrlen);
        memcpy(&sin6->sin6_addr, addr, session->hostent->h_length);
        sin6->sin6_port = htons(25);
    } else {
        struct sockaddr_in *sin = (void*)sa;
        addrlen = sizeof(*sin);
        memset(sin, 0, addrlen);
        memcpy(&sin->sin_addr, addr, session->hostent->h_length);
        sin->sin_port = htons(25);
    }
    sa->sa_family = sa_family;

    if (session->fd == -1) {
        session->fd =
            socket(sa->sa_family, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
        if (session->fd == -1) {
            die("`socket(AF_INET%s, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP)`"
                " failed: %s\n",
                &"6"[sa->sa_family != AF_INET6], strerror(errno));
        }
    }

    if (connect(session->fd, (struct sockaddr*)sa, addrlen)) {
        if (errno == EINPROGRESS) {
            session->state = SESSION_CONNECTING;
            return;
        }
        char addrstr[INET_ADDRSTRLEN > INET6_ADDRSTRLEN ?
                     INET_ADDRSTRLEN : INET6_ADDRSTRLEN];
        inet_ntop(sa_family, addr, addrstr, sizeof(addrstr));
        logger_printf("`connect(%d, /* %s */)` failed: %s\n",
            session->fd, addrstr, strerror(errno));

        ++session->addr_index;
        goto start;
    }

    session->state = SESSION_RECEIVING_GREETING;
}

static void a_search_callback(void *arg, int status, int timeouts,
    unsigned char *reply_data, int reply_size)
{
    struct session *session = arg;
    (void)timeouts;

    if (status != ARES_SUCCESS) {
        logger_printf("`ares_search(/*...*/, \"%s\", ns_c_in, ns_t_a, "
            "a_search_callback, (struct session*)%p)` failed: %s\n",
            session->mx_reply->host, (void*)session, ares_strerror(status));
        try_next_mx_reply(session);
        return;
    }

    {
        int status = ares_parse_a_reply(
            reply_data, reply_size, &session->hostent, NULL, NULL);
        if (status != ARES_SUCCESS) {
            logger_printf("`ares_parse_a_reply(/*...*/)` failed: %s\n",
                ares_strerror(status));
            try_next_mx_reply(session);
            return;
        }
    }
    session->addr_index = 0;

    try_addr(session);
}
 
static void aaaa_search_callback(void *arg, int status, int timeouts,
    unsigned char *reply_data, int reply_size)
{
    struct session *session = arg;
    (void)timeouts;

    if (status != ARES_SUCCESS) {
        logger_printf("`ares_search(/*...*/, \"%s\", ns_c_in, ns_t_aaaa, "
            "aaaa_search_callback, (struct session*)%p)` failed: %s\n",
            session->mx_reply->host, (void*)session, ares_strerror(status));
        ares_search(session->channel, session->mx_reply->host,
            ns_c_in, ns_t_a, a_search_callback, session);
        return;
    }

    {
        int status = ares_parse_aaaa_reply(
            reply_data, reply_size, &session->hostent, NULL, NULL);
        if (status != ARES_SUCCESS) {
            logger_printf("`ares_parse_aaaa_reply(/*...*/)` failed: %s\n",
                ares_strerror(status));
            ares_search(session->channel, session->mx_reply->host,
                ns_c_in, ns_t_a, a_search_callback, session);
            return;
        }
    }
    session->addr_index = 0;

    try_addr(session);
}

static void mx_search_callback(void *arg, int status, int timeouts,
    unsigned char *reply_data, int reply_size)
{
    struct session *session = arg;
    (void)timeouts;

    if (status != ARES_SUCCESS) {
        session->state = SESSION_CLOSED;
        logger_printf("`ares_search(/*...*/, \"%s\", ns_c_in, ns_t_mx, "
            "mx_search_callback, (struct session*)%p)` failed: %s\n",
            session->destination_host, (void*)session, ares_strerror(status));
        return;
    }

    {
        int status = ares_parse_mx_reply(
            reply_data, reply_size, &session->first_mx_reply);
        if (status != ARES_SUCCESS) {
            session->state = SESSION_CLOSED;
            logger_printf("`ares_parse_mx_reply(/*...*/)` failed: %s\n"
                "  session to %s aborted\n",
                ares_strerror(status), session->destination_host);
            return;
        }
    }
    session->mx_reply = session->first_mx_reply;

    ares_search(session->channel, session->mx_reply->host,
        ns_c_in, ns_t_aaaa, aaaa_search_callback, session);
}

static bool try_receive_response(struct session *session) {
    while (true) {
        if (session->response_size == session->response_capacity) {
            session->response_capacity =
                session->response_capacity * 5 / 3 + 1;
            session->response_buffer =
                realloc(session->response_buffer, session->response_capacity);
            if (!session->response_buffer) {
                die("`realloc(/* ... */, %zu)` failed: %s\n",
                    session->response_capacity, strerror(errno));
            }
        }
        ssize_t read_size = read(session->fd,
            session->response_buffer + session->response_size,
            session->response_capacity - session->response_size);
        if (read_size == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                session->state = SESSION_CLOSED;
                logger_printf("`read(%d, (char*)%p, %zu)` failed: %s\n"
                    "  session to %s aborted\n",
                    session->fd,
                    (void*)(session->response_buffer + session->response_size),
                    session->response_capacity - session->response_size,
                    strerror(errno),
                    session->destination_host);
            }
            return false;
        } else if (read_size == 0) {
            session->state = SESSION_CLOSED;
            logger_printf("%s has unexpectedly down shut the connection\n"
                "  session aborted\n",
                session->destination_host);
            return false;
        }
        session->response_size += read_size;

        static char const separator[] = "\r\n";
        static size_t const separator_len = sizeof(separator) - 1;
        while (session->response_line_len + separator_len <=
               session->response_size)
        {
            if (!strncmp(session->response_buffer + session->response_line_len,
                         separator, separator_len))
            { 
                static size_t const code_len = 3;
                if (session->response_line_len < code_len) {
                    session->state = SESSION_CLOSED;
                    logger_printf("reply too short\n"
                        "  session to %s aborted\n",
                        session->destination_host);
                    return false;
                }

                char buffer[code_len + 1];
                memcpy(buffer, session->response_buffer, code_len);
                buffer[code_len] = '\0';
                if (sscanf(buffer, "%d", &session->response_code) != 1) {
                    session->state = SESSION_CLOSED;
                    logger_printf("failed to parse reponse code\n"
                        "  session to %s aborted\n",
                        session->destination_host);
                    return false;
                }

                return true;
            }
            ++session->response_line_len;
        }
    }
}

static bool try_send_request(struct session *session) {
    while (true) {
        if (session->request_offset == session->request_size) { return true; }
        ssize_t write_size = write(session->fd,
            session->request_buffer + session->request_offset,
            session->request_size - session->request_offset);
        if (write_size == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                session->state = SESSION_CLOSED;
                logger_printf("`write(%d, (char*)%p, %zu)` failed: %s\n"
                    "  session to %s aborted\n",
                    session->fd,
                    (void*)(session->request_buffer + session->request_offset),
                    session->request_size - session->request_offset,
                    strerror(errno),
                    session->destination_host);
            }
            return false;
        } else if (write_size == 0) {
            logger_printf("write(%d, (char*)%p, %zu) returned 0\n",
                session->fd,
                (void*)(session->request_buffer + session->request_offset),
                session->request_size - session->request_offset);
            return false;
        }
        session->request_offset += write_size;
    }
}

#ifdef __GNUC__
    __attribute__((format(printf, 2, 3)))
#endif
static int checked_fprintf(FILE *stream, char const *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vfprintf(stream, format, args);
    va_end(args);
    if (result < 0) {
        die("`vfprintf(/* ... */)` failed: %s\n", strerror(errno));
    }
    return result;
}

static void write_data_payload(struct session *session, FILE *stream) {
    struct session_message *message = TAILQ_FIRST(&session->messages);

    for (struct message_header *header = TAILQ_FIRST(&message->self->headers);
         header; header = TAILQ_NEXT(header, link))
    {
        checked_fprintf(stream, "%.*s: %.*s\r\n",
            (int)header->name_len, header->name,
            (int)header->value_len, header->value);
    }

    checked_fprintf(stream, "\r\n");

    char *body = message->self->body;
    size_t body_len = message->self->body_len;

    static char const pattern[] = "\r\n";
    static size_t const pattern_len = sizeof(pattern) - 1;

    size_t start = 0;
    size_t end = 0;
    while (start < body_len) {
        while (end + pattern_len <= body_len &&
               strncmp(body + end, pattern, pattern_len)) { ++end; }
        if (end + pattern_len > body_len) { end = body_len; }

        if (end - start == 1 && body[start] == '.') {
            checked_fprintf(stream, ".");
        }

        if (end < body_len) { end += pattern_len; }

        checked_fprintf(stream, "%.*s", (int)(end - start), body + start);

        start = end;
    }

    checked_fprintf(stream, "\r\n.\r\n");
}

void dispatch(struct session *session) {
    session->request_size = 0;

    free(session->request_buffer);
    session->request_buffer = NULL;

    session->request_offset = 0;

    FILE *stream = open_memstream(&session->request_buffer,
                                  &session->request_size);
    if (!stream) {
        die("`open_memstream(/* ... */)` failed: %s\n",
            strerror(errno));
    }

    struct session_message *message = TAILQ_FIRST(&session->messages);

    switch (session->state) {
    case SESSION_RESOLVING_DNS:
    case SESSION_CONNECTING:
        assert(false);
        goto exit;
    case SESSION_RECEIVING_GREETING:
        if (session->response_code == 554) {
            session->state = SESSION_SENDING_QUIT;
            logger_printf("server %s refused session\n",
                session->destination_host);
            checked_fprintf(stream, "QUIT\r\n");
            goto exit;
        }
        if (session->response_code == 220) {
            session->state = SESSION_SENDING_HELO;
            checked_fprintf(stream, "HELO %s\r\n", session->host);
            goto exit;
        }
        break;
    case SESSION_SENDING_HELO:
        if (session->response_code == 250) {
        start_message_transfer:
            if (!message) {
                session->state = SESSION_SENDING_QUIT;
                checked_fprintf(stream, "QUIT\r\n");
                goto exit;
            }

            session->state = SESSION_SENDING_MAIL_OR_RCPT;
            checked_fprintf(stream, "MAIL FROM:<%.*s>\r\n",
                (int)message->self->sender_len, message->self->sender);

            struct message_destination *destination = 
                TAILQ_FIRST(&message->self->destinations);
            while (destination->host_len !=
                       strlen(session->destination_host) ||
                   strncmp(destination->host,
                           session->destination_host,
                           destination->host_len))
            { destination = TAILQ_NEXT(destination, link); }
            session->message_recepient = 
                TAILQ_FIRST(&destination->recepients);

            message_start_loading_body(message->self);

            goto exit;
        }
        break;
    case SESSION_SENDING_MAIL_OR_RCPT:
        if (session->response_code == 250) {
            if (session->message_recepient) {
                checked_fprintf(stream, "RCPT TO:<%.*s@%s>\r\n",
                    (int)session->message_recepient->user_len,
                    session->message_recepient->user,
                    session->destination_host);
                session->message_recepient =
                    TAILQ_NEXT(session->message_recepient, link);
                goto exit;
            }
            if (message->self->state == MESSAGE_LOADING_BODY) {
                session->state = SESSION_LOADING_MESSAGE_BODY;
                goto exit;
            }
            message_body_loading_done:
            if (message->self->state == MESSAGE_LOADING_FAILED) {
                session->state = SESSION_SENDING_RSET;
                checked_fprintf(stream, "RSET\r\n");
                goto exit;
            }
            if (message->self->state == MESSAGE_BODY_LOADED) {
                session->state = SESSION_SENDING_DATA;
                checked_fprintf(stream, "DATA\r\n");
                goto exit;
            }
        }
        break;
    case SESSION_SENDING_DATA:
        if (session->response_code == 354) {
            session->state = SESSION_SENDING_DATA_PAYLOAD;
            write_data_payload(session, stream);
            goto exit;
        }
        break;
    case SESSION_LOADING_MESSAGE_BODY:
        if (message->self->state != MESSAGE_LOADING_BODY) {
            goto message_body_loading_done;
        }
        goto exit;
    case SESSION_SENDING_DATA_PAYLOAD:
        if (session->response_code == 250) {
            message_mark_as_sent(message->self, session->destination_host);
        dequeue_message:
            TAILQ_REMOVE(&session->messages, message, link);
            message_release(message->self);
            free(message);
            message = TAILQ_FIRST(&session->messages);
            goto start_message_transfer;
        }
        break;
    case SESSION_SENDING_RSET:
        if (session->response_code == 250) {
            goto dequeue_message;
        }
        break;
    case SESSION_SENDING_QUIT:
        if (session->response_code == 221) {
            session->state = SESSION_CLOSED;
            goto exit;
        }
        break;
    case SESSION_CLOSED:
        assert(false);
        goto exit;
    }

    session->state = SESSION_CLOSED;
    logger_printf("server %s sent unexpected response code: %d\n"
        "  session aborted\n",
        session->destination_host, session->response_code);

exit:
    if (fclose(stream)) {
        die("`fclose(/* in-memory stream */)` failed: %s\n", strerror(errno));
    }

    session->response_size = 0;
    session->response_line_len = 0;
    session->response_code = -1;
}

void session_initialize(struct session *session,
    char const *host, char const *destination_host)
{
    session->state = SESSION_RESOLVING_DNS;

    session->host = strdup(host);
    if (!session->host) {
        die("`strdup(\"%s\")` failed: %s\n", host, strerror(errno));
    }

    session->destination_host = strdup(destination_host);
    if (!session->destination_host) {
        die("`strdup(\"%s\")` failed: %s\n",
            destination_host, strerror(errno));
    }

    {
        int status = ares_library_init(ARES_LIB_INIT_ALL);
        if (status != ARES_SUCCESS) {
            die("`ares_library_init(ARES_LIB_INIT_ALL)` failed: %s\n",
                ares_strerror(status));
        }
    }

    {
        session->channel_initialized = false;
        int status = ares_init(&session->channel);
        if (status != ARES_SUCCESS) {
            session->state = SESSION_CLOSED;
            logger_printf("`ares_init((ares_channnel*)%p)` failed: %s\n"
                "  session to %s aborted\n",
                (void*)&session->channel, ares_strerror(status),
                session->destination_host);
        } else {
            session->channel_initialized = true;
        }
    }

    session->first_mx_reply = NULL;

    session->hostent = NULL;

    session->fd = -1;

    session->response_capacity = 0;
    session->response_size = 0;
    session->response_buffer = NULL;
    session->response_line_len = 0;
    session->response_code = -1;

    session->request_size = 0;
    session->request_buffer = NULL;
    session->request_offset = 0;

    TAILQ_INIT(&session->messages);

    logger_printf("initialized session to %s\n", session->destination_host);

    if (session->state == SESSION_RESOLVING_DNS) {
        ares_search(session->channel, session->destination_host,
            ns_c_in, ns_t_mx, mx_search_callback, session);
    }
}

void session_subscribe(struct session *session, struct fd_set *fd_set_) {
    struct session_message *message = TAILQ_FIRST(&session->messages);
    if (message) { message_subscribe(message->self, fd_set_); }

    switch (session->state) {
    case SESSION_RESOLVING_DNS:
        {
            struct timeval tv;
            ares_timeout(session->channel, NULL, &tv);
            int timeout = tv.tv_sec * 1000 + tv.tv_usec / 1000;
            fd_set readables; FD_ZERO(&readables);
            fd_set writables; FD_ZERO(&writables);
            int limit = ares_fds(session->channel, &readables, &writables);
            for (int fd = 0; fd < limit; ++fd) {
                short events = 0;
                if (FD_ISSET(fd, &readables)) { events |= POLLIN; }
                if (FD_ISSET(fd, &writables)) { events |= POLLOUT; }
                if (events) { fd_set_add(fd_set_, fd, events, timeout); }
            }
        }
        break;
    case SESSION_CONNECTING:
        fd_set_add(fd_set_, session->fd, POLLOUT, -1);
        break;
    case SESSION_RECEIVING_GREETING:
    case SESSION_SENDING_HELO:
    case SESSION_SENDING_MAIL_OR_RCPT:
    case SESSION_SENDING_DATA:
        goto exchange;
    case SESSION_LOADING_MESSAGE_BODY:
        break;
    case SESSION_SENDING_DATA_PAYLOAD:
    case SESSION_SENDING_RSET:
    case SESSION_SENDING_QUIT:
        exchange: {
            short events = 0;
            if (session->response_code == -1) { events |= POLLIN; }
            if (session->request_offset < session->request_size) {
                events |= POLLOUT;
            }
            fd_set_add(fd_set_, session->fd, events, -1);
        }
        break;
    case SESSION_CLOSED:
        break;
    }
}

void session_notify(struct session *session, struct fd_set const *fd_set_) {
    struct session_message *message = TAILQ_FIRST(&session->messages);
    if (message) { message_notify(message->self, fd_set_); }

    switch (session->state) {
    case SESSION_RESOLVING_DNS:
        {
            fd_set readables; FD_ZERO(&readables);
            fd_set writables; FD_ZERO(&writables);
            int limit = ares_fds(session->channel, &readables, &writables);
            for (int fd = 0; fd < limit; ++fd) {
                short events = fd_set_get_events(fd_set_, fd);
                if (!(events & (POLLIN | POLLHUP | POLLERR))) {
                    FD_CLR(fd, &readables);
                }
                if (!(events & (POLLOUT | POLLHUP | POLLERR))) {
                    FD_CLR(fd, &writables);
                }
            }
            ares_process(session->channel, &readables, &writables);
        }
        break;
    case SESSION_CONNECTING:
        {
            short events = fd_set_get_events(fd_set_, session->fd);
            if (!(events & (POLLOUT | POLLHUP | POLLERR))) { break; }

            int error;
            if (getsockopt(session->fd, SOL_SOCKET, SO_ERROR,
                           &error, &(socklen_t){sizeof(error)}))
            {
                die("`getsockopt(%d, SOL_SOCKET, SO_ERROR, /*...*/)` "
                    "failed: %s\n", session->fd, strerror(error));
            }
            if (error) {
                char addrstr[INET_ADDRSTRLEN > INET6_ADDRSTRLEN ?
                             INET_ADDRSTRLEN : INET6_ADDRSTRLEN];
                struct sockaddr *sa = (void*)&session->sockaddr;
                void *addr = (sa->sa_family == AF_INET6)
                    ? (void*)&((struct sockaddr_in6*)sa)->sin6_addr
                    : (void*)&((struct sockaddr_in*)sa)->sin_addr;
                inet_ntop(sa->sa_family, addr, addrstr, sizeof(addrstr));
                logger_printf("`connect(%d, /* %s */)` failed: %s\n",
                    session->fd, addrstr, strerror(errno));

                session->state = SESSION_RESOLVING_DNS;
                ++session->addr_index;
                try_addr(session);
                break;
            }

            session->state = SESSION_RECEIVING_GREETING;
        }
        break;
    case SESSION_RECEIVING_GREETING:
    case SESSION_SENDING_HELO:
    case SESSION_SENDING_MAIL_OR_RCPT:
    case SESSION_SENDING_DATA:
        goto exchange;
    case SESSION_LOADING_MESSAGE_BODY:
        dispatch(session);
        break;
    case SESSION_SENDING_DATA_PAYLOAD:
    case SESSION_SENDING_RSET:
    case SESSION_SENDING_QUIT:
        exchange: {
            short events = fd_set_get_events(fd_set_, session->fd);
            if (!(events & (POLLIN | POLLOUT | POLLHUP | POLLERR))) { break; }

            if (events & (POLLIN | POLLHUP | POLLERR) &&
                session->response_code == -1)
            { try_receive_response(session); }
            if (events & (POLLOUT | POLLHUP | POLLERR) && 
                session->request_offset < session->request_size)
            { try_send_request(session); }

            if (session->response_code == -1) { break; }

            dispatch(session);
        } 
        break;
    case SESSION_CLOSED:
        break;
    }
}

void session_enqueue_message(struct session *session,
    struct message* message)
{
    struct session_message *session_message = malloc(sizeof(*message));
    if (!session_message) {
        die("`malloc(%zu)` failed: %s\n",
            sizeof(*session_message), strerror(errno));
    }
    session_message->self = message_retain(message);
    TAILQ_INSERT_TAIL(&session->messages, session_message, link);
}

void session_finalize(struct session *session) {
    if (session->channel_initialized) {
        ares_cancel(session->channel);
    }

    while (true) {
        struct session_message *message = TAILQ_FIRST(&session->messages);
        if (!message) { break; }

        message_release(message->self);

        TAILQ_REMOVE(&session->messages, message, link);
        free(message);
    }

    free(session->request_buffer);

    free(session->response_buffer);

    if (session->fd != -1 && close(session->fd)) {
        die("`close(%d)` failed: %s\n", session->fd, strerror(errno));
    }

    ares_free_hostent(session->hostent);

    ares_free_data(session->first_mx_reply);

    if (session->channel_initialized) {
        ares_destroy(session->channel);
    }

    ares_library_cleanup();

    logger_printf("finalized session to %s\n", session->destination_host);
    free(session->destination_host);
    free(session->host);
}


/*! \file */
