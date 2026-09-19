//ui_window.c
#include "ui.h"
#include "backend_alpm.h"
#include "icon_manager.h"

#include <gtk/gtk.h>
#include <string.h>
#include <stdio.h>

/*
 * async_loader.c does not have a header file.
 */
void async_run(
    void (*worker)(void *data),
    void (*finished)(void *data),
    void *data
);


/* =========================================================
 * Global UI state
 * ========================================================= */

static GtkListStore *store_model = NULL;
static GtkTreeModelFilter *filter_model = NULL;

static GtkWidget *tree_view = NULL;
static GtkWidget *search_entry = NULL;
static GtkWidget *repo_combo = NULL;
static GtkWidget *cat_combo = NULL;

static GtkWidget *install_btn = NULL;
static GtkWidget *remove_btn = NULL;
static GtkWidget *details_btn = NULL;
static GtkWidget *updates_btn = NULL;

static GtkWidget *refresh_btn = NULL;
static GtkWidget *upgrade_btn = NULL;

static GtkWidget *progress_bar = NULL;
static GtkWidget *progress_label = NULL;

static GtkWidget *update_status_label = NULL;

static GtkWidget *main_window = NULL;

static GList *package_list = NULL;

static gboolean show_updates_only = FALSE;
static gboolean operation_running = FALSE;

static gchar *last_operation_output = NULL;


/* =========================================================
 * Window close handler
 *
 * Closing the window hides it and keeps Helwan Store
 * running in the system tray.
 * ========================================================= */

static gboolean on_window_delete(
    GtkWidget *widget,
    GdkEvent *event,
    gpointer user_data)
{
    (void)event;
    (void)user_data;

    gtk_widget_hide(widget);

    return TRUE;
}


/* =========================================================
 * Update status
 * ========================================================= */

void ui_window_update_update_status(
    int update_count)
{
    if (!update_status_label)
        return;

    if (update_count > 0) {

        gchar *text = NULL;

        if (update_count == 1) {

            text =
                g_strdup(
                    "1 system update is available."
                );

        } else {

            text =
                g_strdup_printf(
                    "%d system updates are available.",
                    update_count
                );
        }

        gtk_label_set_text(
            GTK_LABEL(update_status_label),
            text
        );

        g_free(text);

    } else {

        gtk_label_set_text(
            GTK_LABEL(update_status_label),
            "System is up to date."
        );
    }
}


/* =========================================================
 * Async operation data
 * ========================================================= */

typedef enum {
    OP_INSTALL,
    OP_REMOVE,
    OP_REFRESH,
    OP_UPGRADE
} OperationType;


typedef struct {
    OperationType type;
    char *package_name;
    int result;
} OperationData;


/* =========================================================
 * Progress update
 * ========================================================= */

typedef struct {
    char *stage;
    char *package_name;
    int percent;
} ProgressUpdate;


/* =========================================================
 * Pacman output update
 * ========================================================= */

typedef struct {
    char *output;
} OutputUpdate;


/* =========================================================
 * Modern CSS Styling
 * ========================================================= */

static void apply_custom_stylesheet(void)
{
    GtkCssProvider *provider =
        gtk_css_provider_new();

    GError *error = NULL;

    gtk_css_provider_load_from_path(
        provider,
        "data/style.css",
        &error
    );

    if (error != NULL) {

        g_warning(
            "Could not load CSS file: %s",
            error->message
        );

        g_error_free(error);
        g_object_unref(provider);

        return;
    }

    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    g_object_unref(provider);
}


/* =========================================================
 * Package memory management
 * ========================================================= */

static void free_package_info(
    PackageInfo *info)
{
    if (!info)
        return;

    g_free(info->name);
    g_free(info->version);
    g_free(info->desc);
    g_free(info->repo);
    g_free(info);
}


static void free_package_list(void)
{
    if (package_list) {

        g_list_free_full(
            package_list,
            (GDestroyNotify)free_package_info
        );

        package_list = NULL;
    }
}


/* =========================================================
 * Operation output memory
 * ========================================================= */

static void clear_last_operation_output(void)
{
    g_free(last_operation_output);
    last_operation_output = NULL;
}


static void set_last_operation_output(
    const char *output)
{
    if (!output || !*output)
        return;

    g_free(last_operation_output);

    last_operation_output =
        g_strdup(output);
}


/* =========================================================
 * Category detection
 * ========================================================= */

static gboolean package_matches_category(
    PackageInfo *info,
    const char *category)
{
    if (!info || !category)
        return FALSE;

    if (strcmp(
            category,
            "All Categories"
        ) == 0) {

        return TRUE;
    }

    gchar *name =
        g_ascii_strdown(
            info->name
                ? info->name
                : "",
            -1
        );

    gchar *desc =
        g_ascii_strdown(
            info->desc
                ? info->desc
                : "",
            -1
        );

    gboolean result = FALSE;

    if (strcmp(
            category,
            "Multimedia"
        ) == 0) {

        const char *keywords[] = {
            "vlc",
            "ffmpeg",
            "gstreamer",
            "mpv",
            "audio",
            "video",
            "music",
            "player",
            "codec",
            "image",
            "photo",
            "media",
            NULL
        };

        for (int i = 0;
             keywords[i];
             i++) {

            if (strstr(name, keywords[i]) ||
                strstr(desc, keywords[i])) {

                result = TRUE;
                break;
            }
        }

    } else if (strcmp(
                   category,
                   "Games"
               ) == 0) {

        const char *keywords[] = {
            "game",
            "games",
            "steam",
            "lutris",
            "wine",
            "minecraft",
            "retro",
            NULL
        };

        for (int i = 0;
             keywords[i];
             i++) {

            if (strstr(name, keywords[i]) ||
                strstr(desc, keywords[i])) {

                result = TRUE;
                break;
            }
        }

    } else if (strcmp(
                   category,
                   "Development"
               ) == 0) {

        const char *keywords[] = {
            "gcc",
            "clang",
            "python",
            "python-",
            "perl",
            "ruby",
            "node",
            "npm",
            "rust",
            "cargo",
            "git",
            "cmake",
            "meson",
            "make",
            "compiler",
            "development",
            "developer",
            "library",
            "lib",
            NULL
        };

        for (int i = 0;
             keywords[i];
             i++) {

            if (strstr(name, keywords[i]) ||
                strstr(desc, keywords[i])) {

                result = TRUE;
                break;
            }
        }

    } else if (strcmp(
                   category,
                   "System Tools"
               ) == 0) {

        const char *keywords[] = {
            "system",
            "kernel",
            "network",
            "networkmanager",
            "firewall",
            "disk",
            "mount",
            "terminal",
            "process",
            "monitor",
            "backup",
            "firmware",
            "boot",
            "pacman",
            "sudo",
            NULL
        };

        for (int i = 0;
             keywords[i];
             i++) {

            if (strstr(name, keywords[i]) ||
                strstr(desc, keywords[i])) {

                result = TRUE;
                break;
            }
        }
    }

    g_free(name);
    g_free(desc);

    return result;
}


