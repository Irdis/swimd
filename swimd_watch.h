#ifndef SWIMD_watch_H
#define SWIMD_watch_H

#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include "swimd_thread.h"

typedef struct SwimdWatchOwner SwimdWatchOwner;
typedef void (*swimd_watch_callback)(bool *ignore, SwimdWatchOwner *owner);
typedef void (*swimd_watch_notification_handler)(SwimdWatchOwner *owner);

typedef struct {
    int handle;
    int shutdown;

    pthread_t watch_loop;
    swimd_watch_callback callback;

    bool modification_occured;
    long long modification_time_ms;
    long long init_time_ms;
    bool modification_handled;
} SwimdWatch;

typedef struct SwimdWatchOwner {
    volatile bool watch_terminated;
    pthread_t notification_thread;
    pthread_mutex_t notification_lock;
    SwimdAutoResetEvent notification_are_raised;
    swimd_watch_notification_handler notification_handler;
    SwimdWatch *watch;
    void *argument;
} SwimdWatchOwner;


bool swimd_watch_init(SwimdWatchOwner *owner);
bool swimd_watch_track_path(SwimdWatchOwner *owner, const char* path);
bool swimd_watch_begin_tracking(SwimdWatchOwner *owner,
        swimd_watch_callback callback);
bool swimd_watch_end_tracking(SwimdWatchOwner *owner);

void swimd_watch_owner_init(SwimdWatchOwner *owner);
void swimd_watch_owner_begin_waiting(SwimdWatchOwner *owner,
        swimd_watch_notification_handler notification_handler);
void swimd_watch_owner_end(SwimdWatchOwner *owner);

#endif // SWIMD_THREAD_H
