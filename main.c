#ifdef _WIN32
    #include <windows.h>
#else
    #include <pthread.h>  
    #include <dirent.h>  
#endif
#include <stdbool.h>
#include <string.h>

#include <assert.h>
#include <immintrin.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>
#include <stdlib.h>
#include <time.h>
#include "lua.h"
#include "lauxlib.h"
#include "git2.h"

#define NOB_IMPLEMENTATION
#undef ERROR
#include "nob.h"

#ifdef _WIN32
    #define EXPORT __declspec(dllexport)
#else
    #define EXPORT __attribute__((visibility("default")))
#endif

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

#define LEFT_HEAP(ind) (2*((ind) + 1) - 1)
#define RIGHT_HEAP(ind) (2*((ind) + 1))
#define PARENT_HEAP(ind) (((ind) - 1) / 2)
#define SWAP(a, b, type) \
    do {                 \
        type temp = a;   \
        a = b;           \
        b = temp;        \
    } while (0)

#ifdef _WIN32
typedef DWORD (*swimd_thread_callback)(LPVOID);

static void swimd_thread_create(HANDLE *t, swimd_thread_callback callback) {
    *t = CreateThread(NULL, 0, callback, NULL, 0, NULL);
}

static void swimd_thread_join(HANDLE *t) {
    WaitForSingleObject(*t, INFINITE);
}

static void swimd_thread_close(HANDLE *t) {
    CloseHandle(*t);
}

static void swimd_crit_init(CRITICAL_SECTION *lock) {
    InitializeCriticalSection(lock);
}

static void swimd_crit_lock(CRITICAL_SECTION *lock) {
    EnterCriticalSection(lock);
}

static void swimd_crit_unlock(CRITICAL_SECTION *lock) {
    LeaveCriticalSection(lock);
}

static void swimd_crit_close(CRITICAL_SECTION *lock) {
    DeleteCriticalSection(lock);
}

static void swimd_are_init(HANDLE *ev, bool initial_state) {
    *ev = CreateEvent(NULL, false, initial_state, NULL);
}

static void swimd_are_wait(HANDLE *ev) {
    WaitForSingleObject(*ev, INFINITE);
}

static void swimd_are_set(HANDLE *ev) {
    SetEvent(*ev);
}

static void swimd_are_close(HANDLE *ev) {
    CloseHandle(*ev);
}

static void swimd_mre_init(HANDLE *ev, bool initial_state) {
    *ev = CreateEvent(NULL, true, initial_state, NULL);
}

static void swimd_mre_wait(HANDLE *ev) {
    WaitForSingleObject(*ev, INFINITE);
}

static void swimd_mre_set(HANDLE *ev) {
    SetEvent(*ev);
}

static void swimd_mre_reset(HANDLE *ev) {
    ResetEvent(*ev);
}

static void swimd_mre_close(HANDLE *ev) {
    CloseHandle(*ev);
}
#else
typedef void* (*swimd_thread_callback)(void*);

static void swimd_thread_create(pthread_t *t, swimd_thread_callback callback) {
    pthread_create(t, NULL, callback, NULL);
}

static void swimd_thread_join(pthread_t *t) {
   pthread_join(*t, NULL);
}

static void swimd_thread_close(pthread_t *t) {
}

static void swimd_crit_init(pthread_mutex_t *lock) {
    pthread_mutex_init(lock, NULL);
}

static void swimd_crit_lock(pthread_mutex_t *lock) {
    pthread_mutex_lock(lock);
}

static void swimd_crit_unlock(pthread_mutex_t *lock) {
    pthread_mutex_unlock(lock);
}

static void swimd_crit_close(pthread_mutex_t *lock) {
    pthread_mutex_destroy(lock);
}

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool signaled;
} SwimdAutoResetEvent;

static void swimd_are_init(SwimdAutoResetEvent *ev, bool initial_state) {
    pthread_mutex_init(&ev->mutex, NULL);
    pthread_cond_init(&ev->condition, NULL);
    ev->signaled = initial_state;
}

static void swimd_are_wait(SwimdAutoResetEvent *ev) {
    pthread_mutex_lock(&ev->mutex);
    while (!ev->signaled) {
        pthread_cond_wait(&ev->condition, &ev->mutex);
    }
    ev->signaled = false;
    pthread_mutex_unlock(&ev->mutex);
}

static void swimd_are_set(SwimdAutoResetEvent *ev) {
    pthread_mutex_lock(&ev->mutex);
    ev->signaled = true;
    pthread_mutex_unlock(&ev->mutex);
    pthread_cond_signal(&ev->condition);
}

static void swimd_are_close(SwimdAutoResetEvent *ev) {
    pthread_mutex_destroy(&ev->mutex);
    pthread_cond_destroy(&ev->condition);
}

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool signaled;
} SwimdManualResetEvent;

static void swimd_mre_init(SwimdManualResetEvent *ev, bool initial_state) {
    pthread_mutex_init(&ev->mutex, NULL);
    pthread_cond_init(&ev->condition, NULL);
    ev->signaled = initial_state;
}

static void swimd_mre_wait(SwimdManualResetEvent *ev) {
    pthread_mutex_lock(&ev->mutex);
    while (!ev->signaled) {
        pthread_cond_wait(&ev->condition, &ev->mutex);
    }
    pthread_mutex_unlock(&ev->mutex);
}

static void swimd_mre_set(SwimdManualResetEvent *ev) {
    pthread_mutex_lock(&ev->mutex);
    ev->signaled = true;
    pthread_cond_broadcast(&ev->condition);
    pthread_mutex_unlock(&ev->mutex);
}

static void swimd_mre_reset(SwimdManualResetEvent *ev) {
    pthread_mutex_lock(&ev->mutex);
    ev->signaled = false;
    pthread_mutex_unlock(&ev->mutex);
}

static void swimd_mre_close(SwimdManualResetEvent *ev) {
    pthread_mutex_destroy(&ev->mutex);
    pthread_cond_destroy(&ev->condition);
}
#endif

typedef enum {
    SWIMD_INFO,
    SWIMD_WARN,
    SWIMD_ERR,
    SWIMD_DEBUG,
} SwimdLogLevel;

typedef struct  {
    char *path;
    char *name;
    int score;
} SwimdProcessInputResultItem;

typedef struct {
    bool scan_in_progress;
    int scanned_items_count;
    SwimdProcessInputResultItem *items;
    int items_length;
} SwimdProcessInputResult;

typedef struct SwimdFolderStruct SwimdFolderStruct;

typedef struct {
    SwimdFolderStruct **arr;
    int length;
    int capacity;
} SwimdFolderStructList;

typedef struct SwimdFolderStruct
{
    char *name;
    int name_length;
    SwimdFolderStructList folder_lst;
    struct SwimdFolderStruct *parent;
} SwimdFolderStruct;

