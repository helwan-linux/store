//icon_manager.h
#ifndef ICON_MANAGER_H
#define ICON_MANAGER_H

#include <gtk/gtk.h>

void icon_manager_init(GtkWindow *window, const char *app_icon_path);
GdkPixbuf* icon_manager_load_about_logo(const char *logo_path);

#endif
