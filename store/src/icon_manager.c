#include "icon_manager.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static const char *resolve_data_path(const char *filename)
{
    static char path[512];

    /*
     * When running from the source tree.
     */
    if (access("data", F_OK) == 0) {
        snprintf(path, sizeof(path), "data/%s", filename);
        return path;
    }

    /*
     * When installed system-wide.
     */
    snprintf(
        path,
        sizeof(path),
        "/usr/share/helwan/hel-store/%s",
        filename
    );

    return path;
}

void icon_manager_init(
    GtkWindow *window,
    const char *app_icon_path)
{
    GError *error = NULL;

    const char *path = app_icon_path;

    /*
     * Resolve the application icon when the caller
     * provides the normal source-tree path.
     */
    if (app_icon_path &&
        strcmp(app_icon_path, "data/hel-store.png") == 0) {

        path = resolve_data_path("hel-store.png");
    }

    if (!gtk_window_set_icon_from_file(
            window,
            path,
            &error)) {

        g_warning(
            "Failed to load application icon (%s): %s",
            path,
            error
                ? error->message
                : "Unknown error"
        );

        if (error)
            g_error_free(error);
    }
}

GdkPixbuf *icon_manager_load_about_logo(
    const char *logo_path)
{
    GError *error = NULL;

    const char *path = logo_path;

    if (logo_path &&
        strcmp(logo_path, "data/about-logo.png") == 0) {

        path = resolve_data_path("about-logo.png");
    }

    GdkPixbuf *pixbuf =
        gdk_pixbuf_new_from_file_at_scale(
            path,
            128,
            128,
            TRUE,
            &error
        );

    if (!pixbuf) {

        g_warning(
            "Failed to load about logo (%s): %s",
            path,
            error
                ? error->message
                : "Unknown error"
        );

        if (error)
            g_error_free(error);

        return NULL;
    }

    return pixbuf;
}

