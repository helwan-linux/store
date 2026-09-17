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


/* =========================================================
 * Progress callback
 * ========================================================= */

static BackendProgressCallback progress_callback = NULL;
static void *progress_user_data = NULL;

void backend_set_progress_callback(
    BackendProgressCallback callback,
    void *user_data)
{
    progress_callback = callback;
    progress_user_data = user_data;
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
        stage,
        package_name,
        percent,
        progress_user_data
    );
}


/* =========================================================
 * ALPM initialization
 * ========================================================= */

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

    /* قراءة المستودعات ديناميكياً من ملف pacman.conf لضمان ظهور أي مستودع جديد تلقائياً */
    FILE *f = fopen("/etc/pacman.conf", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            char *start = line;
            while (*start && isspace((unsigned char)*start)) start++;
            
            if (*start == '[') {
                char *end = strchr(start, ']');
                if (end) {
                    *end = '\0';
                    char *repo_name = start + 1;
                    
                    if (strcasecmp(repo_name, "options") != 0) {
                        alpm_register_syncdb(handle, repo_name, ALPM_SIG_PACKAGE_OPTIONAL);
                    }
                }
            }
        }
        fclose(f);
    } else {
        /* كاحتياط في حال تعذر قراءة الملف */
        alpm_register_syncdb(handle, "core", ALPM_SIG_PACKAGE_OPTIONAL);
        alpm_register_syncdb(handle, "extra", ALPM_SIG_PACKAGE_OPTIONAL);
        alpm_register_syncdb(handle, "helwan", ALPM_SIG_PACKAGE_OPTIONAL);
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


/* =========================================================
 * Extract real percentage from pacman output
 * ========================================================= */

static void parse_progress_output(
    const char *stage,
    const char *package_name,
    GString *scan_buffer)
{
    if (!scan_buffer || scan_buffer->len == 0)
        return;

    int latest_percent = -1;

    for (gsize i = 0;
         i < scan_buffer->len;
         i++) {

        if (scan_buffer->str[i] != '%')
            continue;

        gssize end =
            (gssize)i - 1;

        gssize start = end;

        while (start >= 0 &&
               isdigit(
                   (unsigned char)
                   scan_buffer->str[start])) {

            start--;
        }

        start++;

        if (start > end)
            continue;

        gsize number_length =
            (gsize)(end - start + 1);

        if (number_length > 3)
            continue;

        char number[4];

        memcpy(
            number,
            scan_buffer->str + start,
            number_length
        );

        number[number_length] = '\0';

        int percent =
            atoi(number);

        if (percent >= 0 &&
            percent <= 100) {

            latest_percent = percent;
        }
    }

    if (latest_percent >= 0) {

        backend_report_progress(
            stage,
            package_name,
            latest_percent
        );
    }


    /*
     * Keep only the tail of the buffer.
     */
    if (scan_buffer->len > 128) {

        gsize keep = 64;

        g_string_erase(
            scan_buffer,
            0,
            scan_buffer->len - keep
        );
    }
}


/* =========================================================
 * Read pacman output while it is running
 * ========================================================= */

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

    GString *scan_buffer =
        g_string_new(NULL);

    if (!scan_buffer)
        return 0;


    backend_report_progress(
        stage,
        package_name,
        0
    );


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

        fprintf(
            stderr,
            "Failed to execute pacman: %s\n",
            error
                ? error->message
                : "Unknown error"
        );

        if (error)
            g_error_free(error);

        g_string_free(
            scan_buffer,
            TRUE
        );

        return 0;
    }


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


    while (stdout_open ||
           stderr_open) {

        struct pollfd poll_fds[2];

        int count = 0;

        int stdout_index = -1;
        int stderr_index = -1;


        if (stdout_open) {

            stdout_index = count;

            poll_fds[count].fd =
                stdout_fd;

            poll_fds[count].events =
                POLLIN | POLLHUP | POLLERR;

            poll_fds[count].revents = 0;

            count++;
        }


        if (stderr_open) {

            stderr_index = count;

            poll_fds[count].fd =
                stderr_fd;

            poll_fds[count].events =
                POLLIN | POLLHUP | POLLERR;

            poll_fds[count].revents = 0;

            count++;
        }


        int poll_result =
            poll(
                poll_fds,
                count,
                250
            );

        if (poll_result < 0) {

            if (errno == EINTR)
                continue;

            break;
        }


        if (stdout_open &&
            stdout_index >= 0 &&
            (poll_fds[stdout_index].revents &
             (POLLIN | POLLHUP | POLLERR))) {

            ssize_t bytes =
                read(
                    stdout_fd,
                    buffer,
                    sizeof(buffer) - 1
                );

            if (bytes > 0) {

                buffer[bytes] = '\0';

                g_string_append_len(
                    scan_buffer,
                    buffer,
                    bytes
                );

                parse_progress_output(
                    stage,
                    package_name,
                    scan_buffer
                );

            } else if (bytes == 0) {

                close(stdout_fd);
                stdout_fd = -1;
                stdout_open = FALSE;

            } else if (
                errno != EAGAIN &&
                errno != EWOULDBLOCK) {

                close(stdout_fd);
                stdout_fd = -1;
                stdout_open = FALSE;
            }
        }


        if (stderr_open &&
            stderr_index >= 0 &&
            (poll_fds[stderr_index].revents &
             (POLLIN | POLLHUP | POLLERR))) {

            ssize_t bytes =
                read(
                    stderr_fd,
                    buffer,
                    sizeof(buffer) - 1
                );

            if (bytes > 0) {

                buffer[bytes] = '\0';

                g_string_append_len(
                    scan_buffer,
                    buffer,
                    bytes
                );

                parse_progress_output(
                    stage,
                    package_name,
                    scan_buffer
                );

            } else if (bytes == 0) {

                close(stderr_fd);
                stderr_fd = -1;
                stderr_open = FALSE;

            } else if (
                errno != EAGAIN &&
                errno != EWOULDBLOCK) {

                close(stderr_fd);
                stderr_fd = -1;
                stderr_open = FALSE;
            }
        }
    }


    if (stdout_open &&
        stdout_fd >= 0) {

        close(stdout_fd);
    }

    if (stderr_open &&
        stderr_fd >= 0) {

        close(stderr_fd);
    }


    if (waitpid(
            child_pid,
            &status,
            0
        ) < 0) {

        g_spawn_close_pid(child_pid);

        g_string_free(
            scan_buffer,
            TRUE
        );

        return 0;
    }


    g_spawn_close_pid(child_pid);


    gboolean success =
        g_spawn_check_wait_status(
            status,
            &error
        );


    if (!success) {

        fprintf(
            stderr,
            "Pacman operation failed: %s\n",
            error
                ? error->message
                : "Unknown error"
        );

        if (error)
            g_error_free(error);

        g_string_free(
            scan_buffer,
            TRUE
        );

        return 0;
    }


    backend_report_progress(
        stage,
        package_name,
        100
    );


    g_string_free(
        scan_buffer,
        TRUE
    );

    return 1;
}


