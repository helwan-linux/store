//core.h
#ifndef CORE_H
#define CORE_H

#include <gtk/gtk.h>
#include <alpm.h>

typedef struct {
    char *name;
    char *version;
    char *desc;
    char *repo;
    gboolean installed;
} PackageInfo;

#endif
