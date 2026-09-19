#include "backend_alpm.h"

#include <gtk/gtk.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>

static alpm_handle_t *handle = NULL;

/* Progress callback */
static BackendProgressCallback progress_callback = NULL;
static void *progress_user_data = NULL;

/* Output callback */
static BackendOutputCallback output_callback = NULL;
static void *output_user_data = NULL;

void backend_set_progress_callback(
    BackendProgressCallback callback,
    void *user_data)
{
    progress_callback = callback;
    progress_user_data = user_data;
}

void backend_set_output_callback(
    BackendOutputCallback callback,
    void *user_data)
{
    output_callback = callback;
    output_user_data = user_data;
}

static void backend_report_progress(
    const char *stage,
    const char *package_name,
    int percent)
{
    if (!progress_callback)
        return;

    if (percent < 0)
        percent = 0;

    if (percent > 100)
        percent = 100;

    progress_callback(
        stage ? stage : "",
        package_name,
        percent,
        progress_user_data
    );
}

static void backend_report_output(const char *output)
{
    if (!output_callback || !output || !*output)
        return;

    output_callback(
        output,
        output_user_data
    );
}

/* --------------------------------------------------------- */
/* ALPM initialization                                       */
/* --------------------------------------------------------- */

int backend_init(void)
{
    alpm_errno_t err;

    handle =
        alpm_initialize(
            "/",
            "/var/lib/pacman",
            &err
        );

    if (!handle) {
        fprintf(
            stderr,
            "ALPM Init Error: %s\n",
            alpm_strerror(err)
        );

        return 0;
    }

    FILE *f = fopen("/etc/pacman.conf", "r");

    if (f) {
        char line[512];

        while (fgets(line, sizeof(line), f)) {
            char *start = line;

            while (*start &&
                   isspace((unsigned char)*start))
                start++;

            if (*start == '[') {
                char *end = strchr(start, ']');

                if (end) {
                    *end = '\0';

                    char *repo_name = start + 1;

                    if (strcasecmp(repo_name, "options") != 0) {
                        alpm_register_syncdb(
                            handle,
                            repo_name,
                            ALPM_SIG_PACKAGE_OPTIONAL
                        );
                    }
                }
            }
        }

        fclose(f);
    } else {
        alpm_register_syncdb(
            handle,
            "core",
            ALPM_SIG_PACKAGE_OPTIONAL
        );

        alpm_register_syncdb(
            handle,
            "extra",
            ALPM_SIG_PACKAGE_OPTIONAL
        );

        alpm_register_syncdb(
            handle,
            "helwan",
            ALPM_SIG_PACKAGE_OPTIONAL
        );
    }

    return 1;
}

void backend_cleanup(void)
{
    if (handle) {
        alpm_release(handle);
        handle = NULL;
    }
}

/* --------------------------------------------------------- */
/* ANSI cleanup                                               */
/* --------------------------------------------------------- */

static void strip_ansi_sequences(char *text)
{
    if (!text)
        return;

    char *src = text;
    char *dst = text;

    while (*src) {

        if ((unsigned char)*src == 0x1b) {
            src++;

            if (*src == '[') {
                src++;

                while (*src &&
                       !(*src >= '@' && *src <= '~')) {
                    src++;
                }

                if (*src)
                    src++;

                continue;
            }

            continue;
        }

        *dst++ = *src++;
    }

    *dst = '\0';
}

/* --------------------------------------------------------- */
/* Clean one output line                                      */
/* --------------------------------------------------------- */

static char *clean_output_line(
    const char *line,
    gsize length)
{
    if (!line || length == 0)
        return NULL;

    char *text =
        g_malloc(length + 1);

    memcpy(
        text,
        line,
        length
    );

    text[length] = '\0';

    strip_ansi_sequences(text);

    char *start = text;

    while (*start &&
           isspace((unsigned char)*start)) {
        start++;
    }

    char *end =
        start + strlen(start);

    while (end > start &&
           isspace((unsigned char)end[-1])) {
        end--;
    }

    *end = '\0';

    if (!*start) {
        g_free(text);
        return NULL;
    }

    if (start != text)
        memmove(
            text,
            start,
            strlen(start) + 1
        );

    return text;
}

