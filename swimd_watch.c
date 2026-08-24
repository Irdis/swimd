#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include "swimd_log.h"
#include "swimd_thread.h"
#include "swimd_watch.h"

#define SWIMD_WATCH_START_HANDLE_EVENTS_FROM_MS 3000
#define SWIMD_WATCH_SILENT_WINDOW_MS 3000
#define SWIMD_WATCH_POLL_INTERVAL_MS 1000

typedef struct inotify_event inotify_event;
static void* swimd_watch_watch_loop(void *arg);

static long long swimd_watch_get_current_time() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

bool swimd_watch_init(SwimdWatchOwner *owner) {
    SwimdWatch *watch = malloc(sizeof(SwimdWatch));
    owner->watch = watch;

    watch->handle = inotify_init1(IN_NONBLOCK);
    if (watch->handle == -1) {
        swimd_log_append(SWIMD_ERR, "Unable to initialize watch handle inotify_init");
        return false;
    }

    watch->shutdown = eventfd(0, 0);
    if (watch->shutdown == -1) {
        close(watch->handle);
        watch->handle = -1;
        swimd_log_append(SWIMD_ERR, "Unable to initialize shutdown handle eventfd");
        return false;
    }

    watch->modification_occured = false;
    watch->modification_time_ms = 0;
    watch->init_time_ms = swimd_watch_get_current_time();
    watch->modification_handled = false;

    return true;
}

bool swimd_watch_track_path(SwimdWatchOwner *owner, const char* path) {
    SwimdWatch *watch = owner->watch;
    if (watch->handle == -1)
        return false;

    int wfd = inotify_add_watch(watch->handle, path,
            IN_CREATE |
            IN_DELETE |
            IN_DELETE_SELF |
            IN_MOVE_SELF |
            IN_MOVE |
            IN_ONLYDIR);

    if (wfd == -1) {
        swimd_log_append(SWIMD_ERR, "Unable to inotify_add_watch: %s", path);
        return false;
    }
    return true;
}

bool swimd_watch_begin_tracking(SwimdWatchOwner *owner,
        swimd_watch_callback callback) {
    SwimdWatch *watch = owner->watch;

    if (watch->handle == -1)
        return false;

    watch->callback = callback;
    swimd_thread_create(&watch->watch_loop,
            swimd_watch_watch_loop,
            owner);

    return true;
}

static void swimd_watch_send_shutdown(SwimdWatch *watch) {
    uint64_t value = 1;
    write(watch->shutdown, &value, sizeof(value));
}

bool swimd_watch_end_tracking(SwimdWatchOwner *owner) {
    SwimdWatch *watch = owner->watch;
    if (watch->handle == -1)
        goto cleanup;
    swimd_watch_send_shutdown(watch);
    swimd_thread_join(&watch->watch_loop);

    close(watch->handle);
    close(watch->shutdown);
    swimd_thread_close(&watch->watch_loop);

cleanup:
    free(watch);
    return true;
}

static void* swimd_watch_watch_loop(void *arg) {
    SwimdWatchOwner *owner = (SwimdWatchOwner*)arg;
    SwimdWatch* watch = owner->watch;
    swimd_log_append(SWIMD_INFO, "Watch loop start");

    const int fds_len = 2;
    struct pollfd fds[] = {
        { .fd = watch->handle, .events = POLLIN },
        { .fd = watch->shutdown, .events = POLLIN }
    };

    char buf[4096];
    ssize_t size;

    while (1) {
        int ret = poll(fds, fds_len, SWIMD_WATCH_POLL_INTERVAL_MS);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        if (ret == 0) { // timer event
            if (watch->modification_handled) {
                continue; // waiting for shutdown
            }

            if (watch->modification_occured) {
                long long cur_time = swimd_watch_get_current_time();
                if (cur_time - watch->modification_time_ms < SWIMD_WATCH_SILENT_WINDOW_MS) {
                    continue;
                }
                bool ignore = false;
                watch->callback(&ignore, owner);
                if (ignore) {
                    watch->modification_occured = false;
                    watch->modification_time_ms = 0;
                } else  {
                    watch->modification_handled = true;
                }
            }
            continue;
        }

        if (fds[1].revents & POLLIN) {
            uint64_t value;
            read(watch->shutdown, &value, sizeof(value));
            break;
        }

        if (watch->modification_handled) {
            continue; // waiting for shutdown
        }

        if (fds[0].revents & POLLIN) {
            size = read(watch->handle, buf, sizeof(buf));
            if (size <= 0)
                break;

            long long cur_time = swimd_watch_get_current_time();
            if (cur_time - watch->init_time_ms < SWIMD_WATCH_START_HANDLE_EVENTS_FROM_MS) {
                continue;
            }
            watch->modification_occured = true;
            watch->modification_time_ms = cur_time;
        }
    }
    swimd_log_append(SWIMD_INFO, "Watch loop ended");
    return NULL;
}

static void* swimd_watch_notification_loop(void *arg) {
    SwimdWatchOwner *owner = (SwimdWatchOwner*)arg;
    swimd_are_wait(&owner->notification_are_raised);

    swimd_crit_lock(&owner->notification_lock);
    if (!owner->watch_terminated) {
        owner->notification_handler(owner);
    }
    swimd_crit_unlock(&owner->notification_lock);
    return NULL;
}

void swimd_watch_owner_init(SwimdWatchOwner *owner) {
    swimd_crit_init(&owner->notification_lock);
    swimd_are_init(&owner->notification_are_raised, false);
}

void swimd_watch_owner_begin_waiting(SwimdWatchOwner *owner,
        swimd_watch_notification_handler notification_handler) {
    owner->notification_handler = notification_handler;

    swimd_thread_create(&owner->notification_thread,
            swimd_watch_notification_loop,
            owner);
}

void swimd_watch_owner_end(SwimdWatchOwner *owner) {
    swimd_crit_lock(&owner->notification_lock);

    if (!owner->watch_terminated) {
        swimd_watch_end_tracking(owner);
        owner->watch_terminated = true;
    }

    swimd_crit_unlock(&owner->notification_lock);

    swimd_are_set(&owner->notification_are_raised);
    swimd_thread_join(&owner->notification_thread);

    swimd_crit_close(&owner->notification_lock);
    swimd_are_close(&owner->notification_are_raised);
    swimd_thread_close(&owner->notification_thread);
}