typedef struct {
    char *name;
    int name_length;
    SwimdFolderStruct *folder;
} SwimdFile;

typedef struct {
    SwimdFile *arr;
    int length;
    int capacity;
} SwimdFileList;

typedef struct {
    short *arr;
    int length;
} SwimdFileVec;

typedef struct {
    int score;
    int index;
} SwimdScoresHeapItem;

typedef struct  {
    SwimdScoresHeapItem *arr;
    int size;
    int max_size;
} SwimdScoresHeap;

typedef void (*swimd_scanning_func)(const char*,
        char*,
        SwimdFileList*,
        SwimdFolderStruct*,
        bool);

typedef struct {
    bool initialized;

    char *needle;
    int needle_length;
    short *needle_vec;
    int needle_vec_length;

    SwimdFileList *files;
    SwimdFileVec *files_vec;
    int files_vec_length;

    SwimdFolderStruct *folders;
    short *scores;
    int scores_length;
    SwimdScoresHeap scores_heap;

    short *d_vec;
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
    volatile bool scan_terminate;
    volatile bool scan_cancelled;
    volatile bool scan_in_progress;
    volatile bool scan_is_refreshing;
    char *scan_path;
    char *base_path;
    int scan_files_count;
    int scan_files_refresh_count;

    swimd_scanning_func scanning_func;
    swimd_thread_callback scanning_loop;
} SwimdScanner;

static void swimd_list_files(const char *root_dir,
        char *base_path,
        SwimdFileList *file_list,
        SwimdFolderStruct *root_folder,
        bool refreshing);
static void swimd_list_git(const char *root_dir,
        char *base_path,
        SwimdFileList *file_list,
        SwimdFolderStruct *root_folder,
        bool refreshing);

#ifdef _WIN32
static DWORD WINAPI swimd_scanning_loop_git(LPVOID lp_param);
static DWORD WINAPI swimd_scanning_loop_files(LPVOID lp_param);
#else
static void* swimd_scanning_loop_git(void* lp_param);
static void* swimd_scanning_loop_files(void* lp_param);
#endif

static bool swimd_initialized = false;
static SwimdScanner swimd_scanners[SCANNER_COUNT] = {0};
static FILE *swimd_log = {0};
static bool swimd_log_enabled = false;

static void swimd_git2_init(void) {
    git_libgit2_init();
}

static void swimd_log_init(const char *log_path) {
    if (log_path == NULL) {
        return;
    }
    swimd_log_enabled = true;
    swimd_log = fopen(log_path, "a");
    if (swimd_log == NULL) {
        fprintf(stderr, "Unable to init log file");
        exit(1);
    }
}

static void swimd_scanner_init_git() {
    swimd_scanners[SCANNER_GIT].scanning_func = &swimd_list_git;
    swimd_scanners[SCANNER_GIT].scanning_loop = &swimd_scanning_loop_git;
}

static void swimd_scanner_init_files() {
    swimd_scanners[SCANNER_FILES].scanning_func = &swimd_list_files;
    swimd_scanners[SCANNER_FILES].scanning_loop = &swimd_scanning_loop_files;
}

static void swimd_global_init(const char *log_path) {
    swimd_log_init(log_path);
    swimd_git2_init();
    swimd_scanner_init_git();
    swimd_scanner_init_files();
}

static void swimd_log_free(void) {
    if (!swimd_log_enabled) {
        return;
    }
    swimd_log_enabled = false;
    fclose(swimd_log);
}

static void swimd_git2_free(void) {
    git_libgit2_shutdown();
}

static void swimd_global_free(void) {
    swimd_git2_free();
    swimd_log_free();
}

static void swimd_log_append(SwimdLogLevel level, const char *msg, ...) {
    if (!swimd_log_enabled) {
        return;
    }
    const char *level_str;
    switch (level) {
        case SWIMD_INFO: 
            level_str = "INFO";
            break;
        case SWIMD_WARN: 
            level_str = "WARN";
            break;
        case SWIMD_ERR: 
            level_str = "ERR";
            break;
        case SWIMD_DEBUG:
            level_str = "DEBUG";
            break;
    }
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    time_t seconds = ts.tv_sec;

    struct tm t;
#ifdef _WIN32
    localtime_s(&t, &seconds);
#else
    localtime_r(&seconds, &t);
#endif

#ifdef DEBUG_PRINT
    printf("%04d-%02d-%02dT%02d:%02d:%02d.%09d ",
        t.tm_year+1900,
        t.tm_mon+1,
        t.tm_mday,
        t.tm_hour,
        t.tm_min,
        t.tm_sec,
        ts.tv_nsec
    );
    printf("[%s] ", level_str);
#endif

    fprintf(swimd_log, "%04d-%02d-%02dT%02d:%02d:%02d.%09d ",
        t.tm_year+1900,
        t.tm_mon+1,
        t.tm_mday,
        t.tm_hour,
        t.tm_min,
        t.tm_sec,
        ts.tv_nsec
    );
    fprintf(swimd_log, "[%s] ", level_str);

    va_list args;
    va_start(args, msg);

#ifdef DEBUG_PRINT
    va_list args_copy;
    va_copy(args_copy, args);
    vprintf(msg, args_copy);
    va_end(args_copy);
#endif
    vfprintf(swimd_log, msg, args);
    va_end(args);

#ifdef DEBUG_PRINT
    printf("\n");
#endif
    fprintf(swimd_log, "\n");
    fflush(swimd_log);
}

static void swimd_folders_init(SwimdFolderStructList *lst) {
    int default_size = 4;
    lst->arr = malloc(default_size * sizeof(SwimdFolderStruct*));
    lst->length = 0;
    lst->capacity = default_size;
}

static void swimd_folders_append(SwimdFolderStructList *lst, SwimdFolderStruct *folder) {
    if (lst->length == lst->capacity) {
        lst->capacity = lst->capacity * 2;
        lst->arr = realloc(lst->arr, lst->capacity * sizeof(SwimdFolderStruct*));
    }
    lst->arr[lst->length] = folder;
    lst->length++;
}

static void swimd_folders_free(SwimdFolderStructList *lst) {
    free(lst->arr);
}

static void swimd_file_list_init(SwimdFileList *lst) {
    int default_size = 4;
    lst->arr = malloc(default_size * sizeof(SwimdFile));
    lst->length = 0;
    lst->capacity = default_size;
}

static void swimd_file_list_append(SwimdFileList *lst, SwimdFile file) {
    if (lst->length == lst->capacity) {
        lst->capacity = lst->capacity * 2;
        lst->arr = realloc(lst->arr, lst->capacity * sizeof(SwimdFile));
    }
    lst->arr[lst->length] = file;
    lst->length++;
    if (lst->length % 10000 == 0)
        swimd_log_append(SWIMD_INFO, "Scanned file count %d", lst->length);
}

