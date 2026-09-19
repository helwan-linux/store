#ifndef UI_H
#define UI_H

#include <gtk/gtk.h>

void ui_window_show(int argc, char *argv[]);
void ui_window_present(void);
void ui_about_dialog_show(GtkWindow *parent);

void tray_init(void);
void tray_cleanup(void);

#endif