/* --------------------------------------------------------- */
/* Progress detection                                         */
/* --------------------------------------------------------- */

static int detect_progress(
    const char *text,
    int current_percent)
{
    if (!text || !*text)
        return current_percent;

    int percent = current_percent;

    if (strstr(text, "resolving dependencies") != NULL)
        percent = 10;

    else if (strstr(
                 text,
                 "looking for conflicting packages"
             ) != NULL)
        percent = 20;

    else if (strstr(text, "Retrieving packages") != NULL ||
             strstr(text, "Downloading") != NULL ||
             strstr(text, "downloading") != NULL)
        percent = 40;

    else if (strstr(text, "checking keyring") != NULL)
        percent = 55;

    else if (strstr(
                 text,
                 "checking package integrity"
             ) != NULL)
        percent = 60;

    else if (strstr(
                 text,
                 "loading package files"
             ) != NULL)
        percent = 70;

    else if (strstr(
                 text,
                 "checking for file conflicts"
             ) != NULL)
        percent = 80;

    else if (strstr(text, "installing") != NULL ||
             strstr(text, "removing") != NULL)
        percent = 90;

    /*
     * Detect percentages such as:
     *
     * 25%
     * 75%
     * 100%
     */
    const char *p = text;

    while (*p) {

        if (isdigit((unsigned char)*p)) {

            char *endptr = NULL;

            long value =
                strtol(p, &endptr, 10);

            if (endptr &&
                *endptr == '%' &&
                value >= 0 &&
                value <= 100) {

                percent = (int)value;
            }

            p = endptr ? endptr : p + 1;

        } else {
            p++;
        }
    }

    return percent;
}

/* --------------------------------------------------------- */
/* Process complete lines from a stream                       */
/* --------------------------------------------------------- */

static void process_output_buffer(
    GString *buffer,
    const char *stage,
    const char *package_name,
    int *current_percent,
    char **last_message)
{
    if (!buffer || buffer->len == 0)
        return;

    while (TRUE) {

        gssize separator = -1;

        for (gsize i = 0;
             i < buffer->len;
             i++) {

            if (buffer->str[i] == '\n' ||
                buffer->str[i] == '\r') {

                separator = (gssize)i;
                break;
            }
        }

        if (separator < 0)
            break;

        gsize line_length =
            (gsize)separator;

        char *line =
            clean_output_line(
                buffer->str,
                line_length
            );

        gsize remove_length =
            (gsize)separator + 1;

        /*
         * Handle CRLF.
         */
        if (remove_length < buffer->len &&
            buffer->str[remove_length] == '\n' &&
            buffer->str[separator] == '\r') {

            remove_length++;
        }

        g_string_erase(
            buffer,
            0,
            remove_length
        );

        if (!line)
            continue;

        *current_percent =
            detect_progress(
                line,
                *current_percent
            );

        /*
         * Save the latest REAL pacman message.
         */
        if (last_message)
            g_free(*last_message);

        if (last_message)
            *last_message =
                g_strdup(line);

        /*
         * Send the real output to the UI.
         */
        backend_report_output(line);

        backend_report_progress(
            line,
            package_name,
            *current_percent
        );

        g_free(line);
    }

    /*
     * Keep the buffer from growing forever.
     */
    if (buffer->len > 8192) {
        g_string_erase(
            buffer,
            0,
            buffer->len - 4096
        );
    }
}

/* --------------------------------------------------------- */
/* Run pacman                                                  */
/* --------------------------------------------------------- */

