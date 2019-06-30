#include <settings.h>

#include <unistd.h>

#include <string.h>

struct settings settings;

static char *get_env_var(char const *name, char *default_value) {
    size_t name_len = strlen(name);
    char **entry = environ;
    while (*entry) {
        char *equals = strchr(*entry, '=');
        size_t len = equals - *entry;
        if (len == name_len && !strncmp(*entry, name, len)) {
            char *value = equals + 1;
            return value;
        }
        ++entry;
    }
    return default_value;
}

void settings_initialize(int argc, char *argv[]) {
    settings.log_path = get_env_var("SMTP_CLIENT_LOG", "/dev/stderr");
    settings.maildir_path = get_env_var("SMTP_MAILDIR", "maildir");
    settings.host = get_env_var("SMTP_HOST", "localhost");
}

void settings_finalize() {

}

/*! \file */
