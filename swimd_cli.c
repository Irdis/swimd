#include <stdbool.h>
#include <string.h>

#include <assert.h>
#include <stdio.h>

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

#define MAX_PATH_LENGTH 300

typedef struct  {
    bool interactive;
    char* needle;
    char* path;
    int top;
    bool git;
    bool print_stat;
} SwimdCliArgs;

void swimd_cli_get_current_dir(char *dir) {
#ifdef _WIN32
    _getcwd(dir, MAX_PATH_LENGTH);
#else
    getcwd(dir, MAX_PATH_LENGTH);
#endif
}

void swimd_cli_print_usage(FILE *out) {
    fprintf(out, "swimd [args]:\n");
    fprintf(out, "    -n <str>: needle\n");
    fprintf(out, "    -p <str>: path, current directory if it is not specified\n");
    fprintf(out, "    -top <N>: select top N, default 100\n");
    fprintf(out, "    -git:     respect .gitignore\n");
    fprintf(out, "    -s:       print statistics\n");
    fprintf(out, "    -i:       interactive\n");
    fprintf(out, "    -?:       show this message\n");
}

void swimd_cli_print_invalid_args(void) {
    fprintf(stderr, "Invalid argument list\n");
    fprintf(stderr, "\n");
    swimd_cli_print_usage(stderr);
}

bool swimd_cli_parse_args(SwimdCliArgs *res, int argc, char **argv) {
    int ind = 1;
    while (ind < argc) {
        char *arg = argv[ind];
        if (strcmp(arg, "-s") == 0) {
            ind++;
            res->print_stat = true;
        } else if (strcmp(arg, "-n") == 0) {
            ind++;
            res->needle = argv[ind];
            ind++;
        } else if (strcmp(arg, "-top") == 0) {
            ind++;
            res->top = atoi(argv[ind]);
            ind++;
        } else if (strcmp(arg, "-p") == 0) {
            ind++;
            res->path = argv[ind];
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

void swimd_cli_run_single(SwimdCliArgs *args) {
    swimd_scanner_global_init("swimd.log");
    swimd_scanner_scanners_glob_init();

    swimd_scanner_scanners_setup_path(args->path);
    SwimdScanner *scanner = args->git
        ? &swimd_scanner_scanners[SCANNER_GIT]
        : &swimd_scanner_scanners[SCANNER_FILES];
    swimd_mre_wait(&scanner->scan_finished);

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
    swimd_scanner_shutdown();
}

int main(int argc, char **argv) {
    SwimdCliArgs args = {0};
    if (!swimd_cli_parse_args(&args, argc, argv))
        return 1;
    swimd_cli_run_single(&args);
    return 0;
}
