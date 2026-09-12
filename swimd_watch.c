#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#ifdef _WIN32
    // todo: win
#else
    #include <unistd.h>
    #include <poll.h>
    #include <sys/eventfd.h>
    #include <sys/inotify.h>
#endif
#include "swimd_log.h"
#include "swimd_thread.h"
#include "swimd_watch.h"

#define SWIMD_WATCH_START_HANDLE_EVENTS_FROM_MS 3000
#define SWIMD_WATCH_SILENT_WINDOW_MS 3000
#define SWIMD_WATCH_POLL_INTERVAL_MS 1000

#ifndef _WIN32
#define INVALID_HANDLE_VALUE -1
#endif

void swimd_watch_notification_loop_impl(void *arg);

#ifdef _WIN32

static long long swimd_watch_get_current_time()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);

    ULARGE_INTEGER time;
    time.LowPart = ft.dwLowDateTime;
    time.HighPart = ft.dwHighDateTime;

    return (long long)(time.QuadPart / 10000);
}

bool swimd_watch_init(SwimdWatchOwner *owner) {
    SwimdWatch *watch = malloc(sizeof(SwimdWatch));
    owner->watch_terminated = false;
    owner->watch = watch;
    owner->has_watch = true;

    watch->handle = INVALID_HANDLE_VALUE;
    watch->shutdown = INVALID_HANDLE_VALUE;

    watch->modification_occured = false;
    watch->modification_time_ms = 0;
    watch->init_time_ms = swimd_watch_get_current_time();
    watch->modification_handled = false;

    return true;
}

bool swimd_watch_track_path(SwimdWatchOwner *owner, const char* path, bool root) {
    if (!root) {
        return true;
    }
    SwimdWatch *watch = owner->watch;
    watch->handle = CreateFileA(
            path,
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            NULL);

    if (watch->handle == INVALID_HANDLE_VALUE) {
        swimd_log_append(SWIMD_ERR, "Unable to initialize watch handle CreateFileA");
        return false;
    }
    watch->shutdown = CreateEventW(NULL, true, false, NULL);
    if (watch->shutdown == INVALID_HANDLE_VALUE) {
        CloseHandle(watch->handle);
        watch->handle = INVALID_HANDLE_VALUE;
        swimd_log_append(SWIMD_ERR, "Unable to initialize shutdown handle CreateEventW");
        return false;
    }

    return true;
}

