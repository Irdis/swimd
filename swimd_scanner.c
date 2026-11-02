#ifdef _WIN32
    #include <windows.h>
#else
    #include <pthread.h>
    #include <dirent.h>
#endif
#include <string.h>

#include <assert.h>
#include <immintrin.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>
#include <stdlib.h>
#include <time.h>
#include "git2.h"

#include "swimd_bitset.h"
#include "swimd_heap.h"
#include "swimd_thread.h"
#include "swimd_log.h"
#include "swimd_watch.h"
#include "swimd_scanner.h"

#undef ERROR
#include "nob.h"

#define SCANNER_GIT   0
#define SCANNER_FILES 1
#define SCANNER_COUNT 2

#define PATH_SLASH_GIT_CHAR '/'
#ifdef _WIN32
    #define PATH_SLASH_CHAR '\\'
#else
    #define PATH_SLASH_CHAR '/'
#endif

#define MAX_PATH_LENGTH 300
#define LANES_COUNT_SHORT 16
#define LANES_COUNT_CHAR 32
#define PROFILE_CHAR_COUNT 256
#define PREFILTER_THRESHOLD 0.7
#define EST_MIN_TRIGGER_FILE_COUNT 5000
#define EST_PACK_VEC_SIZE 30
// #define EST_PACK_VEC_SIZE 1000
#define EST_PARALLEL_ENABLED 1
#define ERROR_THRESHOLD 0.2
#define LEN_DIFF_ERROR_COST 0.3
#define SUB_PENALTY -9
#define MATCH_STRICT_REWARD 9
#define MATCH_CASE_INSENSITIVE_REWARD 5
static int GAP_PENALTY[][2] = {
    { -12, 1 },
    { -11, 4 },
    { -10, -1 }
};
#define Vector __m256i
#define D_IND(i, j) (MAX_PATH_LENGTH * LANES_COUNT_SHORT * (i) + \
            LANES_COUNT_SHORT * (j))

#define ABS(x) ((x) < 0 ? -(x) : (x))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define CEIL_DIV(a,b) ((a) % (b) == 0 ? ((a)/(b)) : ((a)/(b)) + 1)

#define IS_ROOT_FOLDER(f) ((f)->parent == NULL)

#define SWAP(a, b, type) \
    do {                 \
        type temp = a;   \
        a = b;           \
        b = temp;        \
    } while (0)

bool swimd_scanner_initialized = false;
SwimdScanner swimd_scanner_scanners[SCANNER_COUNT] = {0};
SwimdScannerEstWrokers swimd_scanner_est_workers = {0};
SwimdScannerPhaseTimes swimd_scanner_phase_times = {0};

#ifdef _WIN32
CRITICAL_SECTION swimd_scanner_scanners_request_lock;
#else
pthread_mutex_t swimd_scanner_scanners_request_lock;
#endif

static void swimd_scanner_list_files(const char *root_dir,
        char *base_path,
        SwimdScannerFileList *file_list,
        SwimdScannerFolderStruct *root_folder,
        SwimdWatchOwner *watch_owner,
        bool refreshing);
static void swimd_scanner_list_git(const char *root_dir,
        char *base_path,
        SwimdScannerFileList *file_list,
        SwimdScannerFolderStruct *root_folder,
        SwimdWatchOwner *watch_owner,
        bool refreshing);

static bool swimd_scanner_simd_prefilter_flat(SwimdScanner *scanner, int profile_pack_start,
        int profile_pack_count);
static void swimd_scanner_simd_scores_flat(SwimdScanner *scanner,
        int file_vec_start, int file_vec_count, int d_ind);
static void swimd_scanner_top_scores_flat(SwimdScanner *scanner,
        int file_vec_start, int file_vec_count, int heap_ind);

#ifdef _WIN32
static DWORD WINAPI swimd_scanner_scanning_loop_git(LPVOID lp_param);
static DWORD WINAPI swimd_scanner_scanning_loop_files(LPVOID lp_param);
#else
static void* swimd_scanner_scanning_loop_git(void *lp_param);
static void* swimd_scanner_scanning_loop_files(void *lp_param);
#endif

static void swimd_scanner_watch_owner_notification_scanner_handler(SwimdWatchOwner *owner);

double swimd_scanner_now_us(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER t;
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);
    return t.QuadPart * 1e6 / freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
#endif
}

static void swimd_scanner_git2_init(void) {
    int init_count = git_libgit2_init();

    if (init_count < 0) {
        const git_error *err = git_error_last();
        swimd_log_append(SWIMD_ERR, "Failed to initialize libgit2: %s\n",
                (err && err->message) ? err->message : "Unknown error");
        exit(EXIT_FAILURE);
    }
}

