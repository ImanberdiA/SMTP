#include <settings.h>
#include <logger.h>
#include <signal_handler.h>
#include <maildir.h>
#include <client.h>
#include <fd_set.h>
#include <die.h>
#include <ensure_directory.h>

#include <string.h>
#include <errno.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    signal_handler_initialize();

    settings_initialize(argc, argv);

    logger_initialize();

    struct client client;
    client_initialize(&client, settings.maildir_path, settings.host);

    struct fd_set fd_set;
    fd_set_initialize(&fd_set);

    while (!signal_handler.termination_requested) {
        signal_handler_subscribe(&fd_set);
        client_subscribe(&client, &fd_set);

        fd_set_poll(&fd_set);

        signal_handler_notify(&fd_set);
        client_notify(&client, &fd_set);

        fd_set_clear(&fd_set);
    }

    fd_set_finalize(&fd_set);

    client_finalize(&client);

    logger_finalize();

    settings_finalize();

    signal_handler_finalize();

    return EXIT_SUCCESS;
}

/*! \file */
