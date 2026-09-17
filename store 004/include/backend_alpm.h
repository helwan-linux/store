//backend_alpm.h
#ifndef BACKEND_ALPM_H
#define BACKEND_ALPM_H

#include "core.h"

/*
 * Progress callback.
 *
 * percent:
 *   0 - 100 = actual pacman progress
 *
 * package_name:
 *   Package currently being processed when available.
 */
typedef void (*BackendProgressCallback)(
    const char *stage,
    const char *package_name,
    int percent,
    void *user_data
);

int backend_init(void);
void backend_cleanup(void);

GList* backend_get_all_packages(void);

int backend_install_package(const char *package_name);
int backend_remove_package(const char *package_name);
int backend_upgrade_system(void);
int backend_refresh_databases(void);

gboolean backend_package_has_update(const char *package_name);

/*
 * Set the callback used to report real pacman progress.
 */
void backend_set_progress_callback(
    BackendProgressCallback callback,
    void *user_data
);

#endif
