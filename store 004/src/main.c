//main.c
#include "backend_alpm.h"
#include "ui.h"
#include <stdio.h>

int main(int argc, char *argv[]) {
    if (!backend_init()) {
        fprintf(stderr, "Failed to initialize alpm backend.\n");
        return 1;
    }

    ui_window_show(argc, argv);

    backend_cleanup();
    return 0;
}
