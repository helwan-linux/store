#include "ui.h"
#include "backend_alpm.h"

#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AppIndicator *tray_indicator = NULL;
static guint update_check_timer = 0;
static int last_update_count = -1;


/* =========================================================
 * Notification
 * ========================================================= */

static void show_update_notification(int update_count)
{
    if (!g_find_program_in_path("notify-send"))
        return;

    gchar *message = NULL;

    if (update_count == 1) {
        message = g_strdup(
            "1 system update is available."
        );
    } else {
        message = g_strdup_printf(
            "%d system updates are available.",
            update_count
        );
    }

    gchar *argv[] = {
        "notify-send",
        "--app-name=Helwan Store",
        "--icon=hel-store",
        "Helwan Store",
        message,
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

    g_free(message);
}


/* =========================================================
 * Open store
 * ========================================================= */

static void on_open_store(
    GtkMenuItem *item,
    gpointer user_data)
{
    (void)item;
    (void)user_data;

    ui_window_present();
}


/* =========================================================
 * Check updates
 * ========================================================= */

static gboolean update_check_callback(
    gpointer user_data);


/* =========================================================
 * Check updates menu item
 * ========================================================= */

static void on_check_updates(
    GtkMenuItem *item,
    gpointer user_data)
{
    (void)item;
    (void)user_data;

    update_check_callback(NULL);

    ui_window_present();
}


/* =========================================================
 * Exit
 * ========================================================= */

static void on_exit_store(
    GtkMenuItem *item,
    gpointer user_data)
{
    (void)item;
    (void)user_data;

    tray_cleanup();

    gtk_main_quit();
}


/* =========================================================
 * Build tray menu
 * ========================================================= */

static GtkWidget *create_tray_menu(void)
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

    gtk_widget_show_all(menu);

    return menu;
}


/* =========================================================
 * Normal icon
 * ========================================================= */

static void set_normal_tray_icon(void)
{
    if (!tray_indicator)
        return;

    app_indicator_set_icon_full(
        tray_indicator,
        "hel-store",
        "Helwan Software Store"
    );

    app_indicator_set_title(
        tray_indicator,
        "Helwan Store"
    );
}


/* =========================================================
 * Update icon
 * ========================================================= */

static void set_update_tray_icon(int update_count)
{
    if (!tray_indicator)
        return;

    GtkIconTheme *theme =
        gtk_icon_theme_get_default();

    if (gtk_icon_theme_has_icon(
            theme,
            "software-update-available")) {

        app_indicator_set_icon_full(
            tray_indicator,
            "software-update-available",
            "System updates available"
        );

    } else if (gtk_icon_theme_has_icon(
                   theme,
                   "system-software-update")) {

        app_indicator_set_icon_full(
            tray_indicator,
            "system-software-update",
            "System updates available"
        );

    } else {

        /*
         * If the desktop does not provide a standard
         * update icon, keep Helwan Store's own icon.
         */
        app_indicator_set_icon_full(
            tray_indicator,
            "hel-store",
            "System updates available"
        );
    }

    gchar *title = NULL;

    if (update_count == 1) {

        title = g_strdup(
            "Helwan Store - 1 update available"
        );

    } else {

        title = g_strdup_printf(
            "Helwan Store - %d updates available",
            update_count
        );
    }

    app_indicator_set_title(
        tray_indicator,
        title
    );

    g_free(title);
}


/* =========================================================
 * Tray status
 * ========================================================= */

static void set_tray_status(int update_count)
{
    if (!tray_indicator)
        return;

    if (update_count > 0) {

        set_update_tray_icon(
            update_count
        );

    } else {

        set_normal_tray_icon();
    }
}


/* =========================================================
 * Update check
 * ========================================================= */

static gboolean update_check_callback(
    gpointer user_data)
{
    (void)user_data;

    if (!tray_indicator)
        return G_SOURCE_CONTINUE;

    int update_count =
        backend_get_update_count();

    set_tray_status(
        update_count
    );

    /*
     * Notify only when the update state/count changes.
     */
    if (update_count > 0 &&
        update_count != last_update_count) {

        show_update_notification(
            update_count
        );
    }

    last_update_count =
        update_count;

    /*
     * Update the main Store window.
     */
    ui_window_update_update_status(
        update_count
    );

    return G_SOURCE_CONTINUE;
}


/* =========================================================
 * Public immediate update check
 * ========================================================= */

void tray_check_updates_now(void)
{
    if (!tray_indicator)
        return;

    update_check_callback(NULL);
}


/* =========================================================
 * Initialize tray
 * ========================================================= */

void tray_init(void)
{
    if (tray_indicator)
        return;

    /*
     * The icon name refers to:
     *
     * /usr/share/icons/hicolor/256x256/apps/hel-store.png
     *
     * The icon theme must therefore know the Helwan icon.
     */
    tray_indicator =
        app_indicator_new(
            "helwan-store",
            "hel-store",
            APP_INDICATOR_CATEGORY_APPLICATION_STATUS
        );

    if (!tray_indicator) {

        g_warning(
            "Failed to create Helwan Store AppIndicator."
        );

        return;
    }

    app_indicator_set_status(
        tray_indicator,
        APP_INDICATOR_STATUS_ACTIVE
    );

    GtkWidget *menu =
        create_tray_menu();

    app_indicator_set_menu(
        tray_indicator,
        GTK_MENU(menu)
    );

    /*
     * Initial state.
     */
    last_update_count = -1;

    tray_check_updates_now();

    /*
     * Check automatically every 30 minutes.
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

    if (tray_indicator) {

        app_indicator_set_status(
            tray_indicator,
            APP_INDICATOR_STATUS_PASSIVE
        );

        g_object_unref(
            tray_indicator
        );

        tray_indicator = NULL;
    }

    last_update_count = -1;
}