/* =========================================================
 * Search / Repository / Category / Updates filtering
 * ========================================================= */

static gboolean package_visible(
    GtkTreeModel *model,
    GtkTreeIter *iter,
    gpointer user_data)
{
    gchar *name = NULL;
    gchar *version = NULL;
    gchar *description = NULL;
    gchar *repo = NULL;
    gchar *status_display = NULL;
    gchar *status_real = NULL;

    gtk_tree_model_get(
        model,
        iter,
        0, &name,
        1, &version,
        2, &description,
        3, &repo,
        4, &status_display,
        5, &status_real,
        -1
    );

    gboolean visible = TRUE;

    const gchar *search =
        search_entry
            ? gtk_entry_get_text(
                GTK_ENTRY(search_entry))
            : "";

    const gchar *selected_repo =
        repo_combo
            ? gtk_combo_box_text_get_active_text(
                GTK_COMBO_BOX_TEXT(repo_combo))
            : "All Repositories";

    const gchar *selected_category =
        cat_combo
            ? gtk_combo_box_text_get_active_text(
                GTK_COMBO_BOX_TEXT(cat_combo))
            : "All Categories";

    if (show_updates_only) {

        if (!status_real ||
            strcmp(
                status_real,
                "Update Available"
            ) != 0) {

            visible = FALSE;
        }
    }

    if (visible &&
        search &&
        *search) {

        gchar *lower_search =
            g_ascii_strdown(
                search,
                -1
            );

        gchar *lower_name =
            g_ascii_strdown(
                name ? name : "",
                -1
            );

        gchar *lower_desc =
            g_ascii_strdown(
                description
                    ? description
                    : "",
                -1
            );

        if (!strstr(
                lower_name,
                lower_search
            ) &&
            !strstr(
                lower_desc,
                lower_search
            )) {

            visible = FALSE;
        }

        g_free(lower_search);
        g_free(lower_name);
        g_free(lower_desc);
    }

    if (visible &&
        selected_repo &&
        strcmp(
            selected_repo,
            "All Repositories"
        ) != 0) {

        if (!repo ||
            strcmp(
                repo,
                selected_repo
            ) != 0) {

            visible = FALSE;
        }
    }

    if (visible) {

        PackageInfo temp;

        temp.name = name;
        temp.version = version;
        temp.desc = description;
        temp.repo = repo;

        temp.installed =
            status_real &&
            (
                strcmp(
                    status_real,
                    "Installed"
                ) == 0 ||

                strcmp(
                    status_real,
                    "Update Available"
                ) == 0
            );

        if (!package_matches_category(
                &temp,
                selected_category)) {

            visible = FALSE;
        }
    }

    g_free(name);
    g_free(version);
    g_free(description);
    g_free(repo);
    g_free(status_display);
    g_free(status_real);

    return visible;
}


static void refresh_filter(void)
{
    if (filter_model)
        gtk_tree_model_filter_refilter(
            filter_model
        );
}


/* =========================================================
 * Selected package
 * ========================================================= */

static PackageInfo *get_selected_package(void)
{
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(
            GTK_TREE_VIEW(tree_view)
        );

    GtkTreeModel *model = NULL;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(
            selection,
            &model,
            &iter
        )) {

        return NULL;
    }

    GtkTreeModel *child_model = NULL;
    GtkTreeIter child_iter;

    if (GTK_IS_TREE_MODEL_FILTER(model)) {

        child_model =
            gtk_tree_model_filter_get_model(
                GTK_TREE_MODEL_FILTER(model)
            );

        gtk_tree_model_filter_convert_iter_to_child_iter(
            GTK_TREE_MODEL_FILTER(model),
            &child_iter,
            &iter
        );

    } else {

        child_model = model;
        child_iter = iter;
    }

    gchar *name = NULL;

    gtk_tree_model_get(
        child_model,
        &child_iter,
        0,
        &name,
        -1
    );

    if (!name)
        return NULL;

    PackageInfo *found = NULL;

    for (GList *l = package_list;
         l;
         l = l->next) {

        PackageInfo *info = l->data;

        if (info &&
            info->name &&
            strcmp(
                info->name,
                name
            ) == 0) {

            found = info;
            break;
        }
    }

    g_free(name);

    return found;
}


/* =========================================================
 * Action button state
 * ========================================================= */

