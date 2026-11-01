#ifndef SWIMD_SCANNER_H
#define SWIMD_SCANNER_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>

#include "swimd_bitset.h"
#include "swimd_heap.h"

#define uchar unsigned char

#define SCANNER_GIT   0
#define SCANNER_FILES 1
#define SCANNER_COUNT 2

typedef struct  {
    char *path;
    char *name;
    int score;
} SwimdScannerProcessInputResultItem;

typedef struct {
    bool scan_in_progress;
    int scanned_items_count;
    SwimdScannerProcessInputResultItem *items;
    int items_length;
} SwimdScannerProcessInputResult;

typedef struct SwimdScannerFolderStruct SwimdScannerFolderStruct;

typedef struct {
    SwimdScannerFolderStruct **arr;
    int length;
    int capacity;
} SwimdScannerFolderStructList;

typedef struct SwimdScannerFolderStruct
{
    char *name;
    int name_length;
    SwimdScannerFolderStructList folder_lst;
    struct SwimdScannerFolderStruct *parent;
} SwimdScannerFolderStruct;

typedef struct {
    char *name;
    int name_length;
    SwimdScannerFolderStruct *folder;
} SwimdScannerFile;

typedef struct {
    SwimdScannerFile *arr;
    int length;
    int capacity;
} SwimdScannerFileList;

typedef struct {
    short *arr;
    int length;
} SwimdScannerFileNameVec;

typedef void (*swimd_scanner_scanning_func)(const char*,
        char*,
        SwimdScannerFileList*,
        SwimdScannerFolderStruct*,
        SwimdWatchOwner*,
        bool);

typedef struct {
    bool initialized;

    char *needle;
    int needle_length;

    uchar *needle_diff_chars;
    short *needle_diff_chars_frequency;
    int needle_diff_chars_length;

    SwimdWatchOwner *watch_owner;

    SwimdScannerFileList *files;
    int files_vec16_length;
    SwimdScannerFileNameVec *files_name_vec;
    short *files_profile_vec;
    int files_profile_vec_char_length;

    SwimdScannerFolderStruct *folders;
    short *scores;
    SwimdBitset scores_prefiltering;
    int prefilter_candidates_count;
    int scores_length;
    SwimdHeap* scores_heaps;
    int scores_heaps_count;

    short **d_vec;
    int d_vec_count;
    short *gap_distr_fun;
    short *gap_distr_sum;

#ifdef _WIN32
    HANDLE scan_thread;
    HANDLE scan_begin;
    HANDLE scan_started;
    HANDLE scan_finished;
    CRITICAL_SECTION scan_state_swap;
#else
    pthread_t scan_thread;
    SwimdAutoResetEvent scan_begin;
    SwimdAutoResetEvent scan_started;
    SwimdManualResetEvent scan_finished;
    pthread_mutex_t scan_state_swap;
#endif
    volatile bool scan_terminate_request;
    volatile bool scan_cancelled_request;
    volatile bool scan_in_progress;
    volatile bool scan_is_refreshing;
    volatile bool scan_terminated;
    char *scan_path;
    char *base_path;
    int scan_files_count;
    int scan_files_refresh_count;

    swimd_scanner_scanning_func scanning_func;
    swimd_thread_callback scanning_loop;
} SwimdScanner;

typedef struct {
    bool enabled;
    int est_threads_count;
#ifdef _WIN32
    HANDLE* est_begins;
    HANDLE* est_ends;
    HANDLE* est_threads;
#else
    SwimdAutoResetEvent* est_begins;
    SwimdAutoResetEvent* est_ends;
    pthread_t* est_threads;
#endif
    volatile bool est_terminate_request;

    int total_task_count;
    atomic_int task_index;
    SwimdScanner* scanner;
} SwimdScannerEstWrokers;

typedef struct {
    double prefilter_and_scores_us;
    double top_us;
    int count;
} SwimdScannerPhaseTimes;

extern bool swimd_scanner_initialized;
extern SwimdScanner swimd_scanner_scanners[SCANNER_COUNT];
extern SwimdScannerEstWrokers swimd_scanner_est_workers;
extern SwimdScannerPhaseTimes swimd_scanner_phase_times;

#ifdef _WIN32
static CRITICAL_SECTION swimd_scanner_scanners_request_lock;
#else
static pthread_mutex_t swimd_scanner_scanners_request_lock;
#endif

double swimd_scanner_now_us(void);
void swimd_scanner_global_init(const char *log_path);
void swimd_scanner_scanners_glob_init(void);
void swimd_scanner_shutdown(void);
void swimd_scanner_scanners_watch_end_tracking(void);
void swimd_scanner_scanners_setup_path(const char *scan_path);
void swimd_scanner_scan_refresh_path(SwimdScanner *scanner);
bool swimd_scanner_scan_is_refreshing(SwimdScanner *scanner, int *read_count);
void swimd_scanner_scan_process_input(const char *input,
        int max_size,
        SwimdScannerProcessInputResult *result,
        SwimdScanner *scanner);
void swimd_scanner_scan_process_input_free(SwimdScannerProcessInputResult *result);

#endif // SWIMD_SCANNER_H