static int swimd_scanner_get_core_count(void) {
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors;
#else
    return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

static void swimd_scanner_prefilter_and_score(SwimdScanner *scanner, int vec_ind, int vec_size, int thread_index) {
    bool all_filtered_out =  swimd_scanner_simd_prefilter_flat(scanner, vec_ind, vec_size);
    if (!all_filtered_out) {
        swimd_scanner_simd_scores_flat(scanner, vec_ind, vec_size, thread_index);
        swimd_scanner_top_scores_flat(scanner, vec_ind, vec_size, thread_index);
    }
}

static void swimd_scanner_worker_prefilter_and_score(SwimdScannerEstWrokers* est_workers,
        int thread_index) {
    while (1) {
        int task_index = atomic_fetch_add(&est_workers->task_index, 1);
        if (task_index >= est_workers->total_task_count)
            break;
        swimd_scanner_prefilter_and_score(est_workers->scanner,
                task_index * EST_PACK_VEC_SIZE,
                EST_PACK_VEC_SIZE,
                thread_index);
    }
}

static void swimd_scanner_worker_loop_impl(int thread_index) {
    swimd_log_append(SWIMD_INFO, "Est worker started, worker_index=%d", thread_index);
    SwimdScannerEstWrokers* est_workers = &swimd_scanner_est_workers;
    while (1) {
        swimd_are_wait(&est_workers->est_begins[thread_index]);
        if (est_workers->est_terminate_request) {
            break;
        }

        swimd_scanner_worker_prefilter_and_score(est_workers, thread_index);

        swimd_are_set(&est_workers->est_ends[thread_index]);
    }
}

#ifdef _WIN32
static DWORD WINAPI swimd_scanner_worker_loop(LPVOID lp_param) {
    swimd_scanner_worker_loop_impl((int)(intptr_t)lp_param);
    return 0;
}

#else
static void* swimd_scanner_worker_loop(void *lp_param) {
    swimd_scanner_worker_loop_impl((int)(intptr_t)lp_param);
    return NULL;
}
#endif

static void swimd_scanner_est_workers_init(void) {
    int core_count = swimd_scanner_get_core_count();
    if (core_count == 1) {
        swimd_log_append(SWIMD_INFO, "Est workers disabled, 1 cpu core, lol");
        swimd_scanner_est_workers.enabled = false;
        return;
    }
    int workers_count = core_count - 1;
    swimd_log_append(SWIMD_INFO, "Starting est workers, workers_count=%d", workers_count);

#ifdef _WIN32
    swimd_scanner_est_workers.est_begins = malloc(workers_count * sizeof(HANDLE));
    swimd_scanner_est_workers.est_ends = malloc(workers_count * sizeof(HANDLE));
    swimd_scanner_est_workers.est_threads = malloc(workers_count * sizeof(HANDLE));
#else
    swimd_scanner_est_workers.est_begins = malloc(workers_count * sizeof(SwimdAutoResetEvent));
    swimd_scanner_est_workers.est_ends = malloc(workers_count * sizeof(SwimdAutoResetEvent));
    swimd_scanner_est_workers.est_threads = malloc(workers_count * sizeof(pthread_t));
#endif
    swimd_scanner_est_workers.est_terminate_request = false;
    swimd_scanner_est_workers.est_threads_count = workers_count;
    swimd_scanner_est_workers.total_task_count = -1;
    swimd_scanner_est_workers.task_index = -1;
    swimd_scanner_est_workers.scanner = NULL;
    swimd_scanner_est_workers.enabled = true;

    for (int i = 0; i < workers_count; i++) {
#ifdef _WIN32
        HANDLE* ev_b;
        HANDLE* ev_e;
        HANDLE* tr;
        LPVOID thread_index = (LPVOID)(intptr_t)i;
#else
        SwimdAutoResetEvent* ev_b;
        SwimdAutoResetEvent* ev_e;
        pthread_t* tr;
        void* thread_index = (void*)(intptr_t)i;
#endif
        ev_b = &swimd_scanner_est_workers.est_begins[i];
        ev_e = &swimd_scanner_est_workers.est_ends[i];
        tr = &swimd_scanner_est_workers.est_threads[i];
        swimd_are_init(ev_e, false);
        swimd_are_init(ev_b, false);
        swimd_thread_create(tr, swimd_scanner_worker_loop, thread_index);
    }
}

static void swimd_scanner_scanners_init(void) {
    for (int i = 0; i < SCANNER_COUNT; i++) {
        SwimdScanner *scanner = &swimd_scanner_scanners[i];

        // all other flags should return into they natural position naturaly
        scanner->scan_terminated = false;

        SwimdWatchOwner *watch_owner = malloc(sizeof(SwimdWatchOwner));

        swimd_watch_owner_init(watch_owner);
        swimd_watch_owner_begin_waiting(watch_owner,
                swimd_scanner_watch_owner_notification_scanner_handler);

        scanner->watch_owner = watch_owner;
    }
}

static void swimd_scanner_init_git(void) {
    swimd_scanner_scanners[SCANNER_GIT].scanning_func = &swimd_scanner_list_git;
    swimd_scanner_scanners[SCANNER_GIT].scanning_loop = &swimd_scanner_scanning_loop_git;
}

static void swimd_scanner_init_files(void) {
    swimd_scanner_scanners[SCANNER_FILES].scanning_func = &swimd_scanner_list_files;
    swimd_scanner_scanners[SCANNER_FILES].scanning_loop = &swimd_scanner_scanning_loop_files;
}

static void swimd_scanner_scanners_init_request_lock(void) {
     swimd_crit_init(&swimd_scanner_scanners_request_lock);
}

static void swimd_scanner_scanners_request_lock_free(void) {
     swimd_crit_close(&swimd_scanner_scanners_request_lock);
}

void swimd_scanner_global_init(const char *log_path) {
    swimd_scanner_initialized = true;

    swimd_log_init(log_path);
    swimd_scanner_git2_init();
    swimd_scanner_scanners_init_request_lock();
    swimd_scanner_scanners_init();
    swimd_scanner_init_git();
    swimd_scanner_init_files();
    swimd_scanner_est_workers_init();
}

static void swimd_scanner_git2_free(void) {
    git_libgit2_shutdown();
}

static void swimd_scanner_est_workers_free(void) {
    if (!swimd_scanner_est_workers.enabled)
        return;
    swimd_scanner_est_workers.est_terminate_request = true;

    for (int i = 0; i < swimd_scanner_est_workers.est_threads_count; i++) {
        swimd_are_set(&swimd_scanner_est_workers.est_begins[i]);
    }

    for (int i = 0; i < swimd_scanner_est_workers.est_threads_count; i++) {
        swimd_thread_join(&swimd_scanner_est_workers.est_threads[i]);

        swimd_are_close(&swimd_scanner_est_workers.est_begins[i]);
        swimd_are_close(&swimd_scanner_est_workers.est_ends[i]);
        swimd_thread_close(&swimd_scanner_est_workers.est_threads[i]);
    }
    swimd_scanner_est_workers.est_terminate_request = false;
    free(swimd_scanner_est_workers.est_begins);
    free(swimd_scanner_est_workers.est_ends);
    free(swimd_scanner_est_workers.est_threads);
}

static void swimd_scanner_global_free(void) {
    swimd_scanner_est_workers_free();
    swimd_scanner_scanners_request_lock_free();
    swimd_scanner_git2_free();
    swimd_log_free();
}

static void swimd_scanner_folders_init(SwimdScannerFolderStructList *lst) {
    int default_size = 4;
    lst->arr = malloc(default_size * sizeof(SwimdScannerFolderStruct*));
    lst->length = 0;
    lst->capacity = default_size;
}

static void swimd_scanner_folders_append(SwimdScannerFolderStructList *lst, SwimdScannerFolderStruct *folder) {
    if (lst->length == lst->capacity) {
        lst->capacity = lst->capacity * 2;
        lst->arr = realloc(lst->arr, lst->capacity * sizeof(SwimdScannerFolderStruct*));
    }
    lst->arr[lst->length] = folder;
    lst->length++;
}

static void swimd_scanner_folders_free(SwimdScannerFolderStructList *lst) {
    free(lst->arr);
}

static void swimd_scanner_file_list_init(SwimdScannerFileList *lst) {
    int default_size = 4;
    lst->arr = malloc(default_size * sizeof(SwimdScannerFile));
    lst->length = 0;
    lst->capacity = default_size;
}

static void swimd_scanner_file_list_append(SwimdScannerFileList *lst, SwimdScannerFile file) {
    if (lst->length == lst->capacity) {
        lst->capacity = lst->capacity * 2;
        lst->arr = realloc(lst->arr, lst->capacity * sizeof(SwimdScannerFile));
    }
    lst->arr[lst->length] = file;
    lst->length++;
    if (lst->length % 10000 == 0)
        swimd_log_append(SWIMD_INFO, "Scanned file count %d", lst->length);
}

static void swimd_scanner_file_list_free(SwimdScannerFileList *lst) {
    lst->length = 0;
    free(lst->arr);
}

#ifdef _WIN32

static void swimd_scanner_list_files_win32_rec(const char *root_dir,
        SwimdScannerFileList *file_list,
        SwimdScannerFolderStruct *root_folder,
        SwimdWatchOwner *watch_owner,
        bool refreshing,
        bool is_root) {
    SwimdScanner *scanner = &swimd_scanner_scanners[SCANNER_FILES];
    char root_mask[MAX_PATH_LENGTH];
    char inner_folder[MAX_PATH_LENGTH];

    strcpy(root_mask, root_dir);
    strcat(root_mask, "\\*");

    WIN32_FIND_DATA find_file_data;
    HANDLE h_find;

    h_find = FindFirstFile(root_mask, &find_file_data);

    if (h_find == INVALID_HANDLE_VALUE) {
        swimd_log_append(SWIMD_ERR, "FindFirstFile failed (%lu)\n", GetLastError());
        return;
    }
    swimd_watch_track_path(watch_owner, root_dir, is_root);

    while (1) {
        const char *current_file = find_file_data.cFileName;
        int current_file_len = strlen(current_file);

        if (find_file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (strcmp(current_file, ".") != 0 && strcmp(current_file, "..") != 0) {
                char *folder_name = malloc((current_file_len + 1) * sizeof(char));
                strcpy(folder_name, current_file);
                folder_name[current_file_len] = '\0';

                SwimdScannerFolderStruct *folder_node = malloc(sizeof(SwimdScannerFolderStruct));

                folder_node->name = folder_name;
                folder_node->name_length = current_file_len;
                folder_node->parent = root_folder;

                swimd_scanner_folders_init(&folder_node->folder_lst);
                swimd_scanner_folders_append(&root_folder->folder_lst, folder_node);

                strcpy(inner_folder, root_dir);
                strcat(inner_folder, "\\");
                strcat(inner_folder, current_file);

                swimd_scanner_list_files_win32_rec(inner_folder,
                        file_list,
                        folder_node,
                        watch_owner,
                        refreshing,
                        false);
            }
        } else {
            char *file_name = malloc((current_file_len + 1) * sizeof(char));
            strcpy(file_name, current_file);
            file_name[current_file_len] = '\0';

            SwimdScannerFile file_node = {
                .name = file_name,
                .name_length = current_file_len,
                .folder = root_folder
            };

            swimd_scanner_file_list_append(file_list, file_node);
            if (!refreshing)
                scanner->scan_files_count++;
            else
                scanner->scan_files_refresh_count++;
        }

        if (FindNextFile(h_find, &find_file_data) == 0)
            break;
        if (scanner->scan_cancelled_request)
            break;
    }

    if (!scanner->scan_cancelled_request)
    {
        DWORD dw_error = GetLastError();
        if (dw_error != ERROR_NO_MORE_FILES) {
            swimd_log_append(SWIMD_ERR, "FindNextFile error (%lu)", dw_error);
        }
    }

    FindClose(h_find);
}

static void swimd_scanner_list_files_win32(const char *root_dir,
        SwimdScannerFileList *file_list,
        SwimdScannerFolderStruct *root_folder,
        SwimdWatchOwner *watch_owner,
        bool refreshing) {
    swimd_scanner_list_files_win32_rec(root_dir,
            file_list,
            root_folder,
            watch_owner,
            refreshing,
            true);
}

#else
static void swimd_scanner_list_files_linux_rec(const char *root_dir,
        SwimdScannerFileList *file_list,
        SwimdScannerFolderStruct *root_folder,
        SwimdWatchOwner *watch_owner,
        bool refreshing,
        bool is_root) {
    SwimdScanner *scanner = &swimd_scanner_scanners[SCANNER_FILES];
    char inner_folder[MAX_PATH_LENGTH];
    struct dirent *entry;
    DIR *dp = opendir(root_dir);
    if (dp == NULL) {
        swimd_log_append(SWIMD_ERR, "Unable to opendir %s", root_dir);
        return;
    }
    swimd_watch_track_path(watch_owner, root_dir, is_root);

    while ((entry = readdir(dp))) {
        const char *current_file = entry->d_name;
        int current_file_len = strlen(current_file);
        if (entry->d_type == DT_DIR) {
            if (strcmp(current_file, ".") != 0 && strcmp(current_file, "..") != 0) {
                char *folder_name = malloc((current_file_len + 1) * sizeof(char));
                strcpy(folder_name, current_file);
                folder_name[current_file_len] = '\0';

                SwimdScannerFolderStruct *folder_node = malloc(sizeof(SwimdScannerFolderStruct));
                folder_node->name = folder_name;
                folder_node->name_length = current_file_len;
                folder_node->parent = root_folder;

                swimd_scanner_folders_init(&folder_node->folder_lst);
                swimd_scanner_folders_append(&root_folder->folder_lst, folder_node);

                strcpy(inner_folder, root_dir);
                strcat(inner_folder, "/");
                strcat(inner_folder, current_file);

                swimd_scanner_list_files_linux_rec(inner_folder,
                        file_list,
                        folder_node,
                        watch_owner,
                        refreshing,
                        false);
            }
        } else if (entry->d_type == DT_REG) {
            char *file_name = malloc((current_file_len + 1) * sizeof(char));
            strcpy(file_name, current_file);
            file_name[current_file_len] = '\0';

            SwimdScannerFile file_node = {
                .name = file_name,
                .name_length = current_file_len,
                .folder = root_folder
            };

            swimd_scanner_file_list_append(file_list, file_node);
            if (!refreshing)
                scanner->scan_files_count++;
            else
                scanner->scan_files_refresh_count++;
        }

        if (scanner->scan_cancelled_request)
            break;
    }

    closedir(dp);
}

static void swimd_scanner_list_files_linux(const char *root_dir,
        SwimdScannerFileList *file_list,
        SwimdScannerFolderStruct *root_folder,
        SwimdWatchOwner *watch_owner,
        bool refreshing) {
    swimd_scanner_list_files_linux_rec(root_dir,
        file_list,
        root_folder,
        watch_owner,
        refreshing,
        true);
}
#endif

static void swimd_scanner_list_files(const char *root_dir,
        char *base_path,
        SwimdScannerFileList *file_list,
        SwimdScannerFolderStruct *root_folder,
        SwimdWatchOwner *watch_owner,
        bool refreshing) {
    int root_dir_len = strlen(root_dir);
    memcpy(base_path, root_dir, root_dir_len);
    base_path[root_dir_len] = '\0';

#ifdef _WIN32
    swimd_scanner_list_files_win32(root_dir,
        file_list,
        root_folder,
        watch_owner,
        refreshing);
#else
    swimd_scanner_list_files_linux(root_dir,
        file_list,
        root_folder,
        watch_owner,
        refreshing);
#endif
}


static void swimd_scanner_log_git2_error(const char *message, int error) {
    const char *lg2msg = "";

    if (!error)
        return;

    const git_error *lg2err = git_error_last();
    if (lg2err != NULL && lg2err->message != NULL) {
        lg2msg = lg2err->message;
    }

    swimd_log_append(SWIMD_ERR, "%s [%d] %s", message, error, lg2msg);
}

static int swimd_scanner_path_length(const char *a, int segment_count, char separator) {
    if (segment_count == 0)
        return 0;
    int ind = 0;
    while(1) {
        if (a[ind] == separator) {
            segment_count--;
        }
        if (segment_count == 0) {
            return ind;
        }
        if (a[ind] == '\0') {
            return ind;
        }
        ind++;
    }
}
static int swimd_scanner_path_depth(const char *a, char separator) {
    int depth = 1;
    int ind = 0;
    while (1) {
        if (a[ind] == '\0')
            return depth;
        if (a[ind] == separator)
            depth++;
        ind++;
    }
}

static int swimd_scanner_path_match_depth(const char *a, const char *b, char separator) {
    int match_depth = 0;
    int ind = 0;
    while (1) {
        if (a[ind] == '\0' || b[ind] == '\0') {
            if (a[ind] == b[ind] ||
                a[ind] == separator ||
                b[ind] == separator)
                return match_depth + 1;
            return match_depth;
        }
        if (a[ind] != b[ind]) {
            return match_depth;
        }
        if (a[ind] == separator) {
            match_depth++;
        }
        ind++;
    }
}

static SwimdScannerFolderStruct *swimd_scanner_folder_go_up(SwimdScannerFolderStruct *folder,
        int up_count) {
    while (up_count > 0) {
        folder = folder->parent;
        up_count--;
    }
    return folder;
}

static const char *swimd_scanner_path_skip_folder_count(const char *path,
        int folder_count, char separator) {
    while (folder_count > 0) {
        if (*path == separator) {
            folder_count--;
        }
        path++;
    }
    return path;
}

static SwimdScannerFolderStruct *swimd_scanner_folder_find_child(const char *child_name,
        int child_name_length,
        SwimdScannerFolderStruct *cur_folder) {
    for (int i = 0; i < cur_folder->folder_lst.length; i++) {
        SwimdScannerFolderStruct *child_folder = cur_folder->folder_lst.arr[i];
        if (child_folder->name_length == child_name_length &&
                strncmp(child_name, child_folder->name, child_name_length) == 0) {
            return child_folder;
        }
    }
    return NULL;
}

static SwimdScannerFolderStruct *swimd_scanner_process_path(const char *path,
        SwimdScannerFileList *file_list,
        SwimdScannerFolderStruct *cur_folder,
        const char *cur_path,
        int cur_depth,
        int *res_depth,
        bool refreshing) {
    SwimdScanner *scanner = &swimd_scanner_scanners[SCANNER_GIT];
    int match_depth = swimd_scanner_path_match_depth(path, cur_path, PATH_SLASH_GIT_CHAR);
    int up_count = cur_depth - match_depth;
    *res_depth = cur_depth - up_count;

    SwimdScannerFolderStruct *base_folder = swimd_scanner_folder_go_up(cur_folder, up_count);
    const char *base_path = swimd_scanner_path_skip_folder_count(path, match_depth, PATH_SLASH_GIT_CHAR);

    int segment_len = 0;
    while (1) {
        if (base_path[segment_len] == '\0') {
            char *file_name = malloc((segment_len + 1) * sizeof(char));
            strncpy(file_name, base_path, segment_len);
            file_name[segment_len] = '\0';

            SwimdScannerFile file_node = {
                .name = file_name,
                .name_length = segment_len,
                .folder = base_folder
            };

            swimd_scanner_file_list_append(file_list, file_node);

            if (!refreshing)
                scanner->scan_files_count++;
            else
                scanner->scan_files_refresh_count++;

            break;
        }

        if (base_path[segment_len] == PATH_SLASH_GIT_CHAR) {
            (*res_depth)++;

            SwimdScannerFolderStruct *child_folder = swimd_scanner_folder_find_child(base_path,
                    segment_len,
                    base_folder);
            if (child_folder != NULL) {
                base_path += segment_len + 1;
                segment_len = 0;
                base_folder = child_folder;
                continue;
            }

            char *folder_name = malloc((segment_len + 1) * sizeof(char));
            strncpy(folder_name, base_path, segment_len);
            folder_name[segment_len] = '\0';

            SwimdScannerFolderStruct *folder_node = malloc(sizeof(SwimdScannerFolderStruct));

            folder_node->name = folder_name;
            folder_node->name_length = segment_len;
            folder_node->parent = base_folder;

            swimd_scanner_folders_init(&folder_node->folder_lst);
            swimd_scanner_folders_append(&base_folder->folder_lst, folder_node);

            base_path += segment_len + 1;
            segment_len = 0;
            base_folder = folder_node;
            continue;
        }
        segment_len++;
    }
    return base_folder;
}

static void swimd_scanner_git_collect_index_paths(git_repository *repo,
        SwimdScannerFileList *file_list,
        SwimdScannerFolderStruct *root_folder,
        bool refreshing) {
    SwimdScanner *scanner = &swimd_scanner_scanners[SCANNER_GIT];
    if (scanner->scan_cancelled_request)
        return;

    git_index *index = NULL;

	int index_result = git_repository_index(&index, repo);
    if (index_result < 0) {
        swimd_scanner_log_git2_error("Unable to index git repository", index_result);
        goto cleanup;
    }

    size_t entry_count = git_index_entrycount(index);

    SwimdScannerFolderStruct *cur_folder = root_folder;
    int cur_depth = 0;
    char cur_path[MAX_PATH_LENGTH] = "";

    for (size_t i = 0; i < entry_count; i++) {
        const git_index_entry *entry = git_index_get_byindex(index, i);
        int depth = 0;
        cur_folder = swimd_scanner_process_path(entry->path,
            file_list,
            cur_folder,
            cur_path,
            cur_depth,
            &depth,
            refreshing);
        cur_depth = depth;
        strcpy(cur_path, entry->path);

        if (scanner->scan_cancelled_request)
            break;
    }

cleanup:
	git_index_free(index);
}

static void swimd_scanner_git_collect_status_paths(git_repository *repo,
        SwimdScannerFileList *file_list,
        SwimdScannerFolderStruct *root_folder,
        bool refreshing) {
    SwimdScanner *scanner = &swimd_scanner_scanners[SCANNER_GIT];
    if (scanner->scan_cancelled_request)
        return;
    git_status_options status_opts;
    git_status_options_init(&status_opts, GIT_STATUS_OPTIONS_VERSION);
    status_opts.show = GIT_STATUS_SHOW_WORKDIR_ONLY;
    status_opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
                        GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

    git_status_list *status_list = NULL;
    int list_result = git_status_list_new(&status_list, repo, &status_opts);
    if (list_result < 0) {
        swimd_scanner_log_git2_error("Unable to index git repository", list_result);
        goto cleanup;
    }

    SwimdScannerFolderStruct *cur_folder = root_folder;
    int cur_depth = 0;
    char cur_path[MAX_PATH_LENGTH] = "";

    size_t count = git_status_list_entrycount(status_list);
    for (size_t i = 0; i < count; i++) {
        const git_status_entry *entry = git_status_byindex(status_list, i);
        if (entry->index_to_workdir == NULL) {
            continue;
        }
        const char *path = entry->index_to_workdir->new_file.path;
        if ((entry->status & GIT_STATUS_WT_NEW) > 0) {
            int depth = 0;
            cur_folder = swimd_scanner_process_path(path,
                file_list,
                cur_folder,
                cur_path,
                cur_depth,
                &depth,
                refreshing);
            cur_depth = depth;
            strcpy(cur_path, path);
        }
        if (scanner->scan_cancelled_request)
            break;
    }
cleanup:
    git_status_list_free(status_list);
}

static void swimd_scanner_list_git_normalize_base_path(char *base_path) {
    if (PATH_SLASH_CHAR == PATH_SLASH_GIT_CHAR)
        return;
    int base_path_len = strlen(base_path);
    for (int i = 0; i < base_path_len; i++) {
        if (base_path[i] == PATH_SLASH_GIT_CHAR)
            base_path[i] = PATH_SLASH_CHAR;
    }

}
static void swimd_scanner_list_git(const char *root_dir,
        char *base_path,
        SwimdScannerFileList *file_list,
        SwimdScannerFolderStruct *root_folder,
        SwimdWatchOwner *watch_owner,
        bool refreshing) {
    (void)watch_owner;

    git_repository *repo = NULL;

    int repo_result = git_repository_open_ext(&repo, root_dir, 0, NULL);
    if (repo_result == GIT_ENOTFOUND) {
        swimd_log_append(SWIMD_INFO, "Not a git repository %s", root_dir);
        goto cleanup;
    } else if (repo_result < 0) {
        swimd_scanner_log_git2_error("Unable to open git repository", repo_result);
        goto cleanup;
    }

    const char *repo_path = git_repository_workdir(repo);
    int repo_path_length = strlen(repo_path);
    repo_path_length--; // cut last slash
    memcpy(base_path, repo_path, repo_path_length);
    base_path[repo_path_length] = '\0';
    swimd_scanner_list_git_normalize_base_path(base_path);

    swimd_scanner_git_collect_index_paths(repo,
            file_list,
            root_folder,
            refreshing);
    swimd_scanner_git_collect_status_paths(repo,
            file_list,
            root_folder,
            refreshing);
cleanup:
    git_repository_free(repo);
}

static void swimd_scanner_list_directories_folders_free(SwimdScannerFolderStruct *root_folder) {
    for (int i = 0; i < root_folder->folder_lst.length; i++) {
        SwimdScannerFolderStruct *folder = root_folder->folder_lst.arr[i];
        swimd_scanner_list_directories_folders_free(folder);
        swimd_scanner_folders_free(&folder->folder_lst);
        free(folder->name);
        free(folder);
    }
}

static void swimd_scanner_list_directories_free(const SwimdScannerFileList *lst,
        SwimdScannerFolderStruct *root_folder) {
    swimd_scanner_list_directories_folders_free(root_folder);
    for (int i = 0; i < lst->length; i++) {
        free(lst->arr[i].name);
    }
}

static inline uchar swimd_scanner_fold_case(char c) {
  uchar u = (uchar)c;
  return (u >= 'A' && u <= 'Z') ? u + ('a' - 'A') : u;
}

static void swimd_scanner_prep_files_vec(SwimdScanner *scanner) {
    SwimdScannerFileList *files = scanner->files;
    int files_length = files->length;

    int files_vec16_length = CEIL_DIV(files_length, LANES_COUNT_SHORT);
    SwimdScannerFileNameVec *files_name_vec = malloc(files_vec16_length * sizeof(SwimdScannerFileNameVec));

    for (int i = 0; i < files_vec16_length; i++) {
        int max_length = 0;
        for (int j = 0; j < LANES_COUNT_SHORT; j++) {
            if (i * LANES_COUNT_SHORT + j >= files_length)
                break;
            max_length = MAX(max_length, files->arr[i * LANES_COUNT_SHORT + j].name_length);
        }

        int file_vec_length = max_length * LANES_COUNT_SHORT;
        short *file_name_vec_arr = malloc(file_vec_length * sizeof(short));
        memset(file_name_vec_arr, 0, file_vec_length * sizeof(short));

        for (int k = 0; k < max_length; k++) {
            for (int j = 0; j < LANES_COUNT_SHORT; j++) {
                if (i * LANES_COUNT_SHORT + j >= files_length)
                    break;
                SwimdScannerFile file = files->arr[i * LANES_COUNT_SHORT + j];
                if (k >= file.name_length)
                    continue;
                file_name_vec_arr[k * LANES_COUNT_SHORT + j] = (short)file.name[k];
            }
        }
        files_name_vec[i] = (SwimdScannerFileNameVec){
            .arr = file_name_vec_arr,
            .length = file_vec_length,
        };
    }


    int profile_char_vec_length = LANES_COUNT_SHORT * CEIL_DIV(files_length, LANES_COUNT_SHORT);
    short *files_profile_vec = malloc(PROFILE_CHAR_COUNT * profile_char_vec_length * sizeof(short));

    for (int i = 0; i < files_length; i++) {

        SwimdScannerFile file = files->arr[i];

        for (int j = 0; j < file.name_length; j++) {
            uchar ch = swimd_scanner_fold_case(file.name[j]);
            files_profile_vec[ch * profile_char_vec_length + i]++;
        }
    }
    scanner->files_name_vec = files_name_vec;
    scanner->files_profile_vec = files_profile_vec;
    scanner->files_profile_vec_char_length = profile_char_vec_length;
    scanner->files_vec16_length = files_vec16_length;
}

static void swimd_scanner_prep_files_vec_free(SwimdScanner *state) {
    for (int i = 0; i < state->files_vec16_length; i++) {
        SwimdScannerFileNameVec file_name_vec = state->files_name_vec[i];
        free(file_name_vec.arr);
    }

    free(state->files_name_vec);
    free(state->files_profile_vec);
}

static void swimd_scanner_setup_needle_profile(SwimdScanner *scanner) {
    uchar *needle_diff_chars = malloc(PROFILE_CHAR_COUNT * sizeof(uchar));
    short *needle_diff_chars_frequency = malloc(PROFILE_CHAR_COUNT * sizeof(short));
    int needle_diff_chars_length = 0;

    short needle_profile[PROFILE_CHAR_COUNT] = {0};

    for (int i = 0; i < scanner->needle_length; i++) {
        uchar ch = swimd_scanner_fold_case(scanner->needle[i]);
        needle_profile[ch]++;
    }

    for (int i = 0; i < PROFILE_CHAR_COUNT; i++) {
        uchar ch = (uchar)i;
        short fq = needle_profile[i];
        if (fq > 0) {
            needle_diff_chars[needle_diff_chars_length] = ch;
            needle_diff_chars_frequency[needle_diff_chars_length] = fq;
            needle_diff_chars_length++;
        }
    }
    scanner->needle_diff_chars = needle_diff_chars;
    scanner->needle_diff_chars_frequency = needle_diff_chars_frequency;
    scanner->needle_diff_chars_length = needle_diff_chars_length;
}

static void swimd_scanner_setup_score_heaps(int max_size, SwimdScanner *scanner) {
    int heaps_count = swimd_scanner_est_workers.est_threads_count + 1;
    SwimdHeap *heaps = malloc(heaps_count * sizeof(SwimdHeap));
    for (int i = 0; i < heaps_count; i++) {
        SwimdHeap *heap = &heaps[i];
        swimd_heap_init(heap, max_size);
    }
    scanner->scores_heaps = heaps;
    scanner->scores_heaps_count = heaps_count;
}

static void swimd_scanner_setup_needle(const char *needle, int max_size, SwimdScanner *scanner) {
    int needle_length = strlen(needle);
    scanner->needle = malloc((needle_length + 1) * sizeof(char));
    strcpy(scanner->needle, needle);
    scanner->needle_length = needle_length;

    swimd_scanner_setup_needle_profile(scanner);
    swimd_scanner_setup_score_heaps(max_size, scanner);
}

static void swimd_scanner_scores_heaps_free(SwimdScanner *scanner) {
    for (int i = 0; i < scanner->scores_heaps_count; i++) {
        SwimdHeap *heap = &scanner->scores_heaps[i];
        swimd_heap_free(heap);
    }
    free(scanner->scores_heaps);
}

static void swimd_scanner_setup_needle_free(SwimdScanner *scanner) {
    swimd_scanner_scores_heaps_free(scanner);
    free(scanner->needle);
    free(scanner->needle_diff_chars);
    free(scanner->needle_diff_chars_frequency);
}

void swimd_scanner_vec_estimate_diagnostic(short *d,
        int ind,
        char *needle,
        int needle_length,
        char *file_name,
        int file_name_length) {
    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "\n===== swimd_vec_estimate_diag ====\n");
    nob_sb_appendf(&sb, "%s %d\n", needle, needle_length);
    nob_sb_appendf(&sb, "%s %d\n", file_name, file_name_length);
    for (int i = 0; i <= needle_length; i++) {
        for (int j = 0; j <= file_name_length; j++) {
            nob_sb_appendf(&sb, "%4d ", d[D_IND(i, j) + ind]);
        }
        nob_sb_append_cstr(&sb, "\n");
    }
    nob_sb_append_cstr(&sb, "\n");
    nob_sb_append_null(&sb);
    swimd_log_append(SWIMD_DEBUG, sb.items);
    nob_sb_free(sb);
}