static DWORD WINAPI swimd_watch_watch_loop(LPVOID arg) {
    SwimdWatchOwner *owner = (SwimdWatchOwner*)arg;
    SwimdWatch *watch = owner->watch;
    swimd_log_append(SWIMD_INFO, "Watch loop start");

    BYTE buffer[4 * 1024];
    OVERLAPPED overlapped = {0};

    overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    const int handles_count = 2;
    HANDLE handles[] = {
        overlapped.hEvent,
        watch->shutdown
    };

    for (;;) {
        ResetEvent(overlapped.hEvent);

        BOOL ok = ReadDirectoryChangesW(
            watch->handle,
            buffer,
            sizeof(buffer),
            TRUE, // recursive
            FILE_NOTIFY_CHANGE_FILE_NAME |
            FILE_NOTIFY_CHANGE_DIR_NAME,
            NULL,
            &overlapped,
            NULL
        );

        if (!ok) {
            swimd_log_append(SWIMD_ERR, "ReadDirectoryChangesW failed: %lu\n", GetLastError());
            break;
        }

        DWORD result = WaitForMultipleObjects(
            handles_count,
            handles,
            FALSE,
            SWIMD_WATCH_POLL_INTERVAL_MS
        );

        if (result == WAIT_TIMEOUT) {
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

        if (result == WAIT_OBJECT_0 + 1) { // shutdown
            break;
        }

        if (watch->modification_handled) {
            continue; // waiting for shutdown
        }

        if (result == WAIT_OBJECT_0) {
            DWORD bytes;

            if (!GetOverlappedResult(watch->handle,
                        &overlapped, &bytes, FALSE)) {
                swimd_log_append(SWIMD_ERR, "GetOverlappedResult failed: %lu\n", GetLastError());
                break;
            }

            if (bytes <= 0) {
                continue;
            }

            long long cur_time = swimd_watch_get_current_time();
            if (cur_time - watch->init_time_ms < SWIMD_WATCH_START_HANDLE_EVENTS_FROM_MS) {
                continue;
            }
            watch->modification_occured = true;
            watch->modification_time_ms = cur_time;
        }
    }

    CloseHandle(overlapped.hEvent);

    swimd_log_append(SWIMD_INFO, "Watch loop ended");
    return 0;
}

static DWORD WINAPI swimd_watch_notification_loop(LPVOID arg) {
    swimd_watch_notification_loop_impl(arg);
    return 0;
}

static void swimd_watch_send_shutdown(SwimdWatch *watch) {
    SetEvent(watch->shutdown);
}

#else

typedef struct inotify_event inotify_event;
static void* swimd_watch_watch_loop(void *arg);

static long long swimd_watch_get_current_time() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

bool swimd_watch_init(SwimdWatchOwner *owner) {
    SwimdWatch *watch = malloc(sizeof(SwimdWatch));
    owner->watch_terminated = false;
    owner->watch = watch;
    owner->has_watch = true;

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

bool swimd_watch_track_path(SwimdWatchOwner *owner, const char* path, bool root) {
    (void)root;

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

static void swimd_watch_send_shutdown(SwimdWatch *watch) {
    uint64_t value = 1;
    write(watch->shutdown, &value, sizeof(value));
}

static void* swimd_watch_watch_loop(void *arg) {
    SwimdWatchOwner *owner = (SwimdWatchOwner*)arg;
    SwimdWatch *watch = owner->watch;
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

        if (fds[1].revents & POLLIN) { // shutdown
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
    swimd_watch_notification_loop_impl(arg);
    return NULL;
}

#endif //_WIN32

bool swimd_watch_begin_tracking(SwimdWatchOwner *owner,
        swimd_watch_callback callback) {
    SwimdWatch *watch = owner->watch;

    if (watch->handle == INVALID_HANDLE_VALUE)
        return false;

    watch->callback = callback;
    swimd_thread_create(&watch->watch_loop,
            swimd_watch_watch_loop,
            owner);

    return true;
}

bool swimd_watch_end_tracking(SwimdWatchOwner *owner) {
    SwimdWatch *watch = owner->watch;
    if (watch->handle == INVALID_HANDLE_VALUE)
        goto cleanup;
    swimd_watch_send_shutdown(watch);
    swimd_thread_join(&watch->watch_loop);

#ifdef _WIN32
    CloseHandle(watch->handle);
    CloseHandle(watch->shutdown);
#else
    close(watch->handle);
    close(watch->shutdown);
#endif
    swimd_thread_close(&watch->watch_loop);

    watch->handle = INVALID_HANDLE_VALUE;
    watch->shutdown = INVALID_HANDLE_VALUE;
cleanup:
    free(watch);
    owner->has_watch = false;
    return true;
}

static void swimd_watch_notification_loop_impl(void *arg) {
    SwimdWatchOwner *owner = (SwimdWatchOwner*)arg;
    while (1) {
        swimd_are_wait(&owner->notification_are_raised);
        if (owner->owner_terminating) {
            break;
        }

        swimd_crit_lock(&owner->notification_lock);
        if (!owner->watch_terminated) {
            owner->notification_handler(owner);
        }
        swimd_crit_unlock(&owner->notification_lock);
    }
}

void swimd_watch_owner_init(SwimdWatchOwner *owner) {
    owner->owner_terminating = false;
    owner->has_watch = false;
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

    owner->owner_terminating = true;
    swimd_are_set(&owner->notification_are_raised);
    swimd_thread_join(&owner->notification_thread);

    swimd_crit_close(&owner->notification_lock);
    swimd_are_close(&owner->notification_are_raised);
    swimd_thread_close(&owner->notification_thread);
}
