//async_loader.c
#include <pthread.h>
#include <gtk/gtk.h>

typedef struct {
    void (*worker)(void *data);
    void (*finished)(void *data);
    void *data;
} AsyncJob;

static gboolean finish_on_main_thread(gpointer user_data)
{
    AsyncJob *job = user_data;

    if (job && job->finished)
        job->finished(job->data);

    g_free(job);
    return G_SOURCE_REMOVE;
}

static void *worker_thread(void *arg)
{
    AsyncJob *job = arg;

    if (job && job->worker)
        job->worker(job->data);

    /*
     * The worker has finished.
     * Return to GTK main thread for UI updates.
     */
    g_idle_add(finish_on_main_thread, job);

    return NULL;
}

void async_run(void (*worker)(void *data),
               void (*finished)(void *data),
               void *data)
{
    pthread_t thread;

    AsyncJob *job = g_new0(AsyncJob, 1);

    if (!job)
        return;

    job->worker = worker;
    job->finished = finished;
    job->data = data;

    if (pthread_create(&thread, NULL, worker_thread, job) != 0) {
        g_free(job);
        return;
    }

    pthread_detach(thread);
}