static void swimd_scanner_score_minmax(int needle_len,
        int word_len,
        short *gap_distr_sum,
        int *min_score,
        int *max_score) {
    int gap_sum = gap_distr_sum[MAX(needle_len, word_len)] -
        gap_distr_sum[MIN(needle_len, word_len)];

    int min = SUB_PENALTY * MIN(needle_len, word_len);
    min += gap_sum;
    *min_score = min;

    int max = MATCH_STRICT_REWARD * MIN(needle_len, word_len);
    max += gap_sum;
    *max_score = max;
}

static inline Vector swimd_scanner_simd_filter_az(Vector x) {
    Vector va = _mm256_set1_epi16('a' - 1);
    Vector vz = _mm256_set1_epi16('z' + 1);
    Vector vA = _mm256_set1_epi16('A' - 1);
    Vector vZ = _mm256_set1_epi16('Z' + 1);

    Vector gea = _mm256_cmpgt_epi16(x, va);
    Vector lez = _mm256_cmpgt_epi16(vz, x);
    Vector in_a_z = _mm256_and_si256(gea, lez);

    Vector geA = _mm256_cmpgt_epi16(x, vA);
    Vector leZ = _mm256_cmpgt_epi16(vZ, x);
    Vector in_A_Z = _mm256_and_si256(geA, leZ);

    Vector mask = _mm256_or_si256(in_a_z, in_A_Z);
    return _mm256_and_si256(x, mask);
}