/* =========================================================
 * Install
 * ========================================================= */

int backend_install_package(
    const char *package_name)
{
    if (!package_name ||
        !*package_name) {

        return 0;
    }

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


/* =========================================================
 * Remove
 * ========================================================= */

int backend_remove_package(
    const char *package_name)
{
    if (!package_name ||
        !*package_name) {

        return 0;
    }

    char *argv[] = {
        "/usr/bin/pkexec",
        "/usr/bin/pacman",
        "-R",
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


/* =========================================================
 * Upgrade system
 * ========================================================= */

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


/* =========================================================
 * Refresh databases
 * ========================================================= */

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


/* =========================================================
 * Check package update
 * ========================================================= */

gboolean backend_package_has_update(
    const char *package_name)
{
    if (!handle ||
        !package_name) {

        return FALSE;
    }

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
        alpm_pkg_get_version(
            local_pkg
        );

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
            alpm_pkg_get_version(
                sync_pkg
            );

        if (alpm_pkg_vercmp(
                sync_version,
                local_version
            ) > 0) {

            return TRUE;
        }
    }

    return FALSE;
}


/* =========================================================
 * Get all packages
 * ========================================================= */

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
                (
                    alpm_db_get_pkg(
                        local_db,
                        pkgname
                    ) != NULL
                );

            list =
                g_list_append(
                    list,
                    info
                );
        }
    }

    return list;
}
