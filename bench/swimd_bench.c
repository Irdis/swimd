#include <stdbool.h>
#include <string.h>

#include <assert.h>
#include <stdio.h>

#include "../swimd_thread.h"
#include "../swimd_log.h"
#include "../swimd_scanner.h"

#define NOB_IMPLEMENTATION
#undef ERROR
#include "../nob.h"

static void swimd_scanner_scenario_bench(void) {
    const char *needle = "linux";
    swimd_scanner_global_init("swimd.log");
    swimd_scanner_scanners_glob_init();

#ifdef _WIN32
    swimd_scanner_scanners_setup_path("c:\\Projects\\experiments");
#else
    swimd_scanner_scanners_setup_path("/home/ivan/Projects/chromium");
#endif

    SwimdScanner *scanner = &swimd_scanner_scanners[SCANNER_FILES];

    SwimdScannerProcessInputResult input_result = {0};

    while (1) {
        swimd_scanner_scan_process_input(needle, 10, &input_result, scanner);
        if (input_result.scan_in_progress) {
            swimd_scanner_scan_process_input_free(&input_result);
            swimd_thread_sleep(50);
            continue;
        }
        break;
    }
    swimd_scanner_scan_process_input_free(&input_result);
    memset(&swimd_scanner_phase_times, 0, sizeof(SwimdScannerPhaseTimes));

    int it_count = 50;

    while(1) {

        double begin = swimd_scanner_now_us();

        for (int i = 0; i < it_count; i++) {
            swimd_scanner_scan_process_input(needle, 100, &input_result, scanner);
            swimd_scanner_scan_process_input_free(&input_result);
        }

        double end = swimd_scanner_now_us();
        double time_spent = (end - begin) / 1000;
        printf("time spent %fms\n", time_spent / it_count);
        SwimdScannerPhaseTimes *pt = &swimd_scanner_phase_times;
        printf("prefilter & scores %.1fus, top %.1fus\n",
                pt->prefilter_and_scores_us / pt->count,
                pt->top_us / pt->count);
        memset(pt, 0, sizeof(SwimdScannerPhaseTimes));
        getchar();
    }
    swimd_scanner_shutdown();

    printf("Over\n");
}

int main() {
    swimd_scanner_scenario_bench();
    return 0;
}