static inline Vector swimd_scanner_simd_az_inverse_case(Vector x) {
    Vector zero = _mm256_setzero_si256();
    Vector az = swimd_scanner_simd_filter_az(x);
    Vector mask = _mm256_cmpeq_epi16(az, zero);
    Vector sign = _mm256_set1_epi16('a' ^ 'A');
    Vector flipped = _mm256_xor_si256(az, sign);
    Vector clear = _mm256_andnot_si256(mask, flipped);
    return clear;
}

static void swimd_scanner_simd_haystack_scores(short *d,
    char *needle,
    int needle_length,
    short *haystack_vec,
    int haystack_vec_length,
    int haystack_index,
    short *scores,
    short *gap_distr_fun,
    SwimdScannerFileList *files
) {
    int haystack_max_length = haystack_vec_length / LANES_COUNT_SHORT;

    Vector sub_pen = _mm256_set1_epi16(SUB_PENALTY);
    Vector eq_reward = _mm256_set1_epi16(MATCH_STRICT_REWARD);
    Vector cis_reward = _mm256_set1_epi16(MATCH_CASE_INSENSITIVE_REWARD);

    for (int i = 1; i <= needle_length; i++) {
        Vector gap_pen_i = _mm256_set1_epi16(gap_distr_fun[i - 1]);
        Vector va = _mm256_set1_epi16(needle[i - 1]);
        Vector vca = swimd_scanner_simd_az_inverse_case(va);
        for (int j = 1; j <= haystack_max_length; j++) {
            Vector gap_pen_j = _mm256_set1_epi16(gap_distr_fun[j - 1]);
            Vector vb = _mm256_loadu_si256((Vector const*)&haystack_vec[LANES_COUNT_SHORT * (j - 1)]);
            Vector vdiag = _mm256_loadu_si256((Vector const*)&d[
                    D_IND(i - 1, j - 1)
            ]);
            Vector vleft = _mm256_loadu_si256((Vector const*)&d[
                    D_IND(i, j - 1)
            ]);
            Vector vup = _mm256_loadu_si256((Vector const*)&d[
                    D_IND(i - 1, j)
            ]);

            Vector strict_eq = _mm256_cmpeq_epi16(va, vb);
            Vector caseinsensitive_eq = _mm256_cmpeq_epi16(vca, vb);
            Vector hit_mask = _mm256_or_si256(strict_eq, caseinsensitive_eq);
            Vector c1 = _mm256_and_si256(eq_reward, strict_eq);
            Vector c2 = _mm256_and_si256(cis_reward, caseinsensitive_eq);
            Vector c3 = _mm256_andnot_si256(hit_mask, sub_pen);
            Vector o1 = _mm256_add_epi16(c1, c2);
            o1 = _mm256_add_epi16(o1, c3);
            o1 = _mm256_add_epi16(o1, vdiag);

            Vector o2 = _mm256_add_epi16(vup, gap_pen_i);
            Vector o3 = _mm256_add_epi16(vleft, gap_pen_j);

            Vector o = _mm256_max_epi16(o1, o2);
            o = _mm256_max_epi16(o, o3);
            _mm256_storeu_si256((Vector*)&d[
                    D_IND(i, j)
            ], o);
        }
    }
    for (int i = 0; i < LANES_COUNT_SHORT; i++) {
        int file_index = haystack_index * LANES_COUNT_SHORT + i;
        if (file_index >= files->length)
            break;
        int file_name_length = files->arr[file_index].name_length;
        scores[file_index] = d[D_IND(needle_length, file_name_length) + i];
#ifdef DEBUG_PRINT
        // char *file_name = files->arr[file_index].name;
        // swimd_scanner_vec_estimate_diagnostic(d,
        //         i,
        //         needle,
        //         needle_length,
        //         file_name,
        //         file_name_length);
#endif
    }
}