static int backend_run_pacman(
    char *const argv[],
    const char *stage,
    const char *package_name)
{
    GError *error = NULL;

    GPid child_pid = 0;

    gint stdout_fd = -1;
    gint stderr_fd = -1;

    gboolean stdout_open = TRUE;
    gboolean stderr_open = TRUE;

    int status = 0;

    int current_percent = 5;

    GString *stdout_buffer =
        g_string_new(NULL);

    GString *stderr_buffer =
        g_string_new(NULL);

    char *last_message = NULL;

    if (!stdout_buffer || !stderr_buffer) {
        if (stdout_buffer)
            g_string_free(stdout_buffer, TRUE);

        if (stderr_buffer)
            g_string_free(stderr_buffer, TRUE);

        return 0;
    }

    /*
     * Tell the UI that the operation has started.
     */
    backend_report_progress(
        stage,
        package_name,
        current_percent
    );

    backend_report_output(stage);

    /*
     * IMPORTANT:
     * Do not use shell execution.
     * Execute pkexec directly.
     */
    gboolean spawned =
        g_spawn_async_with_pipes(
            NULL,
            (gchar **)argv,
            NULL,
            G_SPAWN_DO_NOT_REAP_CHILD,
            NULL,
            NULL,
            &child_pid,
            NULL,
            &stdout_fd,
            &stderr_fd,
            &error
        );

    if (!spawned) {

        const char *message =
            error
                ? error->message
                : "Failed to start pacman.";

        fprintf(
            stderr,
            "Failed to execute pacman: %s\n",
            message
        );

        backend_report_output(message);

        backend_report_progress(
            message,
            package_name,
            0
        );

        if (error)
            g_error_free(error);

        g_string_free(
            stdout_buffer,
            TRUE
        );

        g_string_free(
            stderr_buffer,
            TRUE
        );

        return 0;
    }

    /*
     * Non-blocking output.
     */
    int flags;

    flags = fcntl(
        stdout_fd,
        F_GETFL,
        0
    );

    if (flags >= 0) {
        fcntl(
            stdout_fd,
            F_SETFL,
            flags | O_NONBLOCK
        );
    }

    flags = fcntl(
        stderr_fd,
        F_GETFL,
        0
    );

    if (flags >= 0) {
        fcntl(
            stderr_fd,
            F_SETFL,
            flags | O_NONBLOCK
        );
    }

    char buffer[4096];

    /*
     * Read stdout and stderr until both streams close.
     */
    while (stdout_open || stderr_open) {

        struct pollfd poll_fds[2];

        int count = 0;

        int stdout_index = -1;
        int stderr_index = -1;

        if (stdout_open) {

            stdout_index = count;

            poll_fds[count].fd =
                stdout_fd;

            poll_fds[count].events =
                POLLIN |
                POLLHUP |
                POLLERR;

            poll_fds[count].revents = 0;

            count++;
        }

        if (stderr_open) {

            stderr_index = count;

            poll_fds[count].fd =
                stderr_fd;

            poll_fds[count].events =
                POLLIN |
                POLLHUP |
                POLLERR;

            poll_fds[count].revents = 0;

            count++;
        }

        int poll_result =
            poll(
                poll_fds,
                count,
                200
            );

        if (poll_result < 0) {

            if (errno == EINTR)
                continue;

            break;
        }

        /*
         * stdout
         */
        if (stdout_open &&
            stdout_index >= 0 &&
            (poll_fds[stdout_index].revents &
             (POLLIN |
              POLLHUP |
              POLLERR))) {

            ssize_t bytes =
                read(
                    stdout_fd,
                    buffer,
                    sizeof(buffer) - 1
                );

            if (bytes > 0) {

                buffer[bytes] = '\0';

                g_string_append_len(
                    stdout_buffer,
                    buffer,
                    bytes
                );

                process_output_buffer(
                    stdout_buffer,
                    stage,
                    package_name,
                    &current_percent,
                    &last_message
                );

            } else if (bytes == 0) {

                /*
                 * Process any final unterminated line.
                 */
                if (stdout_buffer->len > 0) {

                    char *line =
                        clean_output_line(
                            stdout_buffer->str,
                            stdout_buffer->len
                        );

                    if (line) {

                        current_percent =
                            detect_progress(
                                line,
                                current_percent
                            );

                        g_free(last_message);

                        last_message =
                            g_strdup(line);

                        backend_report_output(
                            line
                        );

                        backend_report_progress(
                            line,
                            package_name,
                            current_percent
                        );

                        g_free(line);
                    }

                    g_string_set_size(
                        stdout_buffer,
                        0
                    );
                }

                close(stdout_fd);

                stdout_fd = -1;
                stdout_open = FALSE;

            } else if (errno != EAGAIN &&
                       errno != EWOULDBLOCK) {

                close(stdout_fd);

                stdout_fd = -1;
                stdout_open = FALSE;
            }
        }

        /*
         * stderr
         */
        if (stderr_open &&
            stderr_index >= 0 &&
            (poll_fds[stderr_index].revents &
             (POLLIN |
              POLLHUP |
              POLLERR))) {

            ssize_t bytes =
                read(
                    stderr_fd,
                    buffer,
                    sizeof(buffer) - 1
                );

            if (bytes > 0) {

                buffer[bytes] = '\0';

                g_string_append_len(
                    stderr_buffer,
                    buffer,
                    bytes
                );

                process_output_buffer(
                    stderr_buffer,
                    stage,
                    package_name,
                    &current_percent,
                    &last_message
                );

            } else if (bytes == 0) {

                /*
                 * Process final stderr line.
                 */
                if (stderr_buffer->len > 0) {

                    char *line =
                        clean_output_line(
                            stderr_buffer->str,
                            stderr_buffer->len
                        );

                    if (line) {

                        current_percent =
                            detect_progress(
                                line,
                                current_percent
                            );

                        g_free(last_message);

                        last_message =
                            g_strdup(line);

                        backend_report_output(
                            line
                        );

                        backend_report_progress(
                            line,
                            package_name,
                            current_percent
                        );

                        g_free(line);
                    }

                    g_string_set_size(
                        stderr_buffer,
                        0
                    );
                }

                close(stderr_fd);

                stderr_fd = -1;
                stderr_open = FALSE;

            } else if (errno != EAGAIN &&
                       errno != EWOULDBLOCK) {

                close(stderr_fd);

                stderr_fd = -1;
                stderr_open = FALSE;
            }
        }
    }

    if (stdout_open && stdout_fd >= 0)
        close(stdout_fd);

    if (stderr_open && stderr_fd >= 0)
        close(stderr_fd);

    /*
     * Wait for the actual process.
     */
    if (waitpid(
            child_pid,
            &status,
            0
        ) < 0) {

        const char *message =
            "Failed to wait for pacman.";

        fprintf(
            stderr,
            "%s\n",
            message
        );

        backend_report_output(message);

        backend_report_progress(
            message,
            package_name,
            0
        );

        g_spawn_close_pid(child_pid);

        g_free(last_message);

        g_string_free(
            stdout_buffer,
            TRUE
        );

        g_string_free(
            stderr_buffer,
            TRUE
        );

        return 0;
    }

    g_spawn_close_pid(child_pid);

    /*
     * Check the real exit status.
     */
    gboolean success =
        g_spawn_check_wait_status(
            status,
            &error
        );

    if (!success) {

        /*
         * DO NOT replace the real pacman message
         * with "Child process exited with code 1".
         *
         * That message only describes the process exit.
         * The useful error is normally in stderr.
         */
        if (last_message &&
            *last_message) {

            fprintf(
                stderr,
                "Pacman error: %s\n",
                last_message
            );

            backend_report_output(
                last_message
            );

            backend_report_progress(
                last_message,
                package_name,
                0
            );

        } else {

            const char *message =
                error
                    ? error->message
                    : "Pacman operation failed.";

            fprintf(
                stderr,
                "Pacman operation failed: %s\n",
                message
            );

            backend_report_output(message);

            backend_report_progress(
                message,
                package_name,
                0
            );
        }

        if (error)
            g_error_free(error);

        g_free(last_message);

        g_string_free(
            stdout_buffer,
            TRUE
        );

        g_string_free(
            stderr_buffer,
            TRUE
        );

        return 0;
    }

    /*
     * Successful operation.
     */
    backend_report_progress(
        "Operation completed successfully.",
        package_name,
        100
    );

    backend_report_output(
        "Operation completed successfully."
    );

    g_free(last_message);

    g_string_free(
        stdout_buffer,
        TRUE
    );

    g_string_free(
        stderr_buffer,
        TRUE
    );

    /*
     * IMPORTANT:
     * 1 = success
     * 0 = failure
     */
    return 1;
}

