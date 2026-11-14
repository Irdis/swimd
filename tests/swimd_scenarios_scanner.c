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

static const char *delta_request = "delta";
static SwimdScannerScores delta_expected[] = {
    { .score = 81, .str = "delta_8391.tmp" },
    { .score = 46, .str = "nebula_4729.dat" },
    { .score = 45, .str = "report_a4f92d.txt" },
    { .score = 43, .str = "swimd_test_6381.c" },
    { .score = 33, .str = "v2n8q5w3.data" },
    { .score = 30, .str = "session_e7c31b9a.xml" },
    { .score = 30, .str = "trace_2048.log" },
    { .score = 30, .str = "runtime_a2d8f5.xml" },
    { .score = 30, .str = "snapshot_7314.dat" },
    { .score = 29, .str = "worker_4f2a8d.c" },
};

static const char *snap_request = "snapshot_7314";
static SwimdScannerScores snap_expected[] = {
    { .score = 93, .str = "snapshot_7314.dat" },
    { .score = 43, .str = "session_e7c31b9a.xml" },
    { .score = 36, .str = "swimd_test_6381.c" },
    { .score = 23, .str = "index_91b7e3.db" },
    { .score = 23, .str = "temp_20261110_0142.log" },
};

static const char *suff_request = "a2d8f5";
static SwimdScannerScores suff_expected[] = {
    { .score = 76, .str = "runtime_a2d8f5.xml" },
    { .score = 42, .str = "v2n8q5w3.data" },
    { .score = 40, .str = "f8a2c91d.tmp" },
    { .score = 38, .str = "cache_5d8a2c1e.db" },
    { .score = 38, .str = "trace_2048.log" },
    { .score = 36, .str = "report_a4f92d.txt" },
    { .score = 28, .str = "nebula_4729.dat" },
    { .score = 26, .str = "parser_6e9f2b.c" },
    { .score = 25, .str = "swimd_test_6381.c" },
    { .score = 24, .str = "worker_4f2a8d.c" },
};

static const char *ffff_request = "ffffffffffff";
static SwimdScannerScores ffff_expected[] = {};


static void swimd_scenarios_print_input_result(const char *request,
        SwimdScannerProcessInputResult *input_result,
        const char *expected_scores_name) {
    printf("Result for: %s, expected_scores: %s\n", request, expected_scores_name);
    printf("Actual result:\n");
    for (int i = 0; i < input_result->items_length; i++) {
        SwimdScannerProcessInputResultItem item = input_result->items[i];
        printf("name = %s, score = %d, path = %s\n",
                item.name,
                item.score,
                item.path);
    }
}

static void swimd_scenarios_fail_input(const char *request,
        SwimdScannerProcessInputResult *input_result,
        const char *expected_scores_name,
        const char *reason,
        SwimdCunitTestResult *test_result) {
    swimd_scenarios_print_input_result(request, input_result, expected_scores_name);
    swimd_cunit_fail(test_result, "Mismatched item score of result output");
}

static bool swimd_scenarios_check_scores(const char *request,
        SwimdScannerProcessInputResult *input_result,
        const char *expected_scores_name,
        SwimdScannerScores *expected_scores,
        int expected_scores_length,
        SwimdCunitTestResult *test_result) {

    if (expected_scores_length != input_result->items_length) {
        swimd_scenarios_fail_input(request,
                input_result,
                expected_scores_name,
                "Mismatched item score of result output",
                test_result);
        return false;
    }

    for (int i = 0; i < input_result->items_length; i++) {
        bool score_mismatched = false;
        bool item_missing = true;

        SwimdScannerProcessInputResultItem item = input_result->items[i];
        for (int j = 0; j < input_result->items_length; j++) {
            SwimdScannerScores item_exp = expected_scores[j];
            if (strcmp(item.name, item_exp.str) == 0) {
                item_missing = false;
                score_mismatched = item.score != item_exp.score;
                break;
            }
        }
        if (score_mismatched) {
            swimd_scenarios_fail_input(request,
                    input_result,
                    expected_scores_name,
                    "Mismatched item score of result output",
                    test_result);
            return false;
        }
        if (item_missing) {
            swimd_scenarios_fail_input(request,
                    input_result,
                    expected_scores_name,
                    "Expected item is missing from result output",
                    test_result);
            return false;
        }
    }
    return true;
}

static void swimd_scenarios_file_scanning(SwimdCunitTestResult *test_result) {
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
    swimd_mre_wait(&scanner->scan_finished);

    SwimdScannerProcessInputResult input_result = {0};

    swimd_scanner_scan_process_input(delta_request, 10, &input_result, scanner);
    if (!swimd_scenarios_check_scores(delta_request,
                &input_result,
                "delta_expected",
                delta_expected,
                ARRAY_SIZE(delta_expected),
                test_result))
        goto cleanup;
    swimd_scanner_scan_process_input_free(&input_result);

    swimd_scanner_scan_process_input(snap_request, 10, &input_result, scanner);
    if (!swimd_scenarios_check_scores(snap_request,
                &input_result,
                "snap_expected",
                snap_expected,
                ARRAY_SIZE(snap_expected),
                test_result))
        goto cleanup;
    swimd_scanner_scan_process_input_free(&input_result);

    swimd_scanner_scan_process_input(suff_request, 10, &input_result, scanner);
    if (!swimd_scenarios_check_scores(suff_request,
                &input_result,
                "suff_expected",
                suff_expected,
                ARRAY_SIZE(suff_expected),
                test_result))
        goto cleanup;
    swimd_scanner_scan_process_input_free(&input_result);

    swimd_scanner_scan_process_input(ffff_request, 10, &input_result, scanner);
    if (!swimd_scenarios_check_scores(ffff_request,
                &input_result,
                "ffff_request",
                ffff_expected,
                ARRAY_SIZE(ffff_expected),
                test_result))
        goto cleanup;

cleanup:
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