static void swimd_scanner_simd_scores_flat(SwimdScanner *scanner,
        int file_vec_start, int file_vec_count, int d_ind) {
    int file_vec_end = MIN(file_vec_start + file_vec_count,
            scanner->files_vec16_length);

    for (int i = file_vec_start; i < file_vec_end; i++) {
        int files_length = scanner->files->length;
        bool all_filtered_out = true;
        for (int j = 0; j < LANES_COUNT_SHORT; j++) {
            int file_index = i * LANES_COUNT_SHORT + j;
            if (file_index >= files_length) {
                break;
            }
            if (swimd_bitset_get(&scanner->scores_prefiltering,
                        file_index)) {
                all_filtered_out = false;
                break;
            }
        }

        if (all_filtered_out)
            continue;

        swimd_scanner_simd_haystack_scores(
            scanner->d_vec[d_ind],
            scanner->needle,
            scanner->needle_length,
            scanner->files_name_vec[i].arr,
            scanner->files_name_vec[i].length,
            i,
            scanner->scores,
            scanner->gap_distr_fun,
            scanner->files
        );
    }
}

static void swimd_scanner_simd_scores(SwimdScanner *scanner) {
#ifndef EST_PARALLEL_ENABLED

    swimd_scanner_prefilter_and_score(scanner, 0, scanner->files_vec16_length, 0);
    return;

#else
    if (!swimd_scanner_est_workers.enabled ||
            scanner->files->length < EST_MIN_TRIGGER_FILE_COUNT) {
        swimd_scanner_prefilter_and_score(scanner, 0, scanner->files_vec16_length, 0);
        return;
    }
    swimd_scanner_est_workers.total_task_count = CEIL_DIV(scanner->files_vec16_length,
            EST_PACK_VEC_SIZE);
    swimd_scanner_est_workers.task_index = 0;
    swimd_scanner_est_workers.scanner = scanner;

    for (int i = 0; i < swimd_scanner_est_workers.est_threads_count; i++) {
        swimd_are_set(&swimd_scanner_est_workers.est_begins[i]);
    }

    swimd_scanner_worker_prefilter_and_score(&swimd_scanner_est_workers,
            swimd_scanner_est_workers.est_threads_count);

    for (int i = 0; i < swimd_scanner_est_workers.est_threads_count; i++) {
        swimd_are_wait(&swimd_scanner_est_workers.est_ends[i]);
    }
    swimd_scanner_est_workers.total_task_count = -1;
    swimd_scanner_est_workers.task_index = 0;
    swimd_scanner_est_workers.scanner = NULL;
#endif
}