static void swimd_file_list_free(SwimdFileList *lst) {
    lst->length = 0;
    free(lst->arr);
}

#ifdef _WIN32
static void swimd_list_files_win32(const char *root_dir,
        SwimdFileList *file_list,
        SwimdFolderStruct *root_folder,
        bool refreshing) {
    SwimdScanner *scanner = &swimd_scanners[SCANNER_FILES];
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

    while (1) {
        const char *current_file = find_file_data.cFileName;
        int current_file_len = strlen(current_file);

        if (find_file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (strcmp(current_file, ".") != 0 && strcmp(current_file, "..") != 0) {
                char *folder_name = malloc((current_file_len + 1) * sizeof(char));
                strcpy(folder_name, current_file);
                folder_name[current_file_len] = '\0';

                SwimdFolderStruct *folder_node = malloc(sizeof(SwimdFolderStruct));

                folder_node->name = folder_name;
                folder_node->name_length = current_file_len;
                folder_node->parent = root_folder;

                swimd_folders_init(&folder_node->folder_lst);
                swimd_folders_append(&root_folder->folder_lst, folder_node);

                strcpy(inner_folder, root_dir);
                strcat(inner_folder, "\\");
                strcat(inner_folder, current_file);

                swimd_list_files_win32(inner_folder, file_list, folder_node, refreshing);
            }
        } else {
            char *file_name = malloc((current_file_len + 1) * sizeof(char));
            strcpy(file_name, current_file);
            file_name[current_file_len] = '\0';

            SwimdFile file_node = {
                .name = file_name,
                .name_length = current_file_len,
                .folder = root_folder
            };

            swimd_file_list_append(file_list, file_node);
            if (!refreshing)
                scanner->scan_files_count++;
            else
                scanner->scan_files_refresh_count++;
        }

        if (FindNextFile(h_find, &find_file_data) == 0)
            break;
        if (scanner->scan_cancelled)
            break;
    }

    if (!scanner->scan_cancelled)
    {
        DWORD dw_error = GetLastError();
        if (dw_error != ERROR_NO_MORE_FILES) {
            swimd_log_append(SWIMD_ERR, "FindNextFile error (%lu)", dw_error);
        }
    }

    FindClose(h_find);
}
#else 

static void swimd_list_files_linux(const char *root_dir,
        SwimdFileList *file_list,
        SwimdFolderStruct *root_folder,
        bool refreshing) {
    SwimdScanner *scanner = &swimd_scanners[SCANNER_FILES];
    char inner_folder[MAX_PATH_LENGTH];
    struct dirent *entry;
    DIR *dp = opendir(root_dir);
    if (dp == NULL) {
        swimd_log_append(SWIMD_ERR, "Unable to opendir %s", root_dir);
        return;
    }

    while ((entry = readdir(dp))) {
        const char *current_file = entry->d_name;
        int current_file_len = strlen(current_file);
        if (entry->d_type == DT_DIR) {
            if (strcmp(current_file, ".") != 0 && strcmp(current_file, "..") != 0) {
                char *folder_name = malloc((current_file_len + 1) * sizeof(char));
                strcpy(folder_name, current_file);
                folder_name[current_file_len] = '\0';

                SwimdFolderStruct *folder_node = malloc(sizeof(SwimdFolderStruct));
                folder_node->name = folder_name;
                folder_node->name_length = current_file_len;
                folder_node->parent = root_folder;

                swimd_folders_init(&folder_node->folder_lst);
                swimd_folders_append(&root_folder->folder_lst, folder_node);

                strcpy(inner_folder, root_dir);
                strcat(inner_folder, "/");
                strcat(inner_folder, current_file);

                swimd_list_files_linux(inner_folder, file_list, folder_node, refreshing);
            }
        } else if (entry->d_type == DT_REG) {
            char *file_name = malloc((current_file_len + 1) * sizeof(char));
            strcpy(file_name, current_file);
            file_name[current_file_len] = '\0';

            SwimdFile file_node = {
                .name = file_name,
                .name_length = current_file_len,
                .folder = root_folder
            };

            swimd_file_list_append(file_list, file_node);
            if (!refreshing)
                scanner->scan_files_count++;
            else
                scanner->scan_files_refresh_count++;
        }

        if (scanner->scan_cancelled)
            break;
    }

    closedir(dp);
}
#endif

static void swimd_list_files(const char *root_dir,
        char *base_path,
        SwimdFileList *file_list,
        SwimdFolderStruct *root_folder,
        bool refreshing) {
    int root_dir_len = strlen(root_dir);
    strncpy(base_path, root_dir, root_dir_len);
    base_path[root_dir_len] = '\0';

#ifdef _WIN32
    swimd_list_files_win32(root_dir,
        file_list,
        root_folder,
        refreshing);
#else
    swimd_list_files_linux(root_dir,
        file_list,
        root_folder,
        refreshing);
#endif
}


static void swimd_log_git2_error(const char *message, int error) {
    const char *lg2msg = "";

    if (!error)
        return;

    const git_error *lg2err = git_error_last();
    if (lg2err != NULL && lg2err->message != NULL) {
        lg2msg = lg2err->message;
    }

    swimd_log_append(SWIMD_ERR, "%s [%d] %s", message, error, lg2msg);
}