/* --------------------------------------------------------- */
/* Install                                                     */
/* --------------------------------------------------------- */

int backend_install_package(
    const char *package_name)
{
    if (!package_name || !*package_name)
        return 0;

    char *argv[] = {
        "/usr/bin/pkexec",
        "/usr/bin/pacman",
        "-S",
        "--needed",
        "--noconfirm",
        "--color=never",
        (char *)package_name,
        NULL
    };

    return backend_run_pacman(
        argv,
        "Installing",
        package_name
    );
}

/* --------------------------------------------------------- */
/* Remove                                                      */
/* --------------------------------------------------------- */

int backend_remove_package(
    const char *package_name)
{
    if (!package_name || !*package_name)
        return 0;

    char *argv[] = {
        "/usr/bin/pkexec",
        "/usr/bin/pacman",
        "-Rs",
        "--noconfirm",
        "--color=never",
        (char *)package_name,
        NULL
    };

    return backend_run_pacman(
        argv,
        "Removing",
        package_name
    );
}

/* --------------------------------------------------------- */
/* Upgrade                                                      */
/* --------------------------------------------------------- */

int backend_upgrade_system(void)
{
    char *argv[] = {
        "/usr/bin/pkexec",
        "/usr/bin/pacman",
        "-Syu",
        "--noconfirm",
        "--color=never",
        NULL
    };

    return backend_run_pacman(
        argv,
        "Upgrading system",
        NULL
    );
}