static void update_action_buttons(void)
{
    PackageInfo *info =
        get_selected_package();

    if (operation_running) {

        gtk_widget_set_sensitive(
            install_btn,
            FALSE
        );

        gtk_widget_set_sensitive(
            remove_btn,
            FALSE
        );

        gtk_widget_set_sensitive(
            details_btn,
            FALSE
        );

        return;
    }

    if (!info) {

        gtk_widget_set_sensitive(
            install_btn,
            FALSE
        );

        gtk_widget_set_sensitive(
            remove_btn,
            FALSE
        );

        gtk_widget_set_sensitive(
            details_btn,
            FALSE
        );

        return;
    }

    gtk_widget_set_sensitive(
        details_btn,
        TRUE
    );

    gtk_widget_set_sensitive(
        install_btn,
        !info->installed
    );

    gtk_widget_set_sensitive(
        remove_btn,
        info->installed
    );
}


/* =========================================================
 * Small Status Area
 * ========================================================= */

static void update_status_widgets(
    const char *message,
    int percent)
{
    if (!progress_bar || !progress_label)
        return;

    if (percent < 0)
        percent = 0;

    if (percent > 100)
        percent = 100;

    gtk_progress_bar_set_fraction(
        GTK_PROGRESS_BAR(progress_bar),
        percent / 100.0
    );

    gchar *percent_text =
        g_strdup_printf(
            "%d%%",
            percent
        );

    gtk_progress_bar_set_text(
        GTK_PROGRESS_BAR(progress_bar),
        percent_text
    );

    g_free(percent_text);

    if (message && *message) {

        gtk_label_set_text(
            GTK_LABEL(progress_label),
            message
        );
    }
}


/* =========================================================
 * Progress update on GTK main thread
 * ========================================================= */

static gboolean apply_progress_update(
    gpointer user_data)
{
    ProgressUpdate *update =
        user_data;

    if (!update)
        return G_SOURCE_REMOVE;

    if (update->stage &&
        *update->stage) {

        set_last_operation_output(
            update->stage
        );

        if (operation_running) {

            update_status_widgets(
                update->stage,
                update->percent
            );
        }
    }

    g_free(update->stage);
    g_free(update->package_name);
    g_free(update);

    return G_SOURCE_REMOVE;
}


/* =========================================================
 * Backend progress callback
 * ========================================================= */

static void backend_progress_callback(
    const char *stage,
    const char *package_name,
    int percent,
    void *user_data)
{
    ProgressUpdate *update =
        g_new0(
            ProgressUpdate,
            1
        );

    if (!update)
        return;

    update->stage =
        g_strdup(
            stage
                ? stage
                : "Working"
        );

    update->package_name =
        g_strdup(
            package_name
                ? package_name
                : ""
        );

    update->percent =
        percent;

    g_idle_add(
        apply_progress_update,
        update
    );
}


/* =========================================================
 * Pacman output update
 * ========================================================= */

static gboolean apply_output_update(
    gpointer user_data)
{
    OutputUpdate *update =
        user_data;

    if (!update)
        return G_SOURCE_REMOVE;

    if (update->output &&
        *update->output) {

        set_last_operation_output(
            update->output
        );

        if (operation_running) {

            gtk_label_set_text(
                GTK_LABEL(progress_label),
                update->output
            );
        }
    }

    g_free(update->output);
    g_free(update);

    return G_SOURCE_REMOVE;
}


static void backend_output_callback(
    const char *output,
    void *user_data)
{
    if (!output || !*output)
        return;

    OutputUpdate *update =
        g_new0(
            OutputUpdate,
            1
        );

    if (!update)
        return;

    update->output =
        g_strdup(output);

    g_idle_add(
        apply_output_update,
        update
    );
}


/* =========================================================
 * Operation UI
 * ========================================================= */

static void set_operation_state(
    gboolean running,
    const char *message)
{
    operation_running = running;

    gtk_widget_set_sensitive(
        search_entry,
        !running
    );

    gtk_widget_set_sensitive(
        repo_combo,
        !running
    );

    gtk_widget_set_sensitive(
        cat_combo,
        !running
    );

    gtk_widget_set_sensitive(
        refresh_btn,
        !running
    );

    gtk_widget_set_sensitive(
        upgrade_btn,
        !running
    );

    gtk_widget_set_sensitive(
        updates_btn,
        !running
    );

    if (running) {

        gtk_widget_show(
            progress_label
        );

        gtk_widget_show(
            progress_bar
        );

        gtk_label_set_text(
            GTK_LABEL(progress_label),
            message
                ? message
                : "Working..."
        );

        gtk_progress_bar_set_fraction(
            GTK_PROGRESS_BAR(progress_bar),
            0.0
        );

        gtk_progress_bar_set_text(
            GTK_PROGRESS_BAR(progress_bar),
            "0%"
        );

        gtk_widget_set_sensitive(
            tree_view,
            FALSE
        );

    } else {

        gtk_widget_hide(
            progress_label
        );

        gtk_widget_hide(
            progress_bar
        );

        gtk_widget_set_sensitive(
            tree_view,
            TRUE
        );
    }

    update_action_buttons();
}


/* =========================================================
 * Package details
 * ========================================================= */

