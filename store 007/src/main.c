//main.c
#include "backend_alpm.h"
#include "ui.h"
#include <stdio.h>
#include <gtk/gtk.h>

int main(int argc, char *argv[]) {
    // تهيئة GTK أولاً عشان نقدر نتحكم في الأيقونة الافتراضية
    gtk_init(&argc, &argv);

    if (!backend_init()) {
        fprintf(stderr, "Failed to initialize alpm backend.\n");
        return 1;
    }

    // تعيين الأيقونة الافتراضية للتطبيق من مسار الأصول لكي تظهر في التاسك بار
    GError *error = NULL;
    if (!gtk_window_set_default_icon_from_file("data/hel-store.png", &error)) {
        fprintf(stderr, "Warning: Failed to load default app icon: %s\n", error ? error->message : "Unknown error");
        if (error) g_error_free(error);
    }

    ui_window_show(argc, argv);

    backend_cleanup();
    return 0;
}
