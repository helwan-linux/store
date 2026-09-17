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

static GList *package_list = NULL;

static gboolean show_updates_only = FALSE;
static gboolean operation_running = FALSE;


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
 * Progress update data
 * ========================================================= */

typedef struct {
    char *stage;
    char *package_name;
    int percent;
} ProgressUpdate;


/* =========================================================
 * Modern CSS Styling
 * ========================================================= */

static void apply_custom_stylesheet(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css = 
        "window {"
        "   background-color: #f6f8fa;"
        "}"
        "#main-title {"
        "   color: #24292e;"
        "   font-weight: bold;"
        "   margin-top: 6px;"
        "}"
        "#install_btn {"
        "   background-image: image(#2ea44f);"
        "   color: white;"
        "   border-radius: 6px;"
        "}"
        "#remove_btn {"
        "   background-image: image(#cb2431);"
        "   color: white;"
        "   border-radius: 6px;"
        "}"
        "progressbar trough {"
        "   border-radius: 6px;"
        "   background-color: #e1e4e8;"
        "}"
        "progressbar progress {"
        "   border-radius: 6px;"
        "   background-image: image(#0366d6);"
        "}"
    ;

    gtk_css_provider_load_from_data(provider, css, -1, NULL);
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

            if (strstr(
                    name,
                    keywords[i]
                ) ||
                strstr(
                    desc,
                    keywords[i]
                )) {

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

            if (strstr(
                    name,
                    keywords[i]
                ) ||
                strstr(
                    desc,
                    keywords[i]
                )) {

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

            if (strstr(
                    name,
                    keywords[i]
                ) ||
                strstr(
                    desc,
                    keywords[i]
                )) {

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

            if (strstr(
                    name,
                    keywords[i]
                ) ||
                strstr(
                    desc,
                    keywords[i]
                )) {

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
    gchar *status = NULL;


    gtk_tree_model_get(
        model,
        iter,
        0, &name,
        1, &version,
        2, &description,
        3, &repo,
        4, &status,
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


    /* Updates only */
    if (show_updates_only) {

        if (!status ||
            strcmp(
                status,
                "Update Available"
            ) != 0) {

            visible = FALSE;
        }
    }


    /* Search */
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
                name
                    ? name
                    : "",
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


    /* Repository */
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


    /* Category */
    if (visible) {

        PackageInfo temp;

        temp.name = name;
        temp.version = version;
        temp.desc = description;
        temp.repo = repo;

        temp.installed =
            status &&
            (
                strcmp(
                    status,
                    "Installed"
                ) == 0 ||

                strcmp(
                    status,
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
    g_free(status);


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
 * Real progress update on GTK main thread
 * ========================================================= */

static gboolean apply_progress_update(
    gpointer user_data)
{
    ProgressUpdate *update =
        user_data;


    if (!update)
        return G_SOURCE_REMOVE;


    if (!operation_running) {

        g_free(update->stage);
        g_free(update->package_name);
        g_free(update);

        return G_SOURCE_REMOVE;
    }


    int percent =
        update->percent;


    if (percent < 0)
        percent = 0;

    if (percent > 100)
        percent = 100;


    gtk_progress_bar_set_fraction(
        GTK_PROGRESS_BAR(progress_bar),
        percent / 100.0
    );


    gchar *bar_text =
        g_strdup_printf(
            "%d%%",
            percent
        );


    gtk_progress_bar_set_text(
        GTK_PROGRESS_BAR(progress_bar),
        bar_text
    );


    g_free(bar_text);


    gchar *label_text = NULL;


    if (update->package_name &&
        *update->package_name) {

        label_text =
            g_strdup_printf(
                "%s: %s",
                update->stage
                    ? update->stage
                    : "Working",
                update->package_name
            );

    } else {

        label_text =
            g_strdup(
                update->stage
                    ? update->stage
                    : "Working"
            );
    }


    gtk_label_set_text(
        GTK_LABEL(progress_label),
        label_text
    );


    g_free(label_text);

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


        if (info->installed &&
            backend_package_has_update(
                info->name
            )) {

            status = "Update Available";
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
            4, status,
            -1
        );
    }


    refresh_filter();
    update_action_buttons();
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


    GtkWindow *parent =
        GTK_WINDOW(
            gtk_widget_get_toplevel(
                install_btn
            )
        );


    set_operation_state(
        FALSE,
        NULL
    );


    if (success) {

        const char *message = NULL;


        switch (op->type) {

        case OP_INSTALL:
            message =
                "Package '%s' was installed successfully.";
            break;

        case OP_REMOVE:
            message =
                "Package '%s' was removed successfully.";
            break;

        case OP_REFRESH:
            message =
                "Package databases were refreshed successfully.";
            break;

        case OP_UPGRADE:
            message =
                "System upgrade completed successfully.";
            break;
        }


        if (op->type == OP_INSTALL ||
            op->type == OP_REMOVE) {

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

        const char *message = NULL;


        switch (op->type) {

        case OP_INSTALL:
            message =
                "Failed to install package '%s'.";
            break;

        case OP_REMOVE:
            message =
                "Failed to remove package '%s'.";
            break;

        case OP_REFRESH:
            message =
                "Failed to refresh package databases.";
            break;

        case OP_UPGRADE:
            message =
                "System upgrade failed.";
            break;
        }


        if (op->type == OP_INSTALL ||
            op->type == OP_REMOVE) {

            GtkWidget *dialog =
                gtk_message_dialog_new(
                    parent,
                    GTK_DIALOG_MODAL,
                    GTK_MESSAGE_ERROR,
                    GTK_BUTTONS_OK,
                    message,
                    op->package_name
                );


            gtk_dialog_run(
                GTK_DIALOG(dialog)
            );


            gtk_widget_destroy(dialog);

        } else {

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
        }
    }


    reload_packages();


    g_free(op->package_name);
    g_free(op);
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


    backend_set_progress_callback(
        backend_progress_callback,
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

    // تطبيق الستايل العصري للنافذة
    apply_custom_stylesheet();


    GtkWidget *window =
        gtk_window_new(
            GTK_WINDOW_TOPLEVEL
        );


    gtk_window_set_title(
        GTK_WINDOW(window),
        "Helwan Software Store"
    );


    gtk_window_set_default_size(
        GTK_WINDOW(window),
        1100,
        700
    );


    g_signal_connect(
        window,
        "destroy",
        G_CALLBACK(gtk_main_quit),
        NULL
    );


    /* Application icon */
    icon_manager_init(
        GTK_WINDOW(window),
        "data/hel-store.png"
    );


    /* Main layout */
    GtkWidget *vbox =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL,
            0
        );


    gtk_container_add(
        GTK_CONTAINER(window),
        vbox
    );


    /* Title */
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
        10
    );


    /* Controls */
    GtkWidget *controls =
        gtk_box_new(
            GTK_ORIENTATION_HORIZONTAL,
            10
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
        10
    );


    /* Search */
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


    /* Category */
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


    /* Repository */
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


    /* Refresh */
    refresh_btn =
        gtk_button_new_with_label(
            "Refresh"
        );


    g_signal_connect(
        refresh_btn,
        "clicked",
        G_CALLBACK(on_refresh_clicked),
        window
    );


    gtk_box_pack_start(
        GTK_BOX(controls),
        refresh_btn,
        FALSE,
        FALSE,
        0
    );


    /* Updates */
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


    /* Upgrade */
    upgrade_btn =
        gtk_button_new_with_label(
            "Upgrade System"
        );


    g_signal_connect(
        upgrade_btn,
        "clicked",
        G_CALLBACK(on_upgrade_clicked),
        window
    );


    gtk_box_pack_start(
        GTK_BOX(controls),
        upgrade_btn,
        FALSE,
        FALSE,
        0
    );


    /* =====================================================
     * Progress area
     * ===================================================== */

    progress_label =
        gtk_label_new(
            "Working..."
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
        2
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


    gtk_widget_set_margin_start(
        progress_bar,
        15
    );


    gtk_widget_set_margin_end(
        progress_bar,
        15
    );


    gtk_box_pack_start(
        GTK_BOX(vbox),
        progress_bar,
        FALSE,
        FALSE,
        5
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
        5
    );


    store_model =
        gtk_list_store_new(
            5,
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


    GtkCellRenderer *renderer =
        gtk_cell_renderer_text_new();


    gtk_tree_view_insert_column_with_attributes(
        GTK_TREE_VIEW(tree_view),
        -1,
        "Package Name",
        renderer,
        "text",
        0,
        NULL
    );


    gtk_tree_view_insert_column_with_attributes(
        GTK_TREE_VIEW(tree_view),
        -1,
        "Version",
        renderer,
        "text",
        1,
        NULL
    );


    gtk_tree_view_insert_column_with_attributes(
        GTK_TREE_VIEW(tree_view),
        -1,
        "Description",
        renderer,
        "text",
        2,
        NULL
    );


    gtk_tree_view_insert_column_with_attributes(
        GTK_TREE_VIEW(tree_view),
        -1,
        "Repository",
        renderer,
        "text",
        3,
        NULL
    );


    gtk_tree_view_insert_column_with_attributes(
        GTK_TREE_VIEW(tree_view),
        -1,
        "Status",
        renderer,
        "text",
        4,
        NULL
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
            10
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
        5
    );


    gtk_widget_set_margin_bottom(
        actions,
        5
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
    gtk_widget_set_name(install_btn, "install_btn");


    remove_btn =
        gtk_button_new_with_label(
            "Remove"
        );
    gtk_widget_set_name(remove_btn, "remove_btn");


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
        window
    );


    g_signal_connect(
        install_btn,
        "clicked",
        G_CALLBACK(on_install_clicked),
        window
    );


    g_signal_connect(
        remove_btn,
        "clicked",
        G_CALLBACK(on_remove_clicked),
        window
    );


    g_signal_connect(
        about_btn,
        "clicked",
        G_CALLBACK(on_about_clicked),
        window
    );


    /* Initial state */
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


    /* Load packages */
    reload_packages();


    gtk_widget_show_all(window);


    gtk_widget_hide(
        progress_label
    );


    gtk_widget_hide(
        progress_bar
    );


    gtk_main();


    backend_set_progress_callback(
        NULL,
        NULL
    );


    free_package_list();
}
