#include "swimd_cunit.h"
#include "../nob.h"
#include "../swimd_watch.h"
#include "../swimd_log.h"
#define TEST_OUT SWIMD_CUNIT_TEST_OUT

static bool swimd_watch_notification_received = false;

static void swimd_watch_demo_callback(bool *ignore, SwimdWatchOwner *owner) {
    *ignore = false;
    swimd_are_set(&owner->notification_are_raised);
}

void swimd_watch_owner_notification_demo_handler(SwimdWatchOwner *owner) {
    swimd_watch_notification_received = true;
    swimd_watch_end_tracking(owner);
    owner->watch_terminated = true;
}

void swimd_scenarios_watch(SwimdCunitTestResult *result) {
    swimd_log_init("watch.log");
    SwimdWatchOwner owner = {0};

    swimd_watch_start_handle_events_from_ms = 0;
    swimd_watch_silent_window_ms = 0;
    swimd_watch_poll_interval_ms = 10;

    swimd_watch_owner_init(&owner);
    swimd_watch_owner_begin_waiting(&owner,
            swimd_watch_owner_notification_demo_handler);

    if (!swimd_watch_init(&owner)) {
        goto cleanup;
    }
    nob_mkdir_if_not_exists(TEST_OUT"swimd_scenarios_watch");
    if (!swimd_watch_track_path(&owner, TEST_OUT"swimd_scenarios_watch", true)) {
        goto cleanup;
    }
    swimd_watch_begin_tracking(&owner, swimd_watch_demo_callback);

    swimd_thread_sleep(50);
    nob_write_entire_file(TEST_OUT"swimd_scenarios_watch/1", NULL, 0);
    swimd_thread_sleep(50);

cleanup:
    swimd_watch_owner_end(&owner);

    if (!swimd_watch_notification_received) {
        swimd_cunit_fail(result, "Notification hasn't been received");
    }

    nob_delete_file(TEST_OUT"swimd_scenarios_watch/1");
    nob_delete_file(TEST_OUT"swimd_scenarios_watch");

    swimd_log_free();
}
