#include <stdbool.h>
#include <string.h>

#include <assert.h>
#include <stdio.h>

#include "swimd_thread.h"
#include "swimd_log.h"
#include "swimd_watch.h"
#include "swimd_scanner.h"

#define NOB_IMPLEMENTATION
#undef ERROR
#include "nob.h"

static void swimd_scanner_scenario_reinit(void) {
    for (;;) {
        swimd_scanner_global_init("swimd.log");

        swimd_scanner_scanners_glob_init();

        SwimdScanner *scanner = &swimd_scanner_scanners[SCANNER_FILES];

#ifdef _WIN32
        swimd_scanner_scanners_setup_path("c:\\projects\\tmp_swimd");
#else
        swimd_scanner_scanners_setup_path("/home/ivan/Projects/tmp_swimd");
#endif

        SwimdScannerProcessInputResult result = {0};
        swimd_scanner_scan_process_input("swimd", 10, &result, scanner);

        if (result.scan_in_progress) {
            printf("Scanning %d\n", result.scanned_items_count);
        } else {
            for (int i = 0; i < result.items_length; i++) {
                SwimdScannerProcessInputResultItem item = result.items[i];

                printf("name = %s, score = %d, path = %s\n",
                        item.name,
                        item.score,
                        item.path);
            }
        }

        swimd_scanner_scan_process_input_free(&result);
        getchar();
        swimd_scanner_shutdown();
    }

    printf("Over\n");
}

static void swimd_scanner_scenario_bench(void) {
    swimd_scanner_global_init("swimd.log");
    swimd_scanner_scanners_glob_init();

    SwimdScanner *scanner = &swimd_scanner_scanners[SCANNER_FILES];

    for (int i = 0; i < 10; i++) {

        swimd_crit_lock(&swimd_scanner_scanners_request_lock);
#ifdef _WIN32
        swimd_scanner_scanners_setup_path("c:\\Projects\\experiments");
#else
        swimd_scanner_scanners_setup_path("/home/ivan/Projects/chromium");
#endif
        swimd_crit_unlock(&swimd_scanner_scanners_request_lock);

        int it_count = 50;

        while(1) {

            double begin = swimd_scanner_now_us();

            for (int i = 0; i < it_count; i++) {
                SwimdScannerProcessInputResult result = {0};
                swimd_scanner_scan_process_input("linux", 10, &result, scanner);
                swimd_scanner_scan_process_input_free(&result);
            }

            double end = swimd_scanner_now_us();
            double time_spent = (end - begin) / 1000;
            printf("time spent %fms\n", time_spent / it_count);
            SwimdScannerPhaseTimes *pt = &swimd_scanner_phase_times;
            printf("prefilter & scores %.1fus, top %.1fus\n",
                    pt->prefilter_and_scores_us / pt->count,
                    pt->top_us / pt->count);
            memset(pt, 0, sizeof(SwimdScannerPhaseTimes));
        }
    }
    swimd_scanner_shutdown();

    printf("Over\n");
}

static void swimd_scanner_scenario_scanning(void) {
    swimd_scanner_global_init("swimd.log");
    swimd_scanner_scanners_glob_init();

    SwimdScanner *scanner = &swimd_scanner_scanners[SCANNER_FILES];

    for (int i = 0; i < 10; i++) {

        swimd_crit_lock(&swimd_scanner_scanners_request_lock);
#ifdef _WIN32
        swimd_scanner_scanners_setup_path("c:\\Projects\\experiments");
#else
        swimd_scanner_scanners_setup_path("/home/ivan/Projects/chromium");
#endif
        swimd_crit_unlock(&swimd_scanner_scanners_request_lock);

        int count = 0;

        while(1) {

            double begin = swimd_scanner_now_us();

            SwimdScannerProcessInputResult result = {0};
            swimd_scanner_scan_process_input("sound", 10, &result, scanner);

            double end = swimd_scanner_now_us();

            double time_spent = (end - begin) / 1000;
            printf("time spent %fms\n", time_spent);

            SwimdScannerPhaseTimes *pt = &swimd_scanner_phase_times;
            printf("prefilter & scores %.1fus, top %.1fus\n",
                    pt->prefilter_and_scores_us / pt->count,
                    pt->top_us / pt->count);
            memset(pt, 0, sizeof(SwimdScannerPhaseTimes));

            if (result.scan_in_progress) {
                printf("Scanning %d\n", result.scanned_items_count);
            } else {
                if (result.items_length == 0) {
                    printf("nothing matches\n");
                }

                for (int i = 0; i < result.items_length; i++) {
                    SwimdScannerProcessInputResultItem item = result.items[i];

                    printf("name = %s, score = %d, path = %s\n",
                            item.name,
                            item.score,
                            item.path);
                }
            }

            swimd_scanner_scan_process_input_free(&result);
            count++;
            if (count % 20 == 0)
                getchar();
        }
    }
    swimd_scanner_shutdown();

    printf("Over\n");
}

static int counter = 0;

static void swimd_scanner_watch_demo_callback(bool *ignore, SwimdWatchOwner *owner) {
    *ignore = true;
    counter++;
    if (counter == 3) {
        *ignore = false;
        swimd_are_set(&owner->notification_are_raised);
    } else {
        printf("event ignored\n");
    }
}

void swimd_scanner_watch_owner_notification_demo_handler(SwimdWatchOwner *owner) {
    swimd_watch_end_tracking(owner);
    owner->watch_terminated = true;
}

int swimd_scanner_scenario_watch(void) {
    swimd_log_init("watch.log");
    SwimdWatchOwner owner = {0};

    swimd_watch_owner_init(&owner);
    swimd_watch_owner_begin_waiting(&owner,
            swimd_scanner_watch_owner_notification_demo_handler);

    if (!swimd_watch_init(&owner)) {
        goto cleanup;
    }
#ifndef _WIN32
    if (!swimd_watch_track_path(&owner,
                "/home/ivan/Projects/experiments", true)) {
        goto cleanup;
    }
#else
    if (!swimd_watch_track_path(&owner,
                "c:\\Projects\\experiments", true)) {
        goto cleanup;
    }
#endif

    swimd_watch_begin_tracking(&owner,
            swimd_scanner_watch_demo_callback);

    getchar();

cleanup:
    swimd_watch_owner_end(&owner);

    swimd_log_free();
    return 0;
}

int main() {
    // swimd_scanner_scenario_bench();
    swimd_scanner_scenario_scanning();
    // swimd_scanner_scenario_reinit();
    // swimd_scanner_scenario_watch();

    return 0;
}
