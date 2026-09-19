//icon_manager.c
#include "icon_manager.h"
#include <stdio.h>

void icon_manager_init(GtkWindow *window, const char *app_icon_path) {
    GError *error = NULL;
    if (!gtk_window_set_icon_from_file(window, app_icon_path, &error)) {
        g_warning("Failed to load application icon (%s): %s", app_icon_path, error->message);
        g_error_free(error);
    }
}

GdkPixbuf* icon_manager_load_about_logo(const char *logo_path) {
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(logo_path, 128, 128, TRUE, &error);
    if (!pixbuf) {
        g_warning("Failed to load about logo (%s): %s", logo_path, error->message);
        g_error_free(error);
        return NULL;
    }
    return pixbuf;
}