static void swimd_scanner_simd_haystack_prefilter(
    uchar *needle_diff_chars,
    short *needle_diff_chars_frequency,
    int needle_diff_chars_length,
    int needle_length,
    short *haystack_profile_vec,
    int haystack_profile_char_vec_length,
    int haystack_vec_index,
    SwimdBitset *scores_prefiltering,
    int files_length,
    bool *all_filtered_out
) {
    int haystack_index = LANES_COUNT_SHORT * haystack_vec_index;
    Vector res = _mm256_setzero_si256();
    Vector zero = _mm256_setzero_si256();

    for (int i = 0; i < needle_diff_chars_length; i++) {
        uchar needle_char = needle_diff_chars[i];
        short needle_char_frequency = needle_diff_chars_frequency[i];

        Vector va = _mm256_set1_epi16(needle_char_frequency);
        Vector vb = _mm256_loadu_si256((Vector const*)&haystack_profile_vec[
                needle_char * haystack_profile_char_vec_length + haystack_index]);

        Vector diff = _mm256_sub_epi16(vb, va);
        Vector mask = _mm256_cmpgt_epi16(zero, diff);
        diff = _mm256_and_si256(diff, mask);
        res = _mm256_add_epi16(res, diff);
    }
    short buf[LANES_COUNT_SHORT];
    _mm256_storeu_si256((Vector*)buf, res);
    for (int i = 0; i < LANES_COUNT_SHORT; i++) {
        if (haystack_index + i >= files_length)
            break;

        bool keep_candidate = (float)ABS(buf[i]) / needle_length <
            PREFILTER_THRESHOLD;
        if (keep_candidate)
            *all_filtered_out = false;

        swimd_bitset_set(scores_prefiltering,
                haystack_index + i,
                keep_candidate);
    }
}

static bool swimd_scanner_simd_prefilter_flat(SwimdScanner *scanner, int profile_pack_start,
        int profile_pack_count) {

    int profile_pack_length = CEIL_DIV(scanner->files->length, LANES_COUNT_SHORT);
    int profile_pack_end = MIN(profile_pack_start + profile_pack_count,
            profile_pack_length);

    bool all_filtered_out = true;
    for (int i = profile_pack_start; i < profile_pack_end; i++) {
        swimd_scanner_simd_haystack_prefilter(
            scanner->needle_diff_chars,
            scanner->needle_diff_chars_frequency,
            scanner->needle_diff_chars_length,
            scanner->needle_length,
            scanner->files_profile_vec,
            scanner->files_profile_vec_char_length,
            i,
            &scanner->scores_prefiltering,
            scanner->files->length,
            &all_filtered_out
        );
    }
    return all_filtered_out;
}

static void swimd_scanner_scores_init(SwimdScanner *state) {
    state->scores = malloc(state->files_vec16_length * LANES_COUNT_SHORT * sizeof(short));
    state->scores_length = state->files_vec16_length * LANES_COUNT_SHORT;
    swimd_bitset_init(&state->scores_prefiltering, state->files->length);
}

static void swimd_scanner_scores_free(SwimdScanner *state) {
    free(state->scores);
    swimd_bitset_free(&state->scores_prefiltering);
}

static void swimd_scanner_top_scores_diagnostic(
        int min_score,
        int max_score,
        int score,
        int normalized_score,
        int len_diff_error,
        char *needle,
        char *file_name) {
    Nob_String_Builder sb = {0};

    nob_sb_append_cstr(&sb, "\n===== scores =====\n");
    nob_sb_appendf(&sb, "min =          %d\n", min_score);
    nob_sb_appendf(&sb, "max =          %d\n", max_score);
    nob_sb_appendf(&sb, "res =          %d\n", score);
    nob_sb_appendf(&sb, "nrm =          %d\n", normalized_score);
    nob_sb_appendf(&sb, "len_diff_err = %d\n", len_diff_error);
    nob_sb_appendf(&sb, "needle = %s\n", needle);
    nob_sb_appendf(&sb, "file   = %s\n", file_name);
    swimd_log_append(SWIMD_DEBUG, sb.items);

    nob_sb_free(sb);
}

int swimd_scanner_compare_heap_item(const void *a, const void *b) {
    SwimdHeapItem *a_item = (SwimdHeapItem*)a;
    SwimdHeapItem *b_item = (SwimdHeapItem*)b;
    if (a_item->score == b_item->score)
        return 0;
    return a_item->score > b_item->score ? 1 : -1;
}

static void swimd_scanner_top_scores_merge_and_sort(SwimdScanner *scanner) {
    swimd_heap_merge_many(scanner->scores_heaps, scanner->scores_heaps_count);
    qsort(scanner->scores_heaps[0].arr,
            scanner->scores_heaps[0].size,
            sizeof(SwimdHeapItem),
            swimd_scanner_compare_heap_item);
}

static void swimd_scanner_top_scores_flat(SwimdScanner *scanner,
        int file_vec_start, int file_vec_count, int heap_ind) {

    int file_vec_end = MIN(file_vec_start + file_vec_count,
            scanner->files_vec16_length);
    int haystack_start = LANES_COUNT_SHORT * file_vec_start;
    int haystack_end = MIN(file_vec_end * LANES_COUNT_SHORT, scanner->files->length);

    for (int i = haystack_start; i < haystack_end; i++) {
        if (!swimd_bitset_get(&scanner->scores_prefiltering, i))
            continue;
        int min_score, max_score;
        SwimdScannerFile *file = &scanner->files->arr[i];
        swimd_scanner_score_minmax(scanner->needle_length,
                file->name_length,
                scanner->gap_distr_sum,
                &min_score,
                &max_score);
        short score = scanner->scores[i];
        if (score < min_score || score > max_score) {
            swimd_log_append(SWIMD_ERR, "Score outside of the borders needle '%s' file '%s' score %d min %d max %d",
                    scanner->needle,
                    file->name,
                    score,
                    min_score,
                    max_score);
            assert(min_score <= score && score <= max_score);
        }

        short normalized_score = (short)((score - min_score) / (double)(max_score - min_score) * 100);
        short len_diff_error = ABS(scanner->needle_length - file->name_length) /
            (double)(MAX(scanner->needle_length, file->name_length)) * LEN_DIFF_ERROR_COST * normalized_score;
        normalized_score -= len_diff_error;

        if (normalized_score < ERROR_THRESHOLD * 100) {
            continue;
        }

        swimd_heap_insert(&scanner->scores_heaps[heap_ind], (SwimdHeapItem){
                .score = normalized_score,
                .index = i
        });
    }
}

static void swimd_scanner_init_root_folder(SwimdScannerFolderStruct *root) {
    root->parent = NULL;
    root->name = NULL;
    root->name_length = 0;
}

static void swimd_scanner_free(SwimdScanner *scanner) {
    swimd_scanner_scores_free(scanner);
    swimd_scanner_prep_files_vec_free(scanner);
    swimd_scanner_list_directories_free(scanner->files,
            scanner->folders);
    swimd_scanner_folders_free(&scanner->folders->folder_lst);
    swimd_scanner_file_list_free(scanner->files);

    free(scanner->files);
    free(scanner->folders);
    free(scanner->base_path);
}

static void swimd_scanner_watch_scanner_callback(bool *ignore, SwimdWatchOwner *owner) {
    for (int i = 0; i < SCANNER_COUNT; i++) {
        if (swimd_scanner_scanners[i].scan_in_progress) {
            // scan running, don't latch as handled
            *ignore = true;
            return;
        }
    }

    *ignore = false;
    swimd_are_set(&owner->notification_are_raised);
}

static void swimd_scanner_start_scanning(const char *root_path, SwimdScanner *scanner, bool refreshing) {
    swimd_log_append(SWIMD_INFO, "Scanning path started, refreshing=%d, path=%s", refreshing, root_path);

    SwimdScannerFileList *files = malloc(sizeof(SwimdScannerFileList));
    SwimdScannerFolderStruct *folders = malloc(sizeof(SwimdScannerFolderStruct));

    char *base_path = malloc(MAX_PATH_LENGTH * sizeof(char));
    swimd_scanner_init_root_folder(folders);

    swimd_scanner_file_list_init(files);
    swimd_scanner_folders_init(&folders->folder_lst);

    swimd_watch_init(scanner->watch_owner);

    scanner->scanning_func(root_path,
            base_path,
            files,
            folders,
            scanner->watch_owner,
            refreshing);

    swimd_crit_lock(&scanner->scan_state_swap);

    if (refreshing) {
        swimd_scanner_free(scanner);
    }

    scanner->files = files;
    scanner->folders = folders;
    scanner->base_path = base_path;
    if (refreshing) {
        scanner->scan_files_count = scanner->scan_files_refresh_count;
    }

    swimd_scanner_prep_files_vec(scanner);
    swimd_scanner_scores_init(scanner);

    swimd_watch_begin_tracking(scanner->watch_owner,
            swimd_scanner_watch_scanner_callback);

    swimd_crit_unlock(&scanner->scan_state_swap);

    swimd_log_append(SWIMD_INFO, "Scanning path completed");
}