static void show_package_details(
    GtkWindow *parent,
    PackageInfo *info)
{
    if (!info)
        return;

    GtkWidget *dialog =
        gtk_dialog_new_with_buttons(
            "Package Details",
            parent,
            GTK_DIALOG_MODAL |
            GTK_DIALOG_DESTROY_WITH_PARENT,
            "Close",
            GTK_RESPONSE_CLOSE,
            NULL
        );

    gtk_window_set_default_size(
        GTK_WINDOW(dialog),
        500,
        300
    );

    GtkWidget *content =
        gtk_dialog_get_content_area(
            GTK_DIALOG(dialog)
        );

    GtkWidget *grid =
        gtk_grid_new();

    gtk_grid_set_row_spacing(
        GTK_GRID(grid),
        10
    );

    gtk_grid_set_column_spacing(
        GTK_GRID(grid),
        15
    );

    gtk_widget_set_margin_start(
        grid,
        20
    );

    gtk_widget_set_margin_end(
        grid,
        20
    );

    gtk_widget_set_margin_top(
        grid,
        20
    );

    gtk_widget_set_margin_bottom(
        grid,
        20
    );

    GtkWidget *name_label =
        gtk_label_new(
            info->name
        );

    GtkWidget *version_label =
        gtk_label_new(
            info->version
        );

    GtkWidget *repo_label =
        gtk_label_new(
            info->repo
        );

    const char *status =
        info->installed
            ? "Installed"
            : "Available";

    if (info->installed &&
        backend_package_has_update(
            info->name
        )) {

        status = "Update Available";
    }

    GtkWidget *status_label =
        gtk_label_new(
            status
        );

    GtkWidget *desc_label =
        gtk_label_new(
            info->desc
                ? info->desc
                : "No description"
        );

    gtk_label_set_xalign(
        GTK_LABEL(desc_label),
        0.0
    );

    gtk_label_set_line_wrap(
        GTK_LABEL(desc_label),
        TRUE
    );

    gtk_grid_attach(
        GTK_GRID(grid),
        gtk_label_new("Package:"),
        0, 0, 1, 1
    );

    gtk_grid_attach(
        GTK_GRID(grid),
        name_label,
        1, 0, 1, 1
    );

    gtk_grid_attach(
        GTK_GRID(grid),
        gtk_label_new("Version:"),
        0, 1, 1, 1
    );

    gtk_grid_attach(
        GTK_GRID(grid),
        version_label,
        1, 1, 1, 1
    );

    gtk_grid_attach(
        GTK_GRID(grid),
        gtk_label_new("Repository:"),
        0, 2, 1, 1
    );

    gtk_grid_attach(
        GTK_GRID(grid),
        repo_label,
        1, 2, 1, 1
    );

    gtk_grid_attach(
        GTK_GRID(grid),
        gtk_label_new("Status:"),
        0, 3, 1, 1
    );

    gtk_grid_attach(
        GTK_GRID(grid),
        status_label,
        1, 3, 1, 1
    );

    gtk_grid_attach(
        GTK_GRID(grid),
        gtk_label_new("Description:"),
        0, 4, 1, 1
    );

    gtk_grid_attach(
        GTK_GRID(grid),
        desc_label,
        1, 4, 1, 2
    );

    gtk_container_add(
        GTK_CONTAINER(content),
        grid
    );

    gtk_widget_show_all(dialog);

    gtk_dialog_run(
        GTK_DIALOG(dialog)
    );

    gtk_widget_destroy(dialog);
}


/* =========================================================
 * Reload package list
 * ========================================================= */

static void reload_packages(void)
{
    gtk_list_store_clear(
        store_model
    );

    free_package_list();

    package_list =
        backend_get_all_packages();

    for (GList *l = package_list;
         l;
         l = l->next) {

        PackageInfo *info = l->data;

        if (!info)
            continue;

        GtkTreeIter iter;

        gtk_list_store_append(
            store_model,
            &iter
        );

        const gchar *status =
            info->installed
                ? "Installed"
                : "Available";

        const gchar *status_display =
            "Installed";

        if (info->installed &&
            backend_package_has_update(
                info->name
            )) {

            status =
                "Update Available";

            status_display =
                "UP";

        } else if (!info->installed) {

            status_display =
                "Not Installed";
        }

        gtk_list_store_set(
            store_model,
            &iter,
            0, info->name,
            1, info->version,
            2, info->desc
                ? info->desc
                : "No description",
            3, info->repo,
            4, status_display,
            5, status,
            -1
        );
    }

    refresh_filter();
    update_action_buttons();

    /*
     * Update the system status shown inside the Store.
     */
    ui_window_update_update_status(
        backend_get_update_count()
    );
}


/* =========================================================
 * Async workers
 * ========================================================= */

static void install_worker(
    void *data)
{
    OperationData *op = data;

    op->result =
        backend_install_package(
            op->package_name
        );
}


static void remove_worker(
    void *data)
{
    OperationData *op = data;

    op->result =
        backend_remove_package(
            op->package_name
        );
}


static void refresh_worker(
    void *data)
{
    OperationData *op = data;

    op->result =
        backend_refresh_databases();
}


static void upgrade_worker(
    void *data)
{
    OperationData *op = data;

    op->result =
        backend_upgrade_system();
}


/* =========================================================
 * Async operation finished
 * ========================================================= */

static void operation_finished(
    void *data)
{
    OperationData *op = data;

    if (!op)
        return;

    gboolean success =
        (op->result != 0);

    backend_set_progress_callback(
        NULL,
        NULL
    );

    backend_set_output_callback(
        NULL,
        NULL
    );

    GtkWindow *parent =
        GTK_WINDOW(
            main_window
        );

    set_operation_state(
        FALSE,
        NULL
    );

    if (success) {

        if (op->type == OP_INSTALL ||
            op->type == OP_REMOVE) {

            const char *message =
                op->type == OP_INSTALL
                    ? "Package '%s' was installed successfully."
                    : "Package '%s' was removed successfully.";

            GtkWidget *dialog =
                gtk_message_dialog_new(
                    parent,
                    GTK_DIALOG_MODAL,
                    GTK_MESSAGE_INFO,
                    GTK_BUTTONS_OK,
                    message,
                    op->package_name
                );

            gtk_dialog_run(
                GTK_DIALOG(dialog)
            );

            gtk_widget_destroy(dialog);

        } else {

            const char *message =
                op->type == OP_REFRESH
                    ? "Package databases were refreshed successfully."
                    : "System upgrade completed successfully.";

            GtkWidget *dialog =
                gtk_message_dialog_new(
                    parent,
                    GTK_DIALOG_MODAL,
                    GTK_MESSAGE_INFO,
                    GTK_BUTTONS_OK,
                    "%s",
                    message
                );

            gtk_dialog_run(
                GTK_DIALOG(dialog)
            );

            gtk_widget_destroy(dialog);
        }

    } else {

        const char *real_error =
            last_operation_output &&
            *last_operation_output
                ? last_operation_output
                : "Pacman operation failed.";

        gchar *message = NULL;

        if (op->type == OP_INSTALL) {

            message =
                g_strdup_printf(
                    "Failed to install package '%s'.\n\n"
                    "Pacman message:\n%s",
                    op->package_name,
                    real_error
                );

        } else if (op->type == OP_REMOVE) {

            message =
                g_strdup_printf(
                    "Failed to remove package '%s'.\n\n"
                    "Pacman message:\n%s",
                    op->package_name,
                    real_error
                );

        } else if (op->type == OP_REFRESH) {

            message =
                g_strdup_printf(
                    "Failed to refresh package databases.\n\n"
                    "Pacman message:\n%s",
                    real_error
                );

        } else {

            message =
                g_strdup_printf(
                    "System upgrade failed.\n\n"
                    "Pacman message:\n%s",
                    real_error
                );
        }

        GtkWidget *dialog =
            gtk_message_dialog_new(
                parent,
                GTK_DIALOG_MODAL,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_OK,
                "%s",
                message
            );

        gtk_dialog_run(
            GTK_DIALOG(dialog)
        );

        gtk_widget_destroy(dialog);

        g_free(message);
    }

    reload_packages();

    g_free(op->package_name);
    g_free(op);

    clear_last_operation_output();
}


