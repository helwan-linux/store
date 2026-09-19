//ui_about.c
#include "ui.h"
#include "icon_manager.h"

void ui_about_dialog_show(GtkWindow *parent) {
    GtkWidget *dialog = gtk_about_dialog_new();
    gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);

    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), "Helwan Software Store");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), "1.0");
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog), "Professional Package Center for Helwan Linux");
    gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(dialog), "Copyright © 2026 Helwan Linux");
    
    GdkPixbuf *logo = icon_manager_load_about_logo("data/about-logo.png");
    if (logo) {
        gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(dialog), logo);
        g_object_unref(logo);
    }

    g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy), NULL);
    gtk_widget_show_all(dialog);
}
