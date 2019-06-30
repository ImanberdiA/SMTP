#include <logger.h>

#include <die.h>
#include <settings.h>

#include <pthread.h>
#include <sys/queue.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct logger_message {
    STAILQ_ENTRY(logger_message) link;
    size_t size;
    char data[];
};

struct logger {
    STAILQ_HEAD(, logger_message) messages;

    bool join_requested;

    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;
};

static struct logger logger;

static void *thread_body(void* arg) {
    FILE *file;
    if (!strcmp(settings.log_path, "/dev/stdout")) {
        file = stdout;
    } else if (!strcmp(settings.log_path, "/dev/stderr")) {
        file = stderr;
    } else {
        file = fopen(settings.log_path, "w");
        if (!file) {
            die("`fopen(\"%s\", \"w\")` failed: %s\n",
                settings.log_path, strerror(errno));
        }
    }

    STAILQ_HEAD(, logger_message) messages;
    STAILQ_INIT(&messages);

    bool join_requested = false;
    while (!join_requested) {
        {
            int error = pthread_mutex_lock(&logger.mutex);
            if (error) {
                die("`pthread_mutex_lock(/* ... */)` failed: %s\n",
                    strerror(error));
            }
        }

        while (STAILQ_EMPTY(&logger.messages) && !logger.join_requested) {
            int error = pthread_cond_wait(&logger.cond, &logger.mutex);
            if (error) {
                die("`pthread_cond_wait(/* ... */)` failed: %s\n",
                    strerror(error));
            }
        }

        STAILQ_CONCAT(&messages, &logger.messages);
        join_requested = join_requested || logger.join_requested;

        {
            int error = pthread_mutex_unlock(&logger.mutex);
            if (error) {
                die("`pthread_mutex_unlock(/* ... */)` failed: %s\n",
                    strerror(error));
            }
        }

        while (true) {
            struct logger_message *message = STAILQ_FIRST(&messages);
            if (!message) { break; }

            if (fwrite(message->data, 1, message->size, file) !=
                message->size)
            { fprintf(stderr, "`fwrite(/* to log file */)` failed\n"); }

            STAILQ_REMOVE_HEAD(&messages, link);
            free(message);
        }

        if (fflush(file)) {
            die("`fflush(/* log file */)` failed: %s\n", strerror(errno));
        }
    }

    if (file != stdout && file != stderr && fclose(file)) {
        die("`fclose(/* log file */)` failed: %s\n", strerror(errno));
    }

    return NULL;
}

void logger_initialize(char const *log_path) {
    STAILQ_INIT(&logger.messages);

    logger.join_requested = false;

    {
        int error = pthread_mutex_init(&logger.mutex, NULL);
        if (error) {
            die("`pthread_mutex_init(/* ... */)` failed: %s\n",
                strerror(error));
        }
    }

    {
        int error = pthread_cond_init(&logger.cond, NULL);
        if (error) {
            die("`pthread_cond_init(/* ... */)` failed: %s\n",
                strerror(error));
        }
    }

    {
        int error = pthread_create(&logger.thread, NULL, thread_body, NULL);
        if (error) {
            die("`pthread_create(/* ... */)` failed: %s\n",
                strerror(error));
        }
    }
}

void logger_vprintf(char const* format, va_list args) {
    va_list args2;
    va_copy(args2, args);
    int size = vsnprintf(NULL, 0, format, args2);
    if (size < 0) {
        die("`vsnprintf(NULL, 0, \"%s\", /*...*/)` failed\n", format);
    }
    va_end(args2);

    struct logger_message *message = malloc(sizeof(*message) + size + 1);
    if (!message) {
        die("`malloc(%zu)` failed: %s\n",
            sizeof(*message) + size + 1, strerror(errno));
    }

    message->size = size;
    if (vsprintf(message->data, format, args) != size) {
        die("`vsprintf(NULL, \"%s\", /*...*/)` failed\n", format);
    }

    {
        int error = pthread_mutex_lock(&logger.mutex);
        if (error) {
            die("`pthread_mutex_lock(/* ... */)` failed: %s\n",
                strerror(error));
        }
    }

    STAILQ_INSERT_TAIL(&logger.messages, message, link);
    {
        int error = pthread_cond_signal(&logger.cond);
        if (error) {
            die("`pthread_cond_signal(/* ... */)` failed: %s\n",
                strerror(error));
        }
    }

    {
        int error = pthread_mutex_unlock(&logger.mutex);
        if (error) {
            die("`pthread_mutex_unlock(/* ... */)` failed: %s\n",
                strerror(error));
        }
    }
}

void logger_printf(char const* format, ...) {
    va_list args;
    va_start(args, format);
    logger_vprintf(format, args);
    va_end(args);
}

void logger_finalize() {
    {
        int error = pthread_mutex_lock(&logger.mutex);
        if (error) {
            die("`pthread_mutex_lock(/* ... */)` failed: %s\n",
                strerror(error));
        }
    }

    logger.join_requested = true;
    {
        int error = pthread_cond_signal(&logger.cond);
        if (error) {
            die("`pthread_cond_signal(/* ... */)` failed: %s\n",
                strerror(error));
        }
    }

    {
        int error = pthread_mutex_unlock(&logger.mutex);
        if (error) {
            die("`pthread_mutex_unlock(/* ... */)` failed: %s\n",
                strerror(error));
        }
    }

    {
        int error = pthread_join(logger.thread, &(void*){NULL});
        if (error) {
            die("`pthread_join(/* ... */)` failed: %s\n", strerror(error));
        }
    }
}

/*! \file */