/* =========================================================
 * Start async operation
 * ========================================================= */

static void start_operation(
    OperationType type,
    const char *package_name,
    void (*worker)(void *data),
    const char *message)
{
    OperationData *op =
        g_new0(
            OperationData,
            1
        );

    if (!op)
        return;

    op->type = type;

    op->package_name =
        package_name
            ? g_strdup(package_name)
            : NULL;

    op->result = 0;

    clear_last_operation_output();

    backend_set_progress_callback(
        backend_progress_callback,
        NULL
    );

    backend_set_output_callback(
        backend_output_callback,
        NULL
    );

    set_operation_state(
        TRUE,
        message
    );

    async_run(
        worker,
        operation_finished,
        op
    );
}


/* =========================================================
 * Install
 * ========================================================= */

static void on_install_clicked(
    GtkButton *button,
    gpointer user_data)
{
    GtkWindow *parent =
        GTK_WINDOW(user_data);

    PackageInfo *info =
        get_selected_package();

    if (!info ||
        operation_running) {

        return;
    }

    char *package_name =
        g_strdup(info->name);

    GtkWidget *dialog =
        gtk_message_dialog_new(
            parent,
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_YES_NO,
            "Install package '%s'?",
            package_name
        );

    gint response =
        gtk_dialog_run(
            GTK_DIALOG(dialog)
        );

    gtk_widget_destroy(dialog);

    if (response != GTK_RESPONSE_YES) {

        g_free(package_name);
        return;
    }

    start_operation(
        OP_INSTALL,
        package_name,
        install_worker,
        "Installing..."
    );

    g_free(package_name);
}


/* =========================================================
 * Remove
 * ========================================================= */

static void on_remove_clicked(
    GtkButton *button,
    gpointer user_data)
{
    GtkWindow *parent =
        GTK_WINDOW(user_data);

    PackageInfo *info =
        get_selected_package();

    if (!info ||
        operation_running) {

        return;
    }

    char *package_name =
        g_strdup(info->name);

    GtkWidget *dialog =
        gtk_message_dialog_new(
            parent,
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_YES_NO,
            "Remove package '%s'?",
            package_name
        );

    gint response =
        gtk_dialog_run(
            GTK_DIALOG(dialog)
        );

    gtk_widget_destroy(dialog);

    if (response != GTK_RESPONSE_YES) {

        g_free(package_name);
        return;
    }

    start_operation(
        OP_REMOVE,
        package_name,
        remove_worker,
        "Removing..."
    );

    g_free(package_name);
}


/* =========================================================
 * Package details
 * ========================================================= */

static void on_details_clicked(
    GtkButton *button,
    gpointer user_data)
{
    PackageInfo *info =
        get_selected_package();

    if (!info ||
        operation_running) {

        return;
    }

    show_package_details(
        GTK_WINDOW(user_data),
        info
    );
}


/* =========================================================
 * Tree selection
 * ========================================================= */

static void on_selection_changed(
    GtkTreeSelection *selection,
    gpointer user_data)
{
    update_action_buttons();
}


/* =========================================================
 * Search
 * ========================================================= */

static void on_search_changed(
    GtkSearchEntry *entry,
    gpointer user_data)
{
    refresh_filter();
}


/* =========================================================
 * Repository / Category
 * ========================================================= */

static void on_filter_changed(
    GtkComboBox *combo,
    gpointer user_data)
{
    refresh_filter();
}


/* =========================================================
 * Updates button
 * ========================================================= */

static void on_updates_clicked(
    GtkButton *button,
    gpointer user_data)
{
    if (operation_running)
        return;

    show_updates_only =
        !show_updates_only;

    if (show_updates_only) {

        gtk_button_set_label(
            GTK_BUTTON(updates_btn),
            "All Packages"
        );

    } else {

        gtk_button_set_label(
            GTK_BUTTON(updates_btn),
            "Updates"
        );
    }

    refresh_filter();
}


/* =========================================================
 * Refresh repositories
 * ========================================================= */

static void on_refresh_clicked(
    GtkButton *button,
    gpointer user_data)
{
    if (operation_running)
        return;

    start_operation(
        OP_REFRESH,
        NULL,
        refresh_worker,
        "Refreshing package databases..."
    );
}


/* =========================================================
 * Upgrade system
 * ========================================================= */