static int swimd_path_length(const char *a, int segment_count, char separator) {
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
static int swimd_path_depth(const char *a, char separator) {
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

static int swimd_path_match_depth(const char *a, const char *b, char separator) {
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

static SwimdFolderStruct* swimd_folder_go_up(SwimdFolderStruct *folder,
        int up_count) {
    while (up_count > 0) {
        folder = folder->parent;
        up_count--;
    }
    return folder;
}

static const char *swimd_path_skip_folder_count(const char *path,
        int folder_count, char separator) {
    while (folder_count > 0) {
        if (*path == separator) {
            folder_count--;
        }
        path++;
    }
    return path;
}

static SwimdFolderStruct* swimd_folder_find_child(const char *child_name,
        int child_name_length,
        SwimdFolderStruct *cur_folder) {
    for (int i = 0; i < cur_folder->folder_lst.length; i++) {
        SwimdFolderStruct *child_folder = cur_folder->folder_lst.arr[i];
        if (child_folder->name_length == child_name_length &&
                strncmp(child_name, child_folder->name, child_name_length) == 0) {
            return child_folder;
        }
    }
    return NULL;
}

static SwimdFolderStruct* swimd_process_path(const char *path,
        SwimdFileList *file_list,
        SwimdFolderStruct *cur_folder,
        const char *cur_path,
        int cur_depth,
        int *res_depth,
        bool refreshing) {
    SwimdScanner *scanner = &swimd_scanners[SCANNER_GIT];
    int match_depth = swimd_path_match_depth(path, cur_path, PATH_SLASH_GIT_CHAR);
    int up_count = cur_depth - match_depth;
    *res_depth = cur_depth - up_count;

    SwimdFolderStruct *base_folder = swimd_folder_go_up(cur_folder, up_count);
    const char *base_path = swimd_path_skip_folder_count(path, match_depth, PATH_SLASH_GIT_CHAR);

    int segment_len = 0;
    while (1) {
        if (base_path[segment_len] == '\0') {
            char *file_name = malloc((segment_len + 1) * sizeof(char));
            strncpy(file_name, base_path, segment_len);
            file_name[segment_len] = '\0';

            SwimdFile file_node = {
                .name = file_name,
                .name_length = segment_len,
                .folder = base_folder
            };

            swimd_file_list_append(file_list, file_node);

            if (!refreshing)
                scanner->scan_files_count++;
            else
                scanner->scan_files_refresh_count++;

            break;
        }

        if (base_path[segment_len] == PATH_SLASH_GIT_CHAR) {
            (*res_depth)++;

            SwimdFolderStruct *child_folder = swimd_folder_find_child(base_path,
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

            SwimdFolderStruct *folder_node = malloc(sizeof(SwimdFolderStruct));

            folder_node->name = folder_name;
            folder_node->name_length = segment_len;
            folder_node->parent = base_folder;

            swimd_folders_init(&folder_node->folder_lst);
            swimd_folders_append(&base_folder->folder_lst, folder_node);

            base_path += segment_len + 1;
            segment_len = 0;
            base_folder = folder_node;
            continue;
        }
        segment_len++;
    }
    return base_folder;
}

static void swimd_git_collect_index_paths(git_repository *repo,
        SwimdFileList *file_list,
        SwimdFolderStruct *root_folder,
        bool refreshing) {
    SwimdScanner *scanner = &swimd_scanners[SCANNER_GIT];
    if (scanner->scan_cancelled)
        return;

    git_index *index = NULL;

	int index_result = git_repository_index(&index, repo);
    if (index_result < 0) {
        swimd_log_git2_error("Unable to index git repository", index_result);
        goto cleanup;
    }

    size_t entry_count = git_index_entrycount(index);

    SwimdFolderStruct *cur_folder = root_folder;
    int cur_depth = 0;
    char cur_path[MAX_PATH_LENGTH] = "";

    for (int i = 0; i < entry_count; i++) {
        const git_index_entry *entry = git_index_get_byindex(index, i);
        int depth = 0;
        cur_folder = swimd_process_path(entry->path,
            file_list,
            cur_folder,
            cur_path,
            cur_depth,
            &depth,
            refreshing);
        cur_depth = depth;
        strcpy(cur_path, entry->path);

        if (scanner->scan_cancelled)
            break;
    }

cleanup:
	git_index_free(index);
}

static void swimd_git_collect_status_paths(git_repository *repo,
        SwimdFileList *file_list,
        SwimdFolderStruct *root_folder,
        bool refreshing) {
    SwimdScanner *scanner = &swimd_scanners[SCANNER_GIT];
    if (scanner->scan_cancelled)
        return;
    git_status_options status_opts = GIT_STATUS_OPTIONS_INIT;
    status_opts.show = GIT_STATUS_SHOW_WORKDIR_ONLY;
    status_opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
                        GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

    git_status_list *status_list = NULL;
    int list_result = git_status_list_new(&status_list, repo, &status_opts);
    if (list_result < 0) {
        swimd_log_git2_error("Unable to index git repository", list_result);
        goto cleanup;
    }

    SwimdFolderStruct *cur_folder = root_folder;
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
            cur_folder = swimd_process_path(path,
                file_list,
                cur_folder,
                cur_path,
                cur_depth,
                &depth,
                refreshing);
            cur_depth = depth;
            strcpy(cur_path, path);
        }
        if (scanner->scan_cancelled)
            break;
    }
cleanup:
    git_status_list_free(status_list);
}

static void swimd_list_git_normalize_base_path(char *base_path) {
    if (PATH_SLASH_CHAR == PATH_SLASH_GIT_CHAR) 
        return;
    int base_path_len = strlen(base_path);
    for (int i = 0; i < base_path_len; i++) {
        if (base_path[i] == PATH_SLASH_GIT_CHAR)
            base_path[i] = PATH_SLASH_CHAR;
    }

}
static void swimd_list_git(const char *root_dir,
        char *base_path,
        SwimdFileList *file_list,
        SwimdFolderStruct *root_folder,
        bool refreshing) {
    git_repository *repo = NULL;

    int repo_result = git_repository_open_ext(&repo, root_dir, 0, NULL);
    if (repo_result == GIT_ENOTFOUND) {
        swimd_log_append(SWIMD_INFO, "Not a git repository %s", root_dir);
        goto cleanup;
    } else if (repo_result < 0) {
        swimd_log_git2_error("Unable to open git repository", repo_result);
        goto cleanup;
    }

    const char *repo_path = git_repository_workdir(repo);
    int repo_path_length = strlen(repo_path);
    repo_path_length--; // cut last slash
    strncpy(base_path, repo_path, repo_path_length);
    base_path[repo_path_length] = '\0';
    swimd_list_git_normalize_base_path(base_path);

    swimd_git_collect_index_paths(repo,
            file_list,
            root_folder,
            refreshing);
    swimd_git_collect_status_paths(repo,
            file_list,
            root_folder,
            refreshing);
cleanup:
    git_repository_free(repo);
}

static void swimd_list_directories_folders_free(SwimdFolderStruct *root_folder) {
    for (int i = 0; i < root_folder->folder_lst.length; i++) {
        SwimdFolderStruct *folder = root_folder->folder_lst.arr[i];
        swimd_list_directories_folders_free(folder);
        swimd_folders_free(&folder->folder_lst);
        free(folder->name);
        free(folder);
    }
}

static void swimd_list_directories_free(const SwimdFileList *lst,
        SwimdFolderStruct *root_folder) {
    swimd_list_directories_folders_free(root_folder);
    for (int i = 0; i < lst->length; i++) {
        free(lst->arr[i].name);
    }
}

static void swimd_prep_files_vec(SwimdScanner *scanner) {
    SwimdFileList *files = scanner->files;
    int files_length = files->length;
    int files_vec_length = CEIL_DIV(files_length, LANES_COUNT_SHORT);
    SwimdFileVec *files_vec = malloc(files_vec_length * sizeof(SwimdFileVec));

    for (int i = 0; i < files_vec_length; i++) {
        int max_length = 0;
        for (int j = 0; j < LANES_COUNT_SHORT; j++) {
            if (i * LANES_COUNT_SHORT + j >= files_length)
                break;
            max_length = MAX(max_length, files->arr[i * LANES_COUNT_SHORT + j].name_length);
        }

        int file_vec_length = max_length * LANES_COUNT_SHORT;
        short *file_vec_arr = malloc(file_vec_length * sizeof(short));
        memset(file_vec_arr, 0, file_vec_length * sizeof(short));

        for (int k = 0; k < max_length; k++) {
            for (int j = 0; j < LANES_COUNT_SHORT; j++) {
                if (i * LANES_COUNT_SHORT + j >= files_length)
                    break;
                SwimdFile file = files->arr[i * LANES_COUNT_SHORT + j];
                if (k >= file.name_length)
                    continue;
                file_vec_arr[k * LANES_COUNT_SHORT + j] = (short)file.name[k];
            }
        }
        files_vec[i] = (SwimdFileVec){
            .arr = file_vec_arr,
            .length = file_vec_length,
        };
    }
    scanner->files_vec = files_vec;
    scanner->files_vec_length = files_vec_length;
}

static void swimd_prep_files_vec_free(SwimdScanner *state) {
    for (int i = 0; i < state->files_vec_length; i++) {
        SwimdFileVec file_vec = state->files_vec[i];
        free(file_vec.arr);
    }
    free(state->files_vec);
}

static void swimd_prep_needle_vec(SwimdScanner *state) {
    int needle_vec_length = state->needle_length  *LANES_COUNT_SHORT;
    short *needle_vec = malloc(needle_vec_length * sizeof(short));
    for (int i = 0; i < state->needle_length; i++) {
        for (int j = 0; j < LANES_COUNT_SHORT; j++) {
            needle_vec[i * LANES_COUNT_SHORT + j] = (short)state->needle[i];
        }
    }
    state->needle_vec = needle_vec;
    state->needle_vec_length = needle_vec_length;
}

static void swimd_prep_needle_vec_free(SwimdScanner *state) {
    free(state->needle_vec);
}

static void swimd_setup_needle(const char *needle, SwimdScanner *scanner) {
    int needle_length = strlen(needle);
    scanner->needle = malloc((needle_length + 1) * sizeof(char));
    strcpy(scanner->needle, needle);
    scanner->needle_length = needle_length;

    swimd_prep_needle_vec(scanner);
}

static void swimd_setup_needle_free(SwimdScanner *scanner) {
    free(scanner->needle);

    swimd_prep_needle_vec_free(scanner);
}

void swimd_vec_estimate_diagnostic(short *d,
        int ind,
        char* needle,
        int needle_length,
        char* file_name,
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

static void swimd_score_minmax(int needle_len,
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

static inline Vector swimd_simd_filter_az(Vector x) {
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

static inline Vector swimd_simd_az_inverse_case(Vector x) {
    Vector zero = _mm256_setzero_si256();
    Vector az = swimd_simd_filter_az(x);
    Vector mask = _mm256_cmpeq_epi16(az, zero);
    Vector sign = _mm256_set1_epi16('a' ^ 'A');
    Vector flipped = _mm256_xor_si256(az, sign);
    Vector clear = _mm256_andnot_si256(mask, flipped);
    return clear;
}

static void swimd_simd_haystack_scores(short *d,
    short *needle_vec,
    int needle_vec_length,
    short *haystack_vec,
    int haystack_vec_length,
    int haystack_index,
    short *scores,
    short *gap_distr_fun,
    SwimdFileList *files,
    char* needle
) {
    int needle_length = needle_vec_length / LANES_COUNT_SHORT;
    int haystack_max_length = haystack_vec_length / LANES_COUNT_SHORT;

    Vector sub_pen = _mm256_set1_epi16(SUB_PENALTY);
    Vector eq_reward = _mm256_set1_epi16(MATCH_STRICT_REWARD);
    Vector cis_reward = _mm256_set1_epi16(MATCH_CASE_INSENSITIVE_REWARD);

    for (int i = 1; i <= needle_length; i++) {
        Vector gap_pen_i = _mm256_set1_epi16(gap_distr_fun[i - 1]);
        Vector va = _mm256_loadu_si256((Vector const*)&needle_vec[LANES_COUNT_SHORT * (i - 1)]);
        Vector vca = swimd_simd_az_inverse_case(va);
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
        char *file_name = files->arr[file_index].name;
        int file_name_length = files->arr[file_index].name_length;
        scores[file_index] = d[D_IND(needle_length, file_name_length) + i];
#ifdef DEBUG_PRINT
        swimd_vec_estimate_diagnostic(d,
                i,
                needle,
                needle_length,
                file_name,
                file_name_length);
#endif
    }
}


static void swimd_simd_scores(SwimdScanner *scanner) {
    for (int i = 0; i < scanner->files_vec_length; i++) {
        swimd_simd_haystack_scores(
            scanner->d_vec,
            scanner->needle_vec,
            scanner->needle_vec_length,
            scanner->files_vec[i].arr,
            scanner->files_vec[i].length,
            i,
            scanner->scores,
            scanner->gap_distr_fun,
            scanner->files,
            scanner->needle
        );
    }
}

static void swimd_scores_init(SwimdScanner *state) {
    state->scores = malloc(state->files_vec_length * LANES_COUNT_SHORT * sizeof(short));
    state->scores_length = state->files_vec_length * LANES_COUNT_SHORT;
}

static void swimd_scores_free(SwimdScanner *state) {
    free(state->scores);
}

static void swimd_scores_heap_init(SwimdScoresHeap *scores_heap, int max_size) {
    scores_heap->arr = malloc(max_size * sizeof(SwimdScoresHeapItem));
    scores_heap->size = 0;
    scores_heap->max_size = max_size;
}

static void swimd_scores_heap_free(SwimdScoresHeap *scores_heap) {
    free(scores_heap->arr);
}

static void swimd_scores_heap_cut_head(SwimdScoresHeap *scores_heap) {
    scores_heap->size--;
    scores_heap->arr[0] = scores_heap->arr[scores_heap->size];

    int ind = 0;
    while (1) {
        int left_ind = LEFT_HEAP(ind);
        int right_ind = RIGHT_HEAP(ind);
        int smallest = ind;
        if (left_ind < scores_heap->size &&
            scores_heap->arr[smallest].score > scores_heap->arr[left_ind].score) {
            smallest = left_ind;
        }

        if (right_ind < scores_heap->size &&
            scores_heap->arr[smallest].score > scores_heap->arr[right_ind].score) {
            smallest = right_ind;
        }

        if (smallest == ind)
            break;

        SWAP(scores_heap->arr[ind],
                scores_heap->arr[smallest],
                SwimdScoresHeapItem);
        ind = smallest;
    }
}

static void swimd_scores_heap_insert(SwimdScoresHeap *scores_heap, SwimdScoresHeapItem item) {
    if (scores_heap->size == 0) {
        scores_heap->size++;
        scores_heap->arr[0] = item;
        return;
    }
    if (scores_heap->size == scores_heap->max_size) {
        if (scores_heap->arr[0].score >= item.score)
        {
            return;
        }
        swimd_scores_heap_cut_head(scores_heap);
    }
    scores_heap->arr[scores_heap->size] = item;
    int ind = scores_heap->size;
    scores_heap->size++;
    while (1) {
        if (ind <= 0)
            break;
        int parent_ind = PARENT_HEAP(ind);
        if (scores_heap->arr[parent_ind].score > scores_heap-> arr[ind].score) {
            SWAP(scores_heap->arr[parent_ind],
                    scores_heap-> arr[ind],
                    SwimdScoresHeapItem);
            ind = parent_ind;
            continue;
        }
        break;
    }
}

static int swimd_compare_heap_item(const void *a, const void *b) {
    SwimdScoresHeapItem *a_item = (SwimdScoresHeapItem*)a;
    SwimdScoresHeapItem *b_item = (SwimdScoresHeapItem*)b;
    if (a_item->score == b_item->score)
        return 0;
    return a_item->score > b_item->score ? 1 : -1;
}

static void swimd_top_scores_diagnostic(
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

static int swimd_top_scores(int n, SwimdScanner *scanner) {
    int match_count = 0;
    swimd_scores_heap_init(&scanner->scores_heap, n);
    for (int i = 0; i < scanner->files->length; i++) {
        int min_score, max_score;
        SwimdFile *file = &scanner->files->arr[i];
        swimd_score_minmax(scanner->needle_length,
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

#ifdef DEBUG_PRINT
        // swimd_top_scores_diag(min_score,
        //         max_score,
        //         score,
        //         normalized_score,
        //         len_diff_error,
        //         scanner->needle,
        //         file->name);
#endif
        if (normalized_score < ERROR_THRESHOLD * 100) {
            continue;
        }

        swimd_scores_heap_insert(&scanner->scores_heap, (SwimdScoresHeapItem){
                .score = normalized_score,
                .index = i
        });
        match_count++;
    }
    qsort(scanner->scores_heap.arr,
            scanner->scores_heap.size,
            sizeof(SwimdScoresHeapItem),
            swimd_compare_heap_item);

    return match_count;
}

static void swimd_init_root_folder(SwimdFolderStruct *root) {
    root->parent = NULL;
    root->name = NULL;
    root->name_length = 0;
}

static void swimd_top_scores_free(SwimdScanner *scanner) {
    swimd_scores_heap_free(&scanner->scores_heap);
}

static void swimd_scanner_free(SwimdScanner *scanner) {
    swimd_scores_free(scanner);
    swimd_prep_files_vec_free(scanner);
    swimd_list_directories_free(scanner->files,
            scanner->folders);
    swimd_folders_free(&scanner->folders->folder_lst);
    swimd_file_list_free(scanner->files);

    free(scanner->files);
    free(scanner->folders);
    free(scanner->base_path);
}

static void swimd_scanner_init(const char *root_path, SwimdScanner *scanner) {
    swimd_log_append(SWIMD_INFO, "Scanning path started %s", root_path);

    SwimdFileList *files = malloc(sizeof(SwimdFileList));
    SwimdFolderStruct *folders = malloc(sizeof(SwimdFolderStruct));
    char *base_path = malloc(MAX_PATH_LENGTH * sizeof(char));
    swimd_init_root_folder(folders);

    swimd_file_list_init(files);
    swimd_folders_init(&folders->folder_lst);

    scanner->scanning_func(root_path, base_path, files, folders, false);

    swimd_crit_lock(&scanner->scan_state_swap);

    scanner->files = files;
    scanner->folders = folders;
    scanner->base_path = base_path;

    swimd_prep_files_vec(scanner);
    swimd_scores_init(scanner);

    swimd_crit_unlock(&scanner->scan_state_swap);

    swimd_log_append(SWIMD_INFO, "Scanning path completed");
}

static void swimd_scanner_refresh(const char *root_path, SwimdScanner *scanner) {
    swimd_log_append(SWIMD_INFO, "Refreshing path started %s", root_path);

    SwimdFileList *files = malloc(sizeof(SwimdFileList));
    SwimdFolderStruct *folders = malloc(sizeof(SwimdFolderStruct));
    char *base_path = malloc(MAX_PATH_LENGTH * sizeof(char));
    swimd_init_root_folder(folders);

    swimd_file_list_init(files);
    swimd_folders_init(&folders->folder_lst);

    scanner->scanning_func(root_path, base_path, files, folders, true);

    swimd_crit_lock(&scanner->scan_state_swap);

    swimd_scanner_free(scanner);

    scanner->files = files;
    scanner->folders = folders;
    scanner->base_path = base_path;
    scanner->scan_files_count = scanner->scan_files_refresh_count;

    swimd_prep_files_vec(scanner);
    swimd_scores_init(scanner);

    swimd_crit_unlock(&scanner->scan_state_swap);

    swimd_log_append(SWIMD_INFO, "Refreshing path completed");
}

static void swimd_scanning_loop_impl(SwimdScanner *scanner) {
    swimd_log_append(SWIMD_INFO, "Scanning loop start");
    while (1) {
        swimd_are_wait(&scanner->scan_begin);

        if (scanner->scan_terminate)
            break;

        swimd_are_set(&scanner->scan_started);

        swimd_mre_reset(&scanner->scan_finished);

        if (!scanner->scan_is_refreshing) {
            swimd_scanner_init(scanner->scan_path, scanner);
        } else {
            swimd_scanner_refresh(scanner->scan_path, scanner);
        }
        scanner->scan_in_progress = false;
        scanner->scan_is_refreshing = false;

        swimd_mre_set(&scanner->scan_finished);
    }
    swimd_log_append(SWIMD_INFO, "Scanning loop exit");
}

#ifdef _WIN32
static DWORD WINAPI swimd_scanning_loop_git(LPVOID lp_param) {
    swimd_scanning_loop_impl(&swimd_scanners[SCANNER_GIT]);
    return 0;
}

static DWORD WINAPI swimd_scanning_loop_files(LPVOID lp_param) {
    swimd_scanning_loop_impl(&swimd_scanners[SCANNER_FILES]);
    return 0;
}
#else
static void* swimd_scanning_loop_git(void *lp_param) {
    swimd_scanning_loop_impl(&swimd_scanners[SCANNER_GIT]);
    return NULL;
}

static void* swimd_scanning_loop_files(void *lp_param) {
    swimd_scanning_loop_impl(&swimd_scanners[SCANNER_FILES]);
    return NULL;
}
#endif

static void swimd_scan_thread_init(SwimdScanner *scanner) {
    swimd_are_init(&scanner->scan_begin, false);
    swimd_are_init(&scanner->scan_started, false);
    swimd_mre_init(&scanner->scan_finished, true);

    swimd_thread_create(&scanner->scan_thread, scanner->scanning_loop);

    swimd_crit_init(&scanner->scan_state_swap);
}

static void swimd_d_vec_init(SwimdScanner *scanner) {
    scanner->d_vec = malloc(MAX_PATH_LENGTH * MAX_PATH_LENGTH * LANES_COUNT_SHORT * sizeof(short));
    memset(scanner->d_vec, 0, MAX_PATH_LENGTH * MAX_PATH_LENGTH * LANES_COUNT_SHORT * sizeof(short));
    for (int i = 1; i < MAX_PATH_LENGTH; i++) {
        short value = scanner->d_vec[D_IND(i - 1, 0)];
        value += scanner->gap_distr_fun[i - 1];
        for (int j = 0; j < LANES_COUNT_SHORT; j++) {
            scanner->d_vec[D_IND(i, 0) + j] = value;
        }
    }
    for (int i = 1; i < MAX_PATH_LENGTH; i++) {
        short value = scanner->d_vec[D_IND(0, i - 1)];
        value += scanner->gap_distr_fun[i - 1];
        for (int j = 0; j < LANES_COUNT_SHORT; j++) {
            scanner->d_vec[D_IND(0, i) + j] = value;
        }
    }
}

static void swimd_d_vec_free(SwimdScanner *scanner) {
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
    swimd_d_vec_init(scanner);
    swimd_scan_thread_init(scanner);
}

static void swimd_scan_path_free(SwimdScanner *scanner) {
    free(scanner->scan_path);
}

static void swimd_scan_thread_stop(SwimdScanner *scanner) {
    scanner->scan_cancelled = true;
    swimd_mre_wait(&scanner->scan_finished);
    scanner->scan_cancelled = false;

    scanner->scan_terminate = true;
    swimd_are_set(&scanner->scan_begin);
    swimd_thread_join(&scanner->scan_thread);
    scanner->scan_terminate = false;

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

static void swimd_scan_glob_free(SwimdScanner *scanner) {
    swimd_scan_thread_stop(scanner);
    swimd_gap_distr_free(scanner);
    swimd_d_vec_free(scanner);
}

static void swimd_scan_setup_path(const char *scan_path, SwimdScanner *scanner) {
    scanner->scan_cancelled = true;
    swimd_mre_wait(&scanner->scan_finished);
    scanner->scan_cancelled = false;

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

static void swimd_scan_refresh_path(SwimdScanner *scanner) {
    if (scanner->scan_in_progress)
        return;

    scanner->scan_in_progress = true;
    scanner->scan_is_refreshing = true;
    scanner->scan_files_refresh_count = 0;
    swimd_are_set(&scanner->scan_begin);
    // same
    swimd_are_wait(&scanner->scan_started);
}

static void swimd_str_shift_right(char *buf, int buf_length, int n) {
    memmove(buf + n, buf, buf_length); 
    buf[buf_length + n] = '\0';
}

static void swimd_str_shift_left(char *buf, int buf_length, int n) {
    memmove(buf, buf + n, buf_length - n); 
    buf[buf_length - n] = '\0';
}

static void swimd_str_reverse(char *buf, int buf_length) {
    for (int i = 0; i < buf_length / 2; i++) {
        SWAP(buf[i], buf[buf_length - i - 1], char);
    }
}

static void swimd_print_relative(char *file_path, char *scan_path, char *base_path) {
    int base_path_len = strlen(base_path);
    int scan_path_len = strlen(scan_path);

    if (base_path_len == scan_path_len)
        return;
    int file_path_len = strlen(file_path);

    int scan_path_depth = swimd_path_depth(scan_path, PATH_SLASH_CHAR);
    int absolute_match_count = swimd_path_match_depth(base_path, scan_path, PATH_SLASH_CHAR);

    int up_count = scan_path_depth - absolute_match_count;

    int relative_match_depth = swimd_path_match_depth(file_path, scan_path + base_path_len + 1, PATH_SLASH_CHAR);
    up_count -= relative_match_depth;

    int relative_math_len = swimd_path_length(file_path, relative_match_depth, PATH_SLASH_CHAR);
    if (file_path[relative_math_len] == PATH_SLASH_CHAR)
        relative_math_len++;

    swimd_str_shift_left(file_path, file_path_len, relative_math_len);

    int pref_len = up_count * 3;
    swimd_str_shift_right(file_path, file_path_len, pref_len);

    for (int i = 0; i < up_count; i++) {
        file_path[3*i + 0] = '.';
        file_path[3*i + 1] = '.';
        file_path[3*i + 2] = PATH_SLASH_CHAR;
    }
}

static void swimd_print_path(char *buf, SwimdFile *file) {
    int buf_length = 0;
    for (int i = 0; i < file->name_length; i++) {
        buf[buf_length++] = file->name[file->name_length - i - 1];
    }
    buf[buf_length++] = PATH_SLASH_CHAR;
    SwimdFolderStruct *cur_folder = file->folder;
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
    swimd_str_reverse(buf, buf_length);
    buf[buf_length++] = '\0';
}

static void swimd_process_input(const char *needle,
        int max_size,
        SwimdProcessInputResult *result,
        SwimdScanner *scanner) {
    swimd_setup_needle(needle, scanner);

    swimd_simd_scores(scanner);
    swimd_top_scores(max_size, scanner);

    result->items = malloc(max_size * sizeof(SwimdProcessInputResultItem));

    for (int i = 0; i < scanner->scores_heap.size; i++) {
        SwimdScoresHeapItem heap_item = scanner->scores_heap.arr[i];
        SwimdFile file = scanner->files->arr[heap_item.index];

        char path[MAX_PATH_LENGTH];
        swimd_print_path(path, &file);
        swimd_print_relative(path, scanner->scan_path, scanner->base_path);
        int path_length = strlen(path);

        SwimdProcessInputResultItem item = {0};
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
                SwimdProcessInputResultItem);
    }

    swimd_top_scores_free(scanner);
    swimd_setup_needle_free(scanner);
}

static void swimd_scan_process_input(const char *input,
        int max_size,
        SwimdProcessInputResult *result,
        SwimdScanner *scanner) {

    swimd_crit_lock(&scanner->scan_state_swap);

    result->scanned_items_count = scanner->scan_files_count;

    if (scanner->scan_in_progress && !scanner->scan_is_refreshing) {
        result->scan_in_progress = true;
    } else {
        result->scan_in_progress = false;
        swimd_process_input(input, max_size, result, scanner);
    }

    swimd_crit_unlock(&scanner->scan_state_swap);
}

static void swimd_scan_process_input_free(SwimdProcessInputResult *result) {
    if (result->items == NULL)
        return;

    for (int i = 0; i < result->items_length; i++) {
        SwimdProcessInputResultItem item = result->items[i];
        free(item.path);
        free(item.name);
    }
    free(result->items);
}

static int swimd_lua_init(lua_State *L) {
    if (swimd_initialized) {
        swimd_log_append(SWIMD_INFO, "Already initialized");
        return 0;
    }
    swimd_initialized = true;

    const char *log_path = lua_gettop(L) == 0 || lua_isnil(L, 1)
        ? NULL : luaL_checkstring(L, 1);

    swimd_global_init(log_path);

    swimd_log_append(SWIMD_INFO, "Initializing");

    for (int i = 0; i < SCANNER_COUNT; i++) {
        swimd_scan_glob_init(&swimd_scanners[i]);
    }

    swimd_log_append(SWIMD_INFO, "Initializing completed");
    return 0;
}

static int swimd_lua_shutdown(lua_State *L) {
    if (!swimd_initialized) {
        return 0;
    }
    swimd_log_append(SWIMD_INFO, "Shutting down");

    for (int i = 0; i < SCANNER_COUNT; i++) {
        swimd_scan_glob_free(&swimd_scanners[i]);
    }

    swimd_log_append(SWIMD_INFO, "Shutting down completed");

    swimd_global_free();

    swimd_initialized = false;
    return 0;
}

static int swimd_lua_setup_workspace(lua_State *L) {
    const char *workspace = luaL_checkstring(L, 1);

    swimd_log_append(SWIMD_INFO, "Setting up workspace path %s", workspace);

    for (int i = 0; i < SCANNER_COUNT; i++) {
        swimd_scan_setup_path(workspace, &swimd_scanners[i]);
    }

    swimd_log_append(SWIMD_INFO, "Workspace path setup");
    return 0;
}

static int swimd_lua_refresh_workspace(lua_State *L) {
    swimd_log_append(SWIMD_INFO, "Refreshing workspace");

    for (int i = 0; i < SCANNER_COUNT; i++) {
        swimd_scan_refresh_path(&swimd_scanners[i]);
    }

    swimd_log_append(SWIMD_INFO, "Refresh requested");
    return 0;
}

static int swimd_lua_process_input(lua_State *L) {
    const char *input = luaL_checkstring(L, 1);
    int max_size = luaL_checknumber(L, 2);
    int scanner_index = luaL_checknumber(L, 3);

    SwimdProcessInputResult result = {0};
    SwimdScanner *scanner = &swimd_scanners[scanner_index];

    swimd_scan_process_input(input, max_size, &result, scanner);
    lua_newtable(L);
    lua_pushstring(L, "scan_in_progress");
    lua_pushboolean(L, result.scan_in_progress);
    lua_settable(L, -3);

    lua_pushstring(L, "scanned_items_count");
    lua_pushinteger(L, result.scanned_items_count);
    lua_settable(L, -3);

    lua_pushstring(L, "items");
    lua_newtable(L);
    if (!result.scan_in_progress) {
        for (int i = 0; i < result.items_length; i++) {
            SwimdProcessInputResultItem item = result.items[i];

            lua_pushnumber(L, i + 1);

            lua_newtable(L);
            lua_pushstring(L, "name");
            lua_pushstring(L, item.name);
            lua_settable(L, -3);

            lua_pushstring(L, "score");
            lua_pushinteger(L, item.score);
            lua_settable(L, -3);

            lua_pushstring(L, "path");
            lua_pushstring(L, item.path);
            lua_settable(L, -3);

            lua_settable(L, -3);
        }
    }
    lua_settable(L, -3);

    swimd_scan_process_input_free(&result);
    return 1;
}

static int swimd_lua_sayhello(lua_State *L) {
    const char *str = luaL_checkstring(L, 1);
    char greeting[100] = "Hello, ";
    strcat(greeting, str);
    strcat(greeting, "!");
    lua_pushstring(L, greeting);
    return 1;
}

static int swimd_lua_log(lua_State *L) {
    const char *str = luaL_checkstring(L, 1);
    swimd_log_append(SWIMD_INFO, "[swimd-lua]: %s", str);
    return 1;
}

EXPORT
int luaopen_swimd(lua_State *L) {
    static const luaL_Reg funcs[] = {
        {"init", swimd_lua_init},
        {"setup_workspace", swimd_lua_setup_workspace},
        {"refresh_workspace", swimd_lua_refresh_workspace},
        {"process_input", swimd_lua_process_input},
        {"shutdown", swimd_lua_shutdown},
        {"say_hello", swimd_lua_sayhello},
        {"log", swimd_lua_log},

        {NULL, NULL}
    };
    luaL_register(L, "swimd", funcs);

    lua_pushinteger(L, SCANNER_FILES);
    lua_setfield(L, -2, "SCANNER_FILES");

    lua_pushinteger(L, SCANNER_GIT);
    lua_setfield(L, -2, "SCANNER_GIT");

    return 1;
}

static void swimd_scenario_setup_path(void) {
    swimd_initialized = true;
    swimd_global_init("swimd.log");
    SwimdScanner *scanner = &swimd_scanners[SCANNER_FILES];
    swimd_scan_glob_init(scanner);

    for (;;) {
        swimd_scan_setup_path("c:\\projects\\tmp_swimd", scanner);

        SwimdProcessInputResult result = {0};
        swimd_scan_process_input("swimd", 10, &result, scanner);

        if (result.scan_in_progress) {
            printf("Scanning %d\n", result.scanned_items_count);
        } else {
            for (int i = 0; i < result.items_length; i++) {
                SwimdProcessInputResultItem item = result.items[i];

                printf("name = %s, score = %d, path = %s\n",
                        item.name,
                        item.score,
                        item.path);
            }
        }

        swimd_scan_process_input_free(&result);
        getchar();
    }
    swimd_scan_glob_free(scanner);
    swimd_global_free();

    printf("Over\n");
}

static void swimd_scenario_scanning(void) {
    swimd_initialized = true;
    swimd_global_init("swimd.log");

    SwimdScanner *scanner = &swimd_scanners[SCANNER_FILES];
    swimd_scan_glob_init(scanner);

    for (int i = 0; i < 10; i++) {
#ifdef _WIN32
        swimd_scan_setup_path("c:\\projects\\tmp_swimd", scanner);
#else
        swimd_scan_setup_path("/home/ivan/Projects/tmp_swimd", scanner);
#endif
        while(1) {
            SwimdProcessInputResult result = {0};
            swimd_scan_process_input("fil", 10, &result, scanner);

            if (result.scan_in_progress) {
                printf("Scanning %d\n", result.scanned_items_count);
            } else {
                for (int i = 0; i < result.items_length; i++) {
                    SwimdProcessInputResultItem item = result.items[i];

                    printf("name = %s, score = %d, path = %s\n",
                            item.name,
                            item.score,
                            item.path);
                }
            }

            swimd_scan_process_input_free(&result);
            getchar();
        }
    }
    swimd_scan_glob_free(scanner);
    swimd_global_free();

    printf("Over\n");
}

int main() {
    swimd_scenario_scanning();
    // swimd_scenario_setup_path();

    return 0;
}
