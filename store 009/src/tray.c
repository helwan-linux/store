#include "ui.h"

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GtkStatusIcon *tray_icon = NULL;
static guint update_check_timer = 0;


/* =========================================================
 * Check for updates
 * ========================================================= */

static gboolean check_for_updates(void)
{
    FILE *pipe = NULL;
    char buffer[1024];

    pipe = popen(
        "pacman -Qu 2>/dev/null",
        "r"
    );

    if (!pipe)
        return FALSE;

    while (fgets(
        buffer,
        sizeof(buffer),
        pipe
    )) {

        if (buffer[0] != '\0') {
            pclose(pipe);
            return TRUE;
        }
    }

    pclose(pipe);

    return FALSE;
}


/* =========================================================
 * Notification
 * ========================================================= */

static void show_update_notification(void)
{
    if (!g_find_program_in_path("notify-send"))
        return;

    gchar *argv[] = {
        "notify-send",
        "--app-name=Helwan Store",
        "--icon=hel-store",
        "Helwan Store",
        "System updates are available.",
        NULL
    };

    GError *error = NULL;

    g_spawn_async(
        NULL,
        argv,
        NULL,
        G_SPAWN_SEARCH_PATH,
        NULL,
        NULL,
        NULL,
        &error
    );

    if (error) {

        g_warning(
            "Could not send update notification: %s",
            error->message
        );

        g_error_free(error);
    }
}


/* =========================================================
 * Tray status
 * ========================================================= */

static void set_tray_status(
    gboolean updates_available)
{
    if (!tray_icon)
        return;

    if (updates_available) {

        gtk_status_icon_set_tooltip_text(
            tray_icon,
            "Helwan Store - System updates available"
        );

    } else {

        gtk_status_icon_set_tooltip_text(
            tray_icon,
            "Helwan Software Store"
        );
    }
}


/* =========================================================
 * Update check
 * ========================================================= */

static gboolean update_check_callback(
    gpointer user_data)
{
    gboolean updates =
        check_for_updates();

    set_tray_status(
        updates
    );

    if (updates) {

        show_update_notification();
    }

    return G_SOURCE_CONTINUE;
}


/* =========================================================
 * Open store
 * ========================================================= */

static void on_tray_activate(
    GtkStatusIcon *status_icon,
    gpointer user_data)
{
    ui_window_present();
}


/* =========================================================
 * Tray menu
 * ========================================================= */

static void on_open_store(
    GtkMenuItem *item,
    gpointer user_data)
{
    ui_window_present();
}


static void on_check_updates(
    GtkMenuItem *item,
    gpointer user_data)
{
    gboolean updates =
        check_for_updates();

    set_tray_status(
        updates
    );

    if (updates) {

        show_update_notification();

    } else {

        ui_window_present();
    }
}


static void on_exit_store(
    GtkMenuItem *item,
    gpointer user_data)
{
    tray_cleanup();

    gtk_main_quit();
}


/* =========================================================
 * Popup menu
 * ========================================================= */

static void on_tray_popup(
    GtkStatusIcon *status_icon,
    guint button,
    guint activate_time,
    gpointer user_data)
{
    GtkWidget *menu =
        gtk_menu_new();

    GtkWidget *open_item =
        gtk_menu_item_new_with_label(
            "Open Helwan Store"
        );

    GtkWidget *check_item =
        gtk_menu_item_new_with_label(
            "Check for Updates"
        );

    GtkWidget *separator =
        gtk_separator_menu_item_new();

    GtkWidget *exit_item =
        gtk_menu_item_new_with_label(
            "Exit"
        );

    g_signal_connect(
        open_item,
        "activate",
        G_CALLBACK(on_open_store),
        NULL
    );

    g_signal_connect(
        check_item,
        "activate",
        G_CALLBACK(on_check_updates),
        NULL
    );

    g_signal_connect(
        exit_item,
        "activate",
        G_CALLBACK(on_exit_store),
        NULL
    );

    gtk_menu_shell_append(
        GTK_MENU_SHELL(menu),
        open_item
    );

    gtk_menu_shell_append(
        GTK_MENU_SHELL(menu),
        check_item
    );

    gtk_menu_shell_append(
        GTK_MENU_SHELL(menu),
        separator
    );

    gtk_menu_shell_append(
        GTK_MENU_SHELL(menu),
        exit_item
    );

    gtk_widget_show_all(
        menu
    );

    gtk_menu_popup_at_pointer(
        GTK_MENU(menu),
        NULL
    );
}


/* =========================================================
 * Initialize tray
 * ========================================================= */

void tray_init(void)
{
    if (tray_icon)
        return;

    tray_icon =
        gtk_status_icon_new_from_file(
            "data/hel-store.png"
        );

    if (!tray_icon) {

        g_warning(
            "Failed to create Helwan Store tray icon."
        );

        return;
    }

    gtk_status_icon_set_visible(
        tray_icon,
        TRUE
    );

    gtk_status_icon_set_tooltip_text(
        tray_icon,
        "Helwan Software Store"
    );

    g_signal_connect(
        tray_icon,
        "activate",
        G_CALLBACK(on_tray_activate),
        NULL
    );

    g_signal_connect(
        tray_icon,
        "popup-menu",
        G_CALLBACK(on_tray_popup),
        NULL
    );

    /*
     * First update check after startup.
     */
    update_check_callback(NULL);

    /*
     * Check every 30 minutes.
     */
    update_check_timer =
        g_timeout_add_seconds(
            1800,
            update_check_callback,
            NULL
        );
}


/* =========================================================
 * Cleanup
 * ========================================================= */

void tray_cleanup(void)
{
    if (update_check_timer != 0) {

        g_source_remove(
            update_check_timer
        );

        update_check_timer = 0;
    }

    if (tray_icon) {

        gtk_status_icon_set_visible(
            tray_icon,
            FALSE
        );

        g_object_unref(
            tray_icon
        );

        tray_icon = NULL;
    }
}