static void on_upgrade_clicked(
    GtkButton *button,
    gpointer user_data)
{
    GtkWindow *parent =
        GTK_WINDOW(user_data);

    if (operation_running)
        return;

    GtkWidget *dialog =
        gtk_message_dialog_new(
            parent,
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_YES_NO,
            "Check for and install all available system updates?"
        );

    gint response =
        gtk_dialog_run(
            GTK_DIALOG(dialog)
        );

    gtk_widget_destroy(dialog);

    if (response != GTK_RESPONSE_YES)
        return;

    start_operation(
        OP_UPGRADE,
        NULL,
        upgrade_worker,
        "Upgrading system..."
    );
}


/* =========================================================
 * About
 * ========================================================= */

static void on_about_clicked(
    GtkButton *button,
    gpointer user_data)
{
    ui_about_dialog_show(
        GTK_WINDOW(user_data)
    );
}


/* =========================================================
 * Repository list
 * ========================================================= */

static void load_repositories(
    GtkComboBoxText *combo)
{
    FILE *file =
        fopen(
            "/etc/pacman.conf",
            "r"
        );

    if (!file) {

        g_warning(
            "Could not open /etc/pacman.conf"
        );

        return;
    }

    char line[512];

    while (fgets(
        line,
        sizeof(line),
        file)) {

        char *start = line;

        while (*start == ' ' ||
               *start == '\t') {

            start++;
        }

        if (*start != '[')
            continue;

        char *end =
            strchr(
                start,
                ']'
            );

        if (!end)
            continue;

        *end = '\0';

        if (strcmp(
                start + 1,
                "options"
            ) == 0) {

            continue;
        }

        gtk_combo_box_text_append_text(
            combo,
            start + 1
        );
    }

    fclose(file);
}


/* =========================================================
 * Present existing main window
 * ========================================================= */

void ui_window_present(void)
{
    if (!main_window)
        return;

    gtk_widget_show(
        main_window
    );

    gtk_window_deiconify(
        GTK_WINDOW(main_window)
    );

    gtk_window_present(
        GTK_WINDOW(main_window)
    );
}


/* =========================================================
 * Main Window
 * ========================================================= */

