#include <stdbool.h>
#include <string.h>

#include <assert.h>
#include <stdio.h>
#include <float.h>

#include "swimd_thread.h"
#include "swimd_log.h"
#include "swimd_watch.h"
#include "swimd_scanner.h"

#ifdef _WIN32
    #include <direct.h>
    #include <stdlib.h>
#else
    #include <unistd.h>
    #include <limits.h>
#endif

#define NOB_IMPLEMENTATION
#undef ERROR
#include "nob.h"

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX_PATH_LENGTH 300

typedef struct  {
    bool interactive;
    char* needle;
    char* path;
    int top;
    bool git;
    int bench;
} SwimdCliArgs;

static void swimd_cli_get_current_dir(char *dir) {
#ifdef _WIN32
    _getcwd(dir, MAX_PATH_LENGTH);
#else
    getcwd(dir, MAX_PATH_LENGTH);
#endif
}

static void swimd_cli_print_usage(FILE *out) {
    fprintf(out, "swimd [args]:\n");
    fprintf(out, "    -n <str>:   needle\n");
    fprintf(out, "    -p <str>:   path, current directory if it is not specified\n");
    fprintf(out, "    -top <N>:   select top N, default 100\n");
    fprintf(out, "    -bench <N>: run benchmark for N'ms\n");
    fprintf(out, "    -git:       respect .gitignore\n");
    fprintf(out, "    -i:         interactive\n");
    fprintf(out, "    -?:         show this message\n");
}

static void swimd_cli_print_invalid_args(void) {
    fprintf(stderr, "Invalid argument list\n");
    fprintf(stderr, "\n");
    swimd_cli_print_usage(stderr);
}

static bool swimd_cli_parse_args(SwimdCliArgs *res, int argc, char **argv) {
    int ind = 1;
    while (ind < argc) {
        char *arg = argv[ind];
        if (strcmp(arg, "-n") == 0) {
            ind++;
            if (ind == argc) {
                swimd_cli_print_invalid_args();
                return false;
            }
            res->needle = argv[ind];
            ind++;
        } else if (strcmp(arg, "-p") == 0) {
            ind++;
            if (ind == argc) {
                swimd_cli_print_invalid_args();
                return false;
            }
            res->path = argv[ind];
            ind++;
        } else if (strcmp(arg, "-top") == 0) {
            ind++;
            if (ind == argc) {
                swimd_cli_print_invalid_args();
                return false;
            }
            res->top = atoi(argv[ind]);
            ind++;
        } else if (strcmp(arg, "-bench") == 0) {
            ind++;
            if (ind == argc) {
                swimd_cli_print_invalid_args();
                return false;
            }
            res->bench = atoi(argv[ind]);
            ind++;
        } else if (strcmp(arg, "-git") == 0) {
            ind++;
            res->git = true;
        } else if (strcmp(arg, "-i") == 0) {
            ind++;
            res->interactive = true;
        } else if (strcmp(arg, "-?") == 0) {
            swimd_cli_print_usage(stdout);
            return false;
        } else {
            swimd_cli_print_invalid_args();
            return false;
        }
    }

    if (res->path == NULL) {
        res->path = malloc(MAX_PATH_LENGTH);
        swimd_cli_get_current_dir(res->path);
    }

    if (res->top <= 0) {
        res->top = 100;
    }

    if (!res->interactive && res->needle == NULL) {
        swimd_cli_print_invalid_args();
        return false;
    }

    return true;
}

static void swimd_cli_run_bench(SwimdScanner *scanner, SwimdCliArgs *args, double begin) {
    double end = swimd_scanner_now_us();

    double ingestion = (end - begin) / 1000;
    double iteration_min = DBL_MAX;
    double iteration_max = 0;

    double iteration_tot = 0;
    int iteration_count = 0;

    while (1) {
        double input_beg = swimd_scanner_now_us();
        SwimdScannerProcessInputResult input_result = {0};
        swimd_scanner_scan_process_input(args->needle,
                args->top,
                &input_result,
                scanner);
        swimd_scanner_scan_process_input_free(&input_result);
        double input_end = swimd_scanner_now_us();

        double iteration_time = input_end - input_beg;
        iteration_min = MIN(iteration_min, iteration_time);
        iteration_max = MAX(iteration_max, iteration_time);
        iteration_tot += iteration_time;
        iteration_count++;

        double total = (input_end - begin) / 1000;

        if (total > args->bench) {
            double iteration_avg = iteration_tot / 1000 / iteration_count;
            iteration_min /= 1000;
            iteration_max /= 1000;
            printf("%d iterations, avg: %.2fms, min: %.2fms, max: %.2fms, total: %.2fms, ingestion: %.2fms\n",
                    iteration_count, iteration_avg, iteration_min, iteration_max, total, ingestion);
            break;
        }
    }
}

static void swimd_cli_run_single(SwimdCliArgs *args) {
    double begin, end;
    if (args->bench > 0) {
        begin = swimd_scanner_now_us();
    }
    swimd_scanner_global_init("swimd.log");
    swimd_scanner_scanners_glob_init();

    swimd_scanner_scanners_setup_path(args->path);
    SwimdScanner *scanner = args->git
        ? &swimd_scanner_scanners[SCANNER_GIT]
        : &swimd_scanner_scanners[SCANNER_FILES];
    swimd_mre_wait(&scanner->scan_finished);

    if (args->bench == 0) {
        SwimdScannerProcessInputResult input_result = {0};
        swimd_scanner_scan_process_input(args->needle,
                args->top,
                &input_result,
                scanner);
        for (int i = 0; i < input_result.items_length; i++) {
            SwimdScannerProcessInputResultItem item = input_result.items[i];
            printf("%s\n", item.path);
        }
        swimd_scanner_scan_process_input_free(&input_result);
    } else {
        swimd_cli_run_bench(scanner, args, begin);
    }

    swimd_scanner_shutdown();
}

int main(int argc, char **argv) {
    SwimdCliArgs args = {0};
    if (!swimd_cli_parse_args(&args, argc, argv))
        return 1;
    swimd_cli_run_single(&args);
    return 0;
}