static void swimd_scanner_scanning_loop_impl(SwimdScanner *scanner) {
    swimd_log_append(SWIMD_INFO, "Scanning loop start");
    while (1) {
        swimd_are_wait(&scanner->scan_begin);

        if (scanner->scan_terminate_request)
            break;

        swimd_mre_reset(&scanner->scan_finished);

        swimd_are_set(&scanner->scan_started);

        swimd_scanner_start_scanning(scanner->scan_path,
                scanner, scanner->scan_is_refreshing);

        scanner->scan_in_progress = false;
        scanner->scan_is_refreshing = false;

        swimd_mre_set(&scanner->scan_finished);
    }
    swimd_log_append(SWIMD_INFO, "Scanning loop exit");
}

#ifdef _WIN32
static DWORD WINAPI swimd_scanner_scanning_loop_git(LPVOID lp_param) {
    (void)lp_param;
    swimd_scanner_scanning_loop_impl(&swimd_scanner_scanners[SCANNER_GIT]);
    return 0;
}

static DWORD WINAPI swimd_scanner_scanning_loop_files(LPVOID lp_param) {
    (void)lp_param;
    swimd_scanner_scanning_loop_impl(&swimd_scanner_scanners[SCANNER_FILES]);
    return 0;
}
#else
static void* swimd_scanner_scanning_loop_git(void *lp_param) {
    (void)lp_param;
    swimd_scanner_scanning_loop_impl(&swimd_scanner_scanners[SCANNER_GIT]);
    return NULL;
}

static void* swimd_scanner_scanning_loop_files(void *lp_param) {
    (void)lp_param;
    swimd_scanner_scanning_loop_impl(&swimd_scanner_scanners[SCANNER_FILES]);
    return NULL;
}
#endif

static void swimd_scan_thread_init(SwimdScanner *scanner) {
    swimd_are_init(&scanner->scan_begin, false);
    swimd_are_init(&scanner->scan_started, false);
    swimd_mre_init(&scanner->scan_finished, true);

    swimd_thread_create(&scanner->scan_thread, scanner->scanning_loop, NULL);

    swimd_crit_init(&scanner->scan_state_swap);
}

static void swimd_d_vec_unit_init(SwimdScanner *scanner, int d_ind) {
    short* d_unit = malloc(MAX_PATH_LENGTH * MAX_PATH_LENGTH * LANES_COUNT_SHORT * sizeof(short));
    memset(d_unit, 0, MAX_PATH_LENGTH * MAX_PATH_LENGTH * LANES_COUNT_SHORT * sizeof(short));
    for (int i = 1; i < MAX_PATH_LENGTH; i++) {
        short value = d_unit[D_IND(i - 1, 0)];
        value += scanner->gap_distr_fun[i - 1];
        for (int j = 0; j < LANES_COUNT_SHORT; j++) {
            d_unit[D_IND(i, 0) + j] = value;
        }
    }
    for (int i = 1; i < MAX_PATH_LENGTH; i++) {
        short value = d_unit[D_IND(0, i - 1)];
        value += scanner->gap_distr_fun[i - 1];
        for (int j = 0; j < LANES_COUNT_SHORT; j++) {
            d_unit[D_IND(0, i) + j] = value;
        }
    }
    scanner->d_vec[d_ind] = d_unit;
}

static void swimd_d_vec_init(SwimdScanner *scanner, int count) {
    scanner->d_vec_count = count;
    scanner->d_vec = malloc(count * sizeof(short*));
    for (int i = 0; i < count; i++) {
        swimd_d_vec_unit_init(scanner, i);
    }
}

static void swimd_d_vec_free(SwimdScanner *scanner) {
    for (int i = 0; i < scanner->d_vec_count; i++) {
        free(scanner->d_vec[i]);
    }
    free(scanner->d_vec);
}

static void swimd_gap_distr_fun_custom(short *arr, int n) {
    int gap_ind = 0;
    int ind = 0;
    for (;;) {
        const int *gap_row = GAP_PENALTY[gap_ind];
        gap_ind++;
        if (gap_row[1] == -1) {
            for (int i = ind; i < n; i++) {
                arr[i] = gap_row[0];
            }
            return;
        }
        for (int i = 0; i < gap_row[1]; i++) {
            if (ind >= n)
                return;
            arr[ind] = gap_row[0];
            ind++;
        }
    }
}

static void swimd_gap_distr_fun(short *arr, int n) {
    swimd_gap_distr_fun_custom(arr, n);
}

static void swimd_gap_distr_sum(short *sum, short *arr, int n) {
    sum[0] = 0;
    for (int i = 1; i < n; i++) {
        sum[i] = sum[i - 1] + arr[i - 1];
    }
}

static void swimd_gap_distr_init(SwimdScanner *scanner) {
    scanner->gap_distr_fun = malloc(MAX_PATH_LENGTH * sizeof(short));
    scanner->gap_distr_sum = malloc(MAX_PATH_LENGTH * sizeof(short));
    swimd_gap_distr_fun(scanner->gap_distr_fun, MAX_PATH_LENGTH);
    swimd_gap_distr_sum(scanner->gap_distr_sum, scanner->gap_distr_fun, MAX_PATH_LENGTH);
}

static void swimd_gap_distr_free(SwimdScanner *scanner) {
    free(scanner->gap_distr_fun);
    free(scanner->gap_distr_sum);
}

static void swimd_scan_glob_init(SwimdScanner *scanner) {
    swimd_gap_distr_init(scanner);
    swimd_d_vec_init(scanner, swimd_scanner_est_workers.est_threads_count + 1);
    swimd_scan_thread_init(scanner);
}

void swimd_scanner_scanners_glob_init(void) {
    for (int i = 0; i < SCANNER_COUNT; i++) {
        swimd_scan_glob_init(&swimd_scanner_scanners[i]);
    }
}

static void swimd_scan_path_free(SwimdScanner *scanner) {
    free(scanner->scan_path);
}

static void swimd_scan_thread_stop(SwimdScanner *scanner) {
    scanner->scan_cancelled_request = true;
    swimd_mre_wait(&scanner->scan_finished);
    scanner->scan_cancelled_request = false;

    scanner->scan_terminate_request = true;
    swimd_are_set(&scanner->scan_begin);
    swimd_thread_join(&scanner->scan_thread);
    scanner->scan_terminate_request = false;
    scanner->scan_terminated = true;

    if (scanner->scan_path != NULL) {
        swimd_scan_path_free(scanner);
        swimd_scanner_free(scanner);
        scanner->scan_path = NULL;
    }

    swimd_are_close(&scanner->scan_begin);
    swimd_are_close(&scanner->scan_started);
    swimd_mre_close(&scanner->scan_finished);
    swimd_thread_close(&scanner->scan_thread);

    swimd_crit_close(&scanner->scan_state_swap);
}

static void swimd_scanner_scanners_thread_stop(void) {
    for (int i = 0; i < SCANNER_COUNT; i++) {
        swimd_scan_thread_stop(&swimd_scanner_scanners[i]);
    }
}

static void swimd_scan_glob_free(SwimdScanner *scanner) {
    swimd_gap_distr_free(scanner);
    swimd_d_vec_free(scanner);
}

static void swimd_scanner_scanners_glob_free(void) {
    for (int i = 0; i < SCANNER_COUNT; i++) {
        swimd_scan_glob_free(&swimd_scanner_scanners[i]);
    }
}

static void swimd_scanner_scan_setup_path(const char *scan_path, SwimdScanner *scanner) {
    scanner->scan_cancelled_request = true;
    swimd_mre_wait(&scanner->scan_finished);
    scanner->scan_cancelled_request = false;

    if (scanner->scan_path != NULL) {
        swimd_scan_path_free(scanner);
        swimd_scanner_free(scanner);
        scanner->scan_path = NULL;
    }

    int scan_path_len = strlen(scan_path);
    scanner->scan_path = malloc((scan_path_len + 1) * sizeof(char));
    strcpy(scanner->scan_path, scan_path);

    scanner->scan_in_progress = true;
    scanner->scan_files_count = 0;
    scanner->scan_files_refresh_count = 0;
    swimd_are_set(&scanner->scan_begin);
    // need to wait until we start scanning, in order not to messup in cleanup when state has been not initialized
    swimd_are_wait(&scanner->scan_started);
}