void ui_window_show(
    int argc,
    char *argv[])
{
    gtk_init(
        &argc,
        &argv
    );

    apply_custom_stylesheet();

    main_window =
        gtk_window_new(
            GTK_WINDOW_TOPLEVEL
        );

    gtk_window_set_title(
        GTK_WINDOW(main_window),
        "Helwan Software Store"
    );

    gtk_window_set_default_size(
    GTK_WINDOW(main_window),
    1100,
    700
);

/*
 * Closing the window hides it instead of terminating
 * the application. The tray icon remains active.
 */
g_signal_connect(
    main_window,
    "delete-event",
    G_CALLBACK(on_window_delete),
    NULL
);

    /*
     * Closing the window hides it instead of terminating
     * the application. The tray icon remains active.
     */
    g_signal_connect(
        main_window,
        "delete-event",
        G_CALLBACK(on_window_delete),
        NULL
    );

    icon_manager_init(
        GTK_WINDOW(main_window),
        "data/hel-store.png"
    );

    GtkWidget *vbox =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL,
            0
        );

    gtk_container_add(
        GTK_CONTAINER(main_window),
        vbox
    );

    GtkWidget *title =
        gtk_label_new(NULL);

    gtk_label_set_markup(
        GTK_LABEL(title),
        "<big><b>Helwan Software Store</b></big>"
    );

    gtk_widget_set_name(
        title,
        "main-title"
    );

    gtk_box_pack_start(
        GTK_BOX(vbox),
        title,
        FALSE,
        FALSE,
        8
    );


    /* =====================================================
     * Controls
     * ===================================================== */

    GtkWidget *controls =
        gtk_box_new(
            GTK_ORIENTATION_HORIZONTAL,
            8
        );

    gtk_widget_set_margin_start(
        controls,
        15
    );

    gtk_widget_set_margin_end(
        controls,
        15
    );

    gtk_box_pack_start(
        GTK_BOX(vbox),
        controls,
        FALSE,
        FALSE,
        8
    );

    search_entry =
        gtk_search_entry_new();

    gtk_entry_set_placeholder_text(
        GTK_ENTRY(search_entry),
        "Search packages..."
    );

    g_signal_connect(
        search_entry,
        "search-changed",
        G_CALLBACK(on_search_changed),
        NULL
    );

    gtk_box_pack_start(
        GTK_BOX(controls),
        search_entry,
        TRUE,
        TRUE,
        0
    );

    cat_combo =
        GTK_WIDGET(
            gtk_combo_box_text_new()
        );

    gtk_combo_box_text_append_text(
        GTK_COMBO_BOX_TEXT(cat_combo),
        "All Categories"
    );

    gtk_combo_box_text_append_text(
        GTK_COMBO_BOX_TEXT(cat_combo),
        "Multimedia"
    );

    gtk_combo_box_text_append_text(
        GTK_COMBO_BOX_TEXT(cat_combo),
        "Games"
    );

    gtk_combo_box_text_append_text(
        GTK_COMBO_BOX_TEXT(cat_combo),
        "Development"
    );

    gtk_combo_box_text_append_text(
        GTK_COMBO_BOX_TEXT(cat_combo),
        "System Tools"
    );

    gtk_combo_box_set_active(
        GTK_COMBO_BOX(cat_combo),
        0
    );

    g_signal_connect(
        cat_combo,
        "changed",
        G_CALLBACK(on_filter_changed),
        NULL
    );

    gtk_box_pack_start(
        GTK_BOX(controls),
        cat_combo,
        FALSE,
        FALSE,
        0
    );

    repo_combo =
        GTK_WIDGET(
            gtk_combo_box_text_new()
        );

    gtk_combo_box_text_append_text(
        GTK_COMBO_BOX_TEXT(repo_combo),
        "All Repositories"
    );

    load_repositories(
        GTK_COMBO_BOX_TEXT(repo_combo)
    );

    gtk_combo_box_set_active(
        GTK_COMBO_BOX(repo_combo),
        0
    );

    g_signal_connect(
        repo_combo,
        "changed",
        G_CALLBACK(on_filter_changed),
        NULL
    );

    gtk_box_pack_start(
        GTK_BOX(controls),
        repo_combo,
        FALSE,
        FALSE,
        0
    );

    refresh_btn =
        gtk_button_new_with_label(
            "Refresh"
        );

    g_signal_connect(
        refresh_btn,
        "clicked",
        G_CALLBACK(on_refresh_clicked),
        main_window
    );

    gtk_box_pack_start(
        GTK_BOX(controls),
        refresh_btn,
        FALSE,
        FALSE,
        0
    );

    updates_btn =
        gtk_button_new_with_label(
            "Updates"
        );

    g_signal_connect(
        updates_btn,
        "clicked",
        G_CALLBACK(on_updates_clicked),
        NULL
    );

    gtk_box_pack_start(
        GTK_BOX(controls),
        updates_btn,
        FALSE,
        FALSE,
        0
    );

    upgrade_btn =
        gtk_button_new_with_label(
            "Upgrade System"
        );

    g_signal_connect(
        upgrade_btn,
        "clicked",
        G_CALLBACK(on_upgrade_clicked),
        main_window
    );

    gtk_box_pack_start(
        GTK_BOX(controls),
        upgrade_btn,
        FALSE,
        FALSE,
        0
    );


    /* =====================================================
     * System Update Status
     * ===================================================== */

    update_status_label =
        gtk_label_new(
            "Checking for system updates..."
        );

    gtk_label_set_xalign(
        GTK_LABEL(update_status_label),
        0.0
    );

    gtk_widget_set_margin_start(
        update_status_label,
        15
    );

    gtk_widget_set_margin_end(
        update_status_label,
        15
    );

    gtk_widget_set_margin_top(
        update_status_label,
        2
    );

    gtk_widget_set_margin_bottom(
        update_status_label,
        2
    );

    gtk_box_pack_start(
        GTK_BOX(vbox),
        update_status_label,
        FALSE,
        FALSE,
        0
    );


    /* =====================================================
     * COMPACT STATUS AREA
     * ===================================================== */

    progress_label =
        gtk_label_new(
            "Working..."
        );

    gtk_label_set_xalign(
        GTK_LABEL(progress_label),
        0.0
    );

    gtk_label_set_ellipsize(
        GTK_LABEL(progress_label),
        PANGO_ELLIPSIZE_END
    );

    gtk_widget_set_size_request(
        progress_label,
        -1,
        22
    );

    gtk_widget_set_margin_start(
        progress_label,
        15
    );

    gtk_widget_set_margin_end(
        progress_label,
        15
    );

    gtk_box_pack_start(
        GTK_BOX(vbox),
        progress_label,
        FALSE,
        FALSE,
        1
    );


    progress_bar =
        gtk_progress_bar_new();

    gtk_progress_bar_set_show_text(
        GTK_PROGRESS_BAR(progress_bar),
        TRUE
    );

    gtk_progress_bar_set_text(
        GTK_PROGRESS_BAR(progress_bar),
        "0%"
    );

    gtk_widget_set_size_request(
        progress_bar,
        -1,
        18
    );

    gtk_widget_set_margin_start(
        progress_bar,
        15
    );

    gtk_widget_set_margin_end(
        progress_bar,
        15
    );

    gtk_widget_set_margin_top(
        progress_bar,
        1
    );

    gtk_widget_set_margin_bottom(
        progress_bar,
        3
    );

    gtk_box_pack_start(
        GTK_BOX(vbox),
        progress_bar,
        FALSE,
        FALSE,
        0
    );

    gtk_widget_hide(
        progress_label
    );

    gtk_widget_hide(
        progress_bar
    );


    /* =====================================================
     * Package table
     * ===================================================== */

    GtkWidget *scrolled =
        gtk_scrolled_window_new(
            NULL,
            NULL
        );

    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC
    );

    gtk_box_pack_start(
        GTK_BOX(vbox),
        scrolled,
        TRUE,
        TRUE,
        3
    );

    store_model =
        gtk_list_store_new(
            6,
            G_TYPE_STRING,
            G_TYPE_STRING,
            G_TYPE_STRING,
            G_TYPE_STRING,
            G_TYPE_STRING,
            G_TYPE_STRING
        );

    filter_model =
        GTK_TREE_MODEL_FILTER(
            gtk_tree_model_filter_new(
                GTK_TREE_MODEL(store_model),
                NULL
            )
        );

    gtk_tree_model_filter_set_visible_func(
        filter_model,
        package_visible,
        NULL,
        NULL
    );

    tree_view =
        gtk_tree_view_new_with_model(
            GTK_TREE_MODEL(filter_model)
        );

    gtk_tree_view_set_headers_visible(
        GTK_TREE_VIEW(tree_view),
        TRUE
    );

    GtkCellRenderer *name_renderer =
        gtk_cell_renderer_text_new();

    GtkCellRenderer *version_renderer =
        gtk_cell_renderer_text_new();

    GtkCellRenderer *description_renderer =
        gtk_cell_renderer_text_new();

    GtkCellRenderer *repo_renderer =
        gtk_cell_renderer_text_new();

    GtkCellRenderer *status_renderer =
        gtk_cell_renderer_text_new();

    gtk_tree_view_insert_column_with_attributes(
        GTK_TREE_VIEW(tree_view),
        -1,
        "Package Name",
        name_renderer,
        "text",
        0,
        NULL
    );

    gtk_tree_view_insert_column_with_attributes(
        GTK_TREE_VIEW(tree_view),
        -1,
        "Version",
        version_renderer,
        "text",
        1,
        NULL
    );

    GtkTreeViewColumn *version_column =
        gtk_tree_view_get_column(
            GTK_TREE_VIEW(tree_view),
            1
        );

    if (version_column) {

        gtk_tree_view_column_set_sizing(
            version_column,
            GTK_TREE_VIEW_COLUMN_FIXED
        );

        gtk_tree_view_column_set_fixed_width(
            version_column,
            175
        );
    }

    gtk_tree_view_insert_column_with_attributes(
        GTK_TREE_VIEW(tree_view),
        -1,
        "Description",
        description_renderer,
        "text",
        2,
        NULL
    );

    gtk_tree_view_insert_column_with_attributes(
        GTK_TREE_VIEW(tree_view),
        -1,
        "Repository",
        repo_renderer,
        "text",
        3,
        NULL
    );

    GtkTreeViewColumn *status_column =
        gtk_tree_view_column_new_with_attributes(
            "Status",
            status_renderer,
            "text",
            4,
            NULL
        );

    gtk_tree_view_append_column(
        GTK_TREE_VIEW(tree_view),
        status_column
    );

    g_object_set(
        status_renderer,
        "xalign",
        0.5,
        "ellipsize",
        PANGO_ELLIPSIZE_NONE,
        NULL
    );

    gtk_tree_view_column_set_alignment(
        status_column,
        0.5
    );

    gtk_tree_view_column_set_sizing(
        status_column,
        GTK_TREE_VIEW_COLUMN_FIXED
    );

    gtk_tree_view_column_set_fixed_width(
        status_column,
        42
    );

    gtk_tree_view_column_set_min_width(
        status_column,
        42
    );

    gtk_tree_view_column_set_max_width(
        status_column,
        42
    );

    gtk_tree_view_column_set_resizable(
        status_column,
        FALSE
    );

    gtk_tree_view_column_set_expand(
        status_column,
        FALSE
    );

    GtkTreeViewColumn *description_column =
        gtk_tree_view_get_column(
            GTK_TREE_VIEW(tree_view),
            2
        );

    if (description_column) {

        gtk_tree_view_column_set_sizing(
            description_column,
            GTK_TREE_VIEW_COLUMN_FIXED
        );

        gtk_tree_view_column_set_fixed_width(
            description_column,
            500
        );
    }

    GtkTreeViewColumn *real_status_column =
        gtk_tree_view_column_new();

    gtk_tree_view_append_column(
        GTK_TREE_VIEW(tree_view),
        real_status_column
    );

    gtk_tree_view_column_set_visible(
        real_status_column,
        FALSE
    );

    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(
            GTK_TREE_VIEW(tree_view)
        );

    g_signal_connect(
        selection,
        "changed",
        G_CALLBACK(on_selection_changed),
        NULL
    );

    gtk_container_add(
        GTK_CONTAINER(scrolled),
        tree_view
    );


    /* =====================================================
     * Action buttons
     * ===================================================== */

    GtkWidget *actions =
        gtk_box_new(
            GTK_ORIENTATION_HORIZONTAL,
            8
        );

    gtk_widget_set_margin_start(
        actions,
        15
    );

    gtk_widget_set_margin_end(
        actions,
        15
    );

    gtk_widget_set_margin_top(
        actions,
        3
    );

    gtk_widget_set_margin_bottom(
        actions,
        3
    );

    gtk_box_pack_start(
        GTK_BOX(vbox),
        actions,
        FALSE,
        FALSE,
        0
    );

    details_btn =
        gtk_button_new_with_label(
            "Package Details"
        );

    install_btn =
        gtk_button_new_with_label(
            "Install"
        );

    gtk_widget_set_name(
        install_btn,
        "install_btn"
    );

    remove_btn =
        gtk_button_new_with_label(
            "Remove"
        );

    gtk_widget_set_name(
        remove_btn,
        "remove_btn"
    );

    GtkWidget *about_btn =
        gtk_button_new_with_label(
            "About"
        );

    gtk_box_pack_start(
        GTK_BOX(actions),
        details_btn,
        FALSE,
        FALSE,
        0
    );

    gtk_box_pack_start(
        GTK_BOX(actions),
        install_btn,
        FALSE,
        FALSE,
        0
    );

    gtk_box_pack_start(
        GTK_BOX(actions),
        remove_btn,
        FALSE,
        FALSE,
        0
    );

    gtk_box_pack_end(
        GTK_BOX(actions),
        about_btn,
        FALSE,
        FALSE,
        0
    );

    g_signal_connect(
        details_btn,
        "clicked",
        G_CALLBACK(on_details_clicked),
        main_window
    );

    g_signal_connect(
        install_btn,
        "clicked",
        G_CALLBACK(on_install_clicked),
        main_window
    );

    g_signal_connect(
        remove_btn,
        "clicked",
        G_CALLBACK(on_remove_clicked),
        main_window
    );

    g_signal_connect(
        about_btn,
        "clicked",
        G_CALLBACK(on_about_clicked),
        main_window
    );

    gtk_widget_set_sensitive(
        details_btn,
        FALSE
    );

    gtk_widget_set_sensitive(
        install_btn,
        FALSE
    );

    gtk_widget_set_sensitive(
        remove_btn,
        FALSE
    );


    /* =====================================================
     * Load packages
     * ===================================================== */

    reload_packages();

    gtk_widget_show_all(
        main_window
    );

    gtk_widget_hide(
        progress_label
    );

    gtk_widget_hide(
        progress_bar
    );


    /* =====================================================
     * Start tray
     * ===================================================== */

    tray_init();


    /* =====================================================
     * GTK main loop
     * ===================================================== */

    gtk_main();


    /* =====================================================
     * Cleanup
     * ===================================================== */

    tray_cleanup();

    backend_set_progress_callback(
        NULL,
        NULL
    );

    backend_set_output_callback(
        NULL,
        NULL
    );

    clear_last_operation_output();

    free_package_list();

    main_window = NULL;
}