/* --------------------------------------------------------- */
/* Refresh                                                      */
/* --------------------------------------------------------- */

int backend_refresh_databases(void)
{
    char *argv[] = {
        "/usr/bin/pkexec",
        "/usr/bin/pacman",
        "-Sy",
        "--noconfirm",
        "--color=never",
        NULL
    };

    return backend_run_pacman(
        argv,
        "Refreshing package databases",
        NULL
    );
}

/* --------------------------------------------------------- */
/* Check package update                                        */
/* --------------------------------------------------------- */

gboolean backend_package_has_update(
    const char *package_name)
{
    if (!handle || !package_name)
        return FALSE;

    alpm_db_t *local_db =
        alpm_get_localdb(handle);

    if (!local_db)
        return FALSE;

    alpm_pkg_t *local_pkg =
        alpm_db_get_pkg(
            local_db,
            package_name
        );

    if (!local_pkg)
        return FALSE;

    const char *local_version =
        alpm_pkg_get_version(local_pkg);

    alpm_list_t *syncdbs =
        alpm_get_syncdbs(handle);

    for (alpm_list_t *i = syncdbs;
         i;
         i = i->next) {

        alpm_db_t *db = i->data;

        alpm_pkg_t *sync_pkg =
            alpm_db_get_pkg(
                db,
                package_name
            );

        if (!sync_pkg)
            continue;

        const char *sync_version =
            alpm_pkg_get_version(sync_pkg);

        if (alpm_pkg_vercmp(
                sync_version,
                local_version
            ) > 0) {

            return TRUE;
        }
    }

    return FALSE;
}

/* --------------------------------------------------------- */
/* Get all packages                                            */
/* --------------------------------------------------------- */

GList* backend_get_all_packages(void)
{
    GList *list = NULL;

    if (!handle)
        return NULL;

    alpm_list_t *repos =
        alpm_get_syncdbs(handle);

    alpm_db_t *local_db =
        alpm_get_localdb(handle);

    for (alpm_list_t *i = repos;
         i;
         i = i->next) {

        alpm_db_t *db = i->data;

        const char *dbname =
            alpm_db_get_name(db);

        alpm_list_t *pkgs =
            alpm_db_get_pkgcache(db);

        for (alpm_list_t *j = pkgs;
             j;
             j = j->next) {

            alpm_pkg_t *pkg = j->data;

            const char *pkgname =
                alpm_pkg_get_name(pkg);

            PackageInfo *info =
                malloc(sizeof(PackageInfo));

            if (!info)
                continue;

            info->name =
                g_strdup(pkgname);

            info->version =
                g_strdup(
                    alpm_pkg_get_version(pkg)
                );

            info->desc =
                g_strdup(
                    alpm_pkg_get_desc(pkg)
                );

            info->repo =
                g_strdup(dbname);

            info->installed =
                (alpm_db_get_pkg(
                    local_db,
                    pkgname
                ) != NULL);

            list =
                g_list_append(
                    list,
                    info
                );
        }
    }

    return list;
}
