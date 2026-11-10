#include "swimd_cunit.h"
#include "../nob.h"
#include "../swimd_log.h"
#include "../swimd_scanner.h"

#define TEST_OUT SWIMD_CUNIT_TEST_OUT
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

typedef struct {
    const char *str;
    int score;
} SwimdScannerScores;

static const char *file_names[] = {
    "f8a2c91d.tmp",
    "nebula_4729.dat",
    "config_b71e4f.json",
    "temp_20261110_0142.log",
    "x9m3k7p2.bin",
    "cache_5d8a2c1e.db",
    "swimd_test_6381.c",
    "report_a4f92d.txt",
    "v2n8q5w3.data",
    "session_e7c31b9a.xml",
    "delta_8391.tmp",
    "worker_4f2a8d.c",
    "index_91b7e3.db",
    "omega_5726.json",
    "buffer_c8d3a1.bin",
    "trace_2048.log",
    "parser_6e9f2b.c",
    "snapshot_7314.dat",
    "runtime_a2d8f5.xml",
    "module_49c7e1.h"
};

static SwimdScannerScores delta_expexted[] = {
    { .str = "delta_8391.tmp", .score = 81 },
    { .str = "nebula_4729.dat", .score = 46 },
    { .str = "report_a4f92d.txt", .score = 45 },
    { .str = "swimd_test_6381.c", .score = 43 },
    { .str = "v2n8q5w3.data", .score = 33 },
    { .str = "session_e7c31b9a.xml", .score = 30 },
    { .str = "trace_2048.log", .score = 30 },
    { .str = "runtime_a2d8f5.xml", .score = 30 },
    { .str = "snapshot_7314.dat", .score = 30 },
    { .str = "worker_4f2a8d.c", .score = 29 },
};

static void swimd_scenarios_file_scanning(SwimdCunitTestResult *result) {
    swimd_scanner_global_init("swimd.log");
    swimd_scanner_scanners_glob_init();

    nob_mkdir_if_not_exists(TEST_OUT"swimd_scenarios_file_scanning");

    int files_length = ARRAY_SIZE(file_names);

    Nob_String_Builder sb = {0};
    for (int i = 0; i < files_length; i++) {
        const char *file_name = file_names[i];
        nob_sb_appendf(&sb, TEST_OUT"swimd_scenarios_file_scanning/%s", file_name);
        nob_sb_append_null(&sb);
        nob_write_entire_file(sb.items, NULL, 0);
        sb.count = 0;
    }
    swimd_scanner_scanners_setup_path(TEST_OUT"swimd_scenarios_file_scanning");

    SwimdScanner *scanner = &swimd_scanner_scanners[SCANNER_FILES];
    SwimdScannerProcessInputResult input_result = {0};

    while (1) {
        swimd_scanner_scan_process_input("delta", 10, &input_result, scanner);
        if (input_result.scan_in_progress) {
            swimd_scanner_scan_process_input_free(&input_result);
            swimd_thread_sleep(50);
            continue;
        }
        break;
    }

    int delta_length = ARRAY_SIZE(delta_expexted);

    if (delta_length != input_result.items_length) {
        swimd_cunit_fail(result, "Mismatched length of result output");
    } else {
        for (int i = 0; i < input_result.items_length; i++) {
            bool score_mismatched = false;
            bool item_missing = true;

            SwimdScannerProcessInputResultItem item = input_result.items[i];
            for (int j = 0; j < input_result.items_length; j++) {
                SwimdScannerScores item_exp = delta_expexted[j];
                if (strcmp(item.name, item_exp.str) == 0) {
                    item_missing = false;
                    score_mismatched = item.score != item_exp.score;
                    break;
                }
            }
            if (score_mismatched) {
                swimd_cunit_fail(result, "Mismatched item score of result output");
                break;
            }
            if (item_missing) {
                swimd_cunit_fail(result, "Expected item is missing from result output");
                break;
            }
        }
    }

    swimd_scanner_scan_process_input_free(&input_result);
    swimd_scanner_shutdown();

    for (int i = 0; i < files_length; i++) {
        const char *file_name = file_names[i];
        nob_sb_appendf(&sb, TEST_OUT"swimd_scenarios_file_scanning/%s", file_name);
        nob_sb_append_null(&sb);
        nob_delete_file(sb.items);
        sb.count = 0;
    }
    nob_delete_file(TEST_OUT"swimd_scenarios_file_scanning");

    nob_sb_free(sb);
}