void swimd_scanner_scanners_setup_path(const char *scan_path) {
    for (int i = 0; i < SCANNER_COUNT; i++) {
        swimd_scanner_scan_setup_path(scan_path, &swimd_scanner_scanners[i]);
    }
}

bool swimd_scanner_scan_is_refreshing(SwimdScanner *scanner, int *read_count) {
    bool res = scanner->scan_in_progress;
    if (!scanner->scan_is_refreshing) {
        // maybe refresh was called before we completed initial setup.
        // let's just show ititial scan progress
        *read_count = scanner->scan_files_count;
    } else {
        *read_count = scanner->scan_files_refresh_count;
    }
    return res;
}

void swimd_scanner_scan_refresh_path(SwimdScanner *scanner) {
    if (scanner->scan_in_progress)
        return;

    scanner->scan_in_progress = true;
    scanner->scan_is_refreshing = true;
    scanner->scan_files_refresh_count = 0;
    swimd_are_set(&scanner->scan_begin);
    // need to wait until we start scanning, in order not to messup in cleanup when state has been not initialized
    swimd_are_wait(&scanner->scan_started);
}

void swimd_scanner_scanners_watch_end_tracking(void) {
    for (int i = 0; i < SCANNER_COUNT; i++) {
        SwimdScanner *scanner = &swimd_scanner_scanners[i];
        SwimdWatchOwner *scanner_owner = scanner->watch_owner;
        if (scanner_owner->has_watch) {
            swimd_watch_end_tracking(scanner_owner);
            scanner_owner->watch_terminated = true;
        }
    }
}

static void swimd_scanner_watch_owner_notification_scanner_handler(SwimdWatchOwner *owner) {
    (void)owner;
    swimd_log_append(SWIMD_INFO, "Refreshing workspace, requested from watch notification handler");
    swimd_crit_lock(&swimd_scanner_scanners_request_lock);
    for (int i = 0; i < SCANNER_COUNT; i++) {
        SwimdScanner *scanner = &swimd_scanner_scanners[i];
        if (scanner->scan_in_progress || scanner->scan_terminated)
            goto cleanup;
    }

    swimd_scanner_scanners_watch_end_tracking();

    for (int i = 0; i < SCANNER_COUNT; i++) {
        swimd_scanner_scan_refresh_path(&swimd_scanner_scanners[i]);
    }

cleanup:
    swimd_crit_unlock(&swimd_scanner_scanners_request_lock);
    swimd_log_append(SWIMD_INFO, "Refresh requested");
}


static void swimd_scanner_str_shift_right(char *buf, int buf_length, int n) {
    memmove(buf + n, buf, buf_length);
    buf[buf_length + n] = '\0';
}

static void swimd_scanner_str_shift_left(char *buf, int buf_length, int n) {
    memmove(buf, buf + n, buf_length - n);
    buf[buf_length - n] = '\0';
}

static void swimd_scanner_str_reverse(char *buf, int buf_length) {
    for (int i = 0; i < buf_length / 2; i++) {
        SWAP(buf[i], buf[buf_length - i - 1], char);
    }
}

static void swimd_scanner_print_relative(char *file_path, char *scan_path, char *base_path) {
    int base_path_len = strlen(base_path);
    int scan_path_len = strlen(scan_path);

    if (base_path_len == scan_path_len)
        return;
    int file_path_len = strlen(file_path);

    int scan_path_depth = swimd_scanner_path_depth(scan_path, PATH_SLASH_CHAR);
    int absolute_match_count = swimd_scanner_path_match_depth(base_path, scan_path, PATH_SLASH_CHAR);

    int up_count = scan_path_depth - absolute_match_count;

    int relative_match_depth = swimd_scanner_path_match_depth(file_path, scan_path + base_path_len + 1, PATH_SLASH_CHAR);
    up_count -= relative_match_depth;

    int relative_math_len = swimd_scanner_path_length(file_path, relative_match_depth, PATH_SLASH_CHAR);
    if (file_path[relative_math_len] == PATH_SLASH_CHAR)
        relative_math_len++;

    swimd_scanner_str_shift_left(file_path, file_path_len, relative_math_len);

    int pref_len = up_count * 3;
    swimd_scanner_str_shift_right(file_path, file_path_len, pref_len);

    for (int i = 0; i < up_count; i++) {
        file_path[3*i + 0] = '.';
        file_path[3*i + 1] = '.';
        file_path[3*i + 2] = PATH_SLASH_CHAR;
    }
}

static void swimd_scanner_print_path(char *buf, SwimdScannerFile *file) {
    int buf_length = 0;
    for (int i = 0; i < file->name_length; i++) {
        buf[buf_length++] = file->name[file->name_length - i - 1];
    }
    buf[buf_length++] = PATH_SLASH_CHAR;
    SwimdScannerFolderStruct *cur_folder = file->folder;
    while (1) {
        for (int i = 0; i < cur_folder->name_length; i++) {
            buf[buf_length++] = cur_folder->name[cur_folder->name_length - i - 1];
        }
        if (IS_ROOT_FOLDER(cur_folder))
            break;
        buf[buf_length++] = PATH_SLASH_CHAR;
        cur_folder = cur_folder->parent;
    }
    buf_length--;
    swimd_scanner_str_reverse(buf, buf_length);
    buf[buf_length++] = '\0';
}

static void swimd_scanner_process_input(const char *needle,
        int max_size,
        SwimdScannerProcessInputResult *result,
        SwimdScanner *scanner) {
    swimd_scanner_setup_needle(needle, max_size, scanner);

    double t0 = swimd_scanner_now_us();
    swimd_scanner_simd_scores(scanner);

    double t1 = swimd_scanner_now_us();
    swimd_scanner_top_scores_merge_and_sort(scanner);

    double t2 = swimd_scanner_now_us();
    swimd_scanner_phase_times.prefilter_and_scores_us += t1 - t0;
    swimd_scanner_phase_times.top_us += t2 - t1;
    swimd_scanner_phase_times.count++;

    result->items = malloc(max_size * sizeof(SwimdScannerProcessInputResultItem));

    for (int i = 0; i < scanner->scores_heaps[0].size; i++) {
        SwimdHeapItem heap_item = scanner->scores_heaps[0].arr[i];
        SwimdScannerFile file = scanner->files->arr[heap_item.index];

        char path[MAX_PATH_LENGTH];
        swimd_scanner_print_path(path, &file);
        swimd_scanner_print_relative(path, scanner->scan_path, scanner->base_path);
        int path_length = strlen(path);

        SwimdScannerProcessInputResultItem item = {0};
        item.path = malloc((path_length + 1) * sizeof(char));
        strcpy(item.path, path);
        item.name = malloc((file.name_length + 1) * sizeof(char));
        strcpy(item.name, file.name);
        item.score = heap_item.score;

        result->items[result->items_length] = item;
        result->items_length++;
    }

    for (int i = 0; i < result->items_length / 2; i++) {
        SWAP(result->items[i], result->items[result->items_length - i - 1],
                SwimdScannerProcessInputResultItem);
    }

    swimd_scanner_setup_needle_free(scanner);
}

void swimd_scanner_scan_process_input(const char *input,
        int max_size,
        SwimdScannerProcessInputResult *result,
        SwimdScanner *scanner) {

    swimd_crit_lock(&scanner->scan_state_swap);

    result->scanned_items_count = scanner->scan_files_count;

    if (scanner->scan_in_progress && !scanner->scan_is_refreshing) {
        result->scan_in_progress = true;
    } else {
        result->scan_in_progress = false;
        swimd_scanner_process_input(input, max_size, result, scanner);
    }

    swimd_crit_unlock(&scanner->scan_state_swap);
}

void swimd_scanner_scan_process_input_free(SwimdScannerProcessInputResult *result) {
    if (result->items == NULL)
        return;

    for (int i = 0; i < result->items_length; i++) {
        SwimdScannerProcessInputResultItem item = result->items[i];
        free(item.path);
        free(item.name);
    }
    free(result->items);
}

static void swimd_scanner_watch_owners_shutdown(void) {
    for (int i = 0; i < SCANNER_COUNT; i++) {
        SwimdScanner *scanner = &swimd_scanner_scanners[i];
        swimd_watch_owner_end(scanner->watch_owner);
    }
}

void swimd_scanner_shutdown(void) {
    if (!swimd_scanner_initialized) {
        return;
    }
    swimd_log_append(SWIMD_INFO, "Shutting down");

    swimd_crit_lock(&swimd_scanner_scanners_request_lock);
    swimd_scanner_scanners_thread_stop();
    swimd_crit_unlock(&swimd_scanner_scanners_request_lock);

    swimd_scanner_watch_owners_shutdown();

    swimd_scanner_scanners_glob_free();

    swimd_log_append(SWIMD_INFO, "Shutting down completed");

    swimd_scanner_global_free();

    swimd_scanner_initialized = false;
}
