#include "immintrin.h"
#include "stdio.h"
#include "windows.h"
#include "limits.h"
#include "stdlib.h"
#include "lua.h"
#include "lauxlib.h"
#include "time.h"

#define MAX_PATH_LENGTH 300
#define LANES_COUNT_SHORT 16
#define GAP_PENALTY 3
#define SUB_PENALTY 2
#define Vector __m256i

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

typedef struct  {
    char* path;
    char* name;
    int score;
} SwimdProcessInputResultItem;

typedef struct {
    BOOL scan_in_progress;
    int scanned_items_count;
    SwimdProcessInputResultItem* items;
    int items_length;
} SwimdProcessInputResult;

typedef struct SwimdFolderStruct SwimdFolderStruct;

typedef struct {
    SwimdFolderStruct** arr;
    int length;
    int capacity;
} SwimdFolderStructList;

typedef struct SwimdFolderStruct
{
    char* name;
    int name_length;
    SwimdFolderStructList folder_lst;
    struct SwimdFolderStruct* parent;
} SwimdFolderStruct;

typedef struct {
    char* name;
    int name_length;
    SwimdFolderStruct* folder;
} SwimdFile;

typedef struct {
    SwimdFile* arr;
    int length;
    int capacity;
} SwimdFileList;

typedef short SwimdAlgoMatrix[MAX_PATH_LENGTH * MAX_PATH_LENGTH * LANES_COUNT_SHORT];

typedef struct {
    short* arr;
    int length;
} SwimdFileVec;

typedef struct {
    int score;
    int index;
} SwimdScoresHeapItem;

typedef struct  {
    SwimdScoresHeapItem* arr;
    int size;
    int max_size;
} SwimdScoresHeap;

typedef struct {
    BOOL initialized;

    char* needle;
    int needle_length;
    short* needle_vec;
    int needle_vec_length;

    SwimdFileList files;
    SwimdFileVec* files_vec;
    int files_vec_length;

    SwimdFolderStruct folders;
    short* scores;
    int scores_length;
    SwimdScoresHeap scores_heap;

    SwimdAlgoMatrix d_vec;

    HANDLE scan_thread;
    HANDLE scan_begin;
    HANDLE scan_started;
    HANDLE scan_finished;
    volatile BOOL scan_terminate;
    volatile BOOL scan_cancelled;
    volatile BOOL scan_in_progress;
    char* scan_path;
} SwimdAlgoState;

static SwimdAlgoState swimd_state = {0};
static FILE* swimd_log = {0};

void swimd_log_init(void) {
    swimd_log = fopen("swimd.log", "a");
    if (swimd_log == NULL) {
        fprintf(stderr, "Unable to init log file");
        exit(1);
    }
}

void swimd_log_free(void) {
    fclose(swimd_log);
}

void swimd_log_append(const char* msg, ...) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    time_t seconds = ts.tv_sec;

    struct tm *t = localtime(&seconds);

#ifdef DEBUG_PRINT
    printf("%04d-%02d-%02dT%02d:%02d:%02d.%09d ",
        t->tm_year+1900, 
        t->tm_mon+1, 
        t->tm_mday,
        t->tm_hour, 
        t->tm_min, 
        t->tm_sec,
        ts.tv_nsec
    );
#endif

    fprintf(swimd_log, "%04d-%02d-%02dT%02d:%02d:%02d.%09d ",
        t->tm_year+1900, 
        t->tm_mon+1, 
        t->tm_mday,
        t->tm_hour, 
        t->tm_min, 
        t->tm_sec,
        ts.tv_nsec
    );
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

void swimd_folders_init(SwimdFolderStructList* lst) {
    int default_size = 4;
    lst->arr = malloc(default_size * sizeof(SwimdFolderStruct*));
    lst->length = 0;
    lst->capacity = default_size;
}

void swimd_folders_append(SwimdFolderStructList* lst, SwimdFolderStruct* folder) {
    if (lst->length == lst->capacity) {
        lst->capacity = lst->capacity * 2;
        lst->arr = realloc(lst->arr, lst->capacity * sizeof(SwimdFolderStruct*));
    }
    lst->arr[lst->length] = folder;
    lst->length++;
}

void swimd_folders_free(SwimdFolderStructList* lst) {
    free(lst->arr);
}

void swimd_file_list_init(SwimdFileList* lst) {
    int default_size = 4;
    lst->arr = malloc(default_size * sizeof(SwimdFile));
    lst->length = 0;
    lst->capacity = default_size;
}

void swimd_file_list_append(SwimdFileList* lst, SwimdFile file) {
    if (lst->length == lst->capacity) {
        lst->capacity = lst->capacity * 2;
        lst->arr = realloc(lst->arr, lst->capacity * sizeof(SwimdFile));
    }
    lst->arr[lst->length] = file;
    lst->length++;
    if (lst->length % 10000 == 0)
        swimd_log_append("Scanned file count %d", lst->length);
}

void SwimdFilelist_free(SwimdFileList* lst) {
    lst->length = 0;
    free(lst->arr);
}

void swimd_list_directories(const char* root_dir,
        SwimdFileList* file_list,
        SwimdFolderStruct* root_folder) {

    char root_mask[MAX_PATH_LENGTH];
    char inner_folder[MAX_PATH_LENGTH];

    strcpy(root_mask, root_dir);
    strcat(root_mask, "\\*");

    WIN32_FIND_DATA find_file_data;
    HANDLE h_find;

    h_find = FindFirstFile(root_mask, &find_file_data);

    if (h_find == INVALID_HANDLE_VALUE) {
        swimd_log_append("FindFirstFile failed (%lu)\n", GetLastError());
        return;
    }

    while (1) {
        const char* current_file = find_file_data.cFileName;
        int current_file_len = strlen(current_file);

        if (find_file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (strcmp(current_file, ".") != 0 && strcmp(current_file, "..") != 0) {
                char* folder_name = malloc((current_file_len + 1) * sizeof(char));
                strcpy(folder_name, current_file);

                SwimdFolderStruct* folder_node = malloc(sizeof(SwimdFolderStruct));

                folder_node->name = folder_name;
                folder_node->name_length = current_file_len;
                folder_node->parent = root_folder;

                swimd_folders_init(&folder_node->folder_lst);
                swimd_folders_append(&root_folder->folder_lst, folder_node);

                strcpy(inner_folder, root_dir);
                strcat(inner_folder, "\\");
                strcat(inner_folder, current_file);

                swimd_list_directories(inner_folder, file_list, folder_node);
            }
        } else {
            char* file_name = malloc((current_file_len + 1) * sizeof(char));
            strcpy(file_name, current_file);

            SwimdFile file_node = {
                .name = file_name,
                .name_length = current_file_len,
                .folder = root_folder
            };

            swimd_file_list_append(file_list, file_node);
        }

        if (FindNextFile(h_find, &find_file_data) == 0)
            break;
        if (swimd_state.scan_cancelled)
            break;
    }

    if (!swimd_state.scan_cancelled)
    {
        DWORD dw_error = GetLastError();
        if (dw_error != ERROR_NO_MORE_FILES) {
            swimd_log_append("FindNextFile error (%lu)", dw_error);
        }
    }

    FindClose(h_find);
}

void swimd_list_directories_folders_free(SwimdFolderStruct* root_folder) {
    for (int i = 0; i < root_folder->folder_lst.length; i++) {
        SwimdFolderStruct* folder = root_folder->folder_lst.arr[i];
        swimd_list_directories_folders_free(folder);
        swimd_folders_free(&folder->folder_lst);
        free(folder->name);
        free(folder);
    }
}

void swimd_list_directories_free(const SwimdFileList* lst,
        SwimdFolderStruct* root_folder) {
    swimd_list_directories_folders_free(root_folder);
    for (int i = 0; i < lst->length; i++) {
        free(lst->arr[i].name);
    }
}

void swimd_prep_files_vec(SwimdAlgoState* state) {
    SwimdFileList files = state->files;
    int files_length = files.length;
    int files_vec_length = CEIL_DIV(files_length, LANES_COUNT_SHORT);
    SwimdFileVec* files_vec = malloc(files_vec_length * sizeof(SwimdFileVec));

    for (int i = 0; i < files_vec_length; i++) {
        int max_length = 0;
        for (int j = 0; j < LANES_COUNT_SHORT; j++) {
            if (i * LANES_COUNT_SHORT + j >= files_length)
                break;
            max_length = MAX(max_length, files.arr[i * LANES_COUNT_SHORT + j].name_length);
        }

        int file_vec_length = max_length * LANES_COUNT_SHORT;
        short* file_vec_arr = malloc(file_vec_length * sizeof(short));
        memset(file_vec_arr, 0, file_vec_length * sizeof(short));

        for (int k = 0; k < max_length; k++) {
            for (int j = 0; j < LANES_COUNT_SHORT; j++) {
                if (i * LANES_COUNT_SHORT + j >= files_length)
                    break;
                SwimdFile file = files.arr[i * LANES_COUNT_SHORT + j];
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
    state->files_vec = files_vec;
    state->files_vec_length = files_vec_length;
}

void swimd_prep_files_vec_free(SwimdAlgoState* state) {
    for (int i = 0; i < state->files_vec_length; i++) {
        SwimdFileVec file_vec = state->files_vec[i];
        free(file_vec.arr);
    }
    free(state->files_vec);
}

void swimd_prep_needle_vec(SwimdAlgoState* state) {
    int needle_vec_length = state->needle_length * LANES_COUNT_SHORT;
    short* needle_vec = malloc(needle_vec_length * sizeof(short));
    for (int i = 0; i < state->needle_length; i++) {
        for (int j = 0; j < LANES_COUNT_SHORT; j++) {
            needle_vec[i * LANES_COUNT_SHORT + j] = (short)state->needle[i];
        }
    }
    state->needle_vec = needle_vec;
    state->needle_vec_length = needle_vec_length;
}

void swimd_prep_needle_vec_free(SwimdAlgoState* state) {
    free(state->needle_vec);
}

void swimd_setup_needle(const char* needle) {
    int needle_length = strlen(needle);
    swimd_state.needle = malloc((needle_length + 1) * sizeof(char));
    strcpy(swimd_state.needle, needle);
    swimd_state.needle_length = needle_length;

    swimd_prep_needle_vec(&swimd_state);
}

void swimd_setup_needle_free(void) {
    free(swimd_state.needle);

    swimd_prep_needle_vec_free(&swimd_state);
}

void swimd_prep_d_vec(SwimdAlgoState* state) {
    memset(state->d_vec, 0, MAX_PATH_LENGTH * MAX_PATH_LENGTH * LANES_COUNT_SHORT * sizeof(short));
    for (int i = 1; i < MAX_PATH_LENGTH; i++) {
        short value = state->d_vec[(i - 1) * MAX_PATH_LENGTH * LANES_COUNT_SHORT];
        value -= GAP_PENALTY;
        for (int j = 0; j < LANES_COUNT_SHORT; j++) {
            state->d_vec[i * MAX_PATH_LENGTH * LANES_COUNT_SHORT + j] = value;
        }
    }
    for (int i = 1; i < MAX_PATH_LENGTH; i++) {
        short value = state->d_vec[(i - 1) * LANES_COUNT_SHORT];
        value -= GAP_PENALTY;
        for (int j = 0; j < LANES_COUNT_SHORT; j++) {
            state->d_vec[i * LANES_COUNT_SHORT + j] = value;
        }
    }
}

void swimd_vec_estimate(SwimdAlgoMatrix* d,
    short* a,
    int a_len,
    short* b,
    int b_len,
    int pos,
    short* scores
) {
    int n = a_len / LANES_COUNT_SHORT + 1;
    int m = b_len / LANES_COUNT_SHORT + 1;

    Vector sub_pen = _mm256_set1_epi16(-SUB_PENALTY);
    Vector eq_reward = _mm256_set1_epi16(SUB_PENALTY);
    Vector gap_pen = _mm256_set1_epi16(GAP_PENALTY);

    Vector res = _mm256_set1_epi16(SHRT_MIN);
    for (int i = 1; i < n; i++) {
        Vector va = _mm256_loadu_si256((Vector const*)&a[LANES_COUNT_SHORT * (i - 1)]);
        for (int j = 1; j < m; j++) {
            Vector vb = _mm256_loadu_si256((Vector const*)&b[LANES_COUNT_SHORT * (j - 1)]);
            Vector vdiag = _mm256_loadu_si256((Vector const*)&(*d)[
                    MAX_PATH_LENGTH * LANES_COUNT_SHORT * (i - 1) +
                    LANES_COUNT_SHORT * (j - 1)
            ]);
            Vector vup = _mm256_loadu_si256((Vector const*)&(*d)[
                    MAX_PATH_LENGTH * LANES_COUNT_SHORT * i +
                    LANES_COUNT_SHORT * (j - 1)
            ]);
            Vector vleft = _mm256_loadu_si256((Vector const*)&(*d)[
                    MAX_PATH_LENGTH * LANES_COUNT_SHORT * (i - 1) +
                    LANES_COUNT_SHORT * j
            ]);

            Vector veq = _mm256_cmpeq_epi16(va, vb);
            Vector c1 = _mm256_and_si256(eq_reward, veq);
            Vector c2 = _mm256_andnot_si256(veq, sub_pen); // flipped
            Vector o1 = _mm256_add_epi16(c1, c2);
            o1 = _mm256_add_epi16(o1, vdiag);

            Vector o2 = _mm256_sub_epi16(vup, gap_pen);
            Vector o3 = _mm256_sub_epi16(vleft, gap_pen);

            Vector o = _mm256_max_epi16(o1, o2);
            o = _mm256_max_epi16(o, o3);
            _mm256_storeu_si256((Vector*)&(*d)[
                    MAX_PATH_LENGTH * LANES_COUNT_SHORT * i +
                    LANES_COUNT_SHORT * j
            ], o);
            res = _mm256_max_epi16(res, o);
        }
    }
    _mm256_storeu_si256((Vector*)&scores[
            pos * LANES_COUNT_SHORT
    ], res);
}

void swimd_simd_scores(void) {
    for (int i = 0; i < swimd_state.files_vec_length; i++) {
        swimd_vec_estimate(
            &swimd_state.d_vec,
            swimd_state.needle_vec,
            swimd_state.needle_vec_length,
            swimd_state.files_vec[i].arr,
            swimd_state.files_vec[i].length,
            i,
            swimd_state.scores
        );
    }
}

void swimd_scores_init(SwimdAlgoState* state) {
    state->scores = malloc(state->files_vec_length * LANES_COUNT_SHORT * sizeof(short));
    state->scores_length = state->files_vec_length * LANES_COUNT_SHORT;
}

void swimd_scores_free(SwimdAlgoState* state) {
    free(state->scores);
}

void SwimdScoresHeap_init(SwimdScoresHeap* scores_heap, int max_size) {
    scores_heap->arr = malloc(max_size * sizeof(SwimdScoresHeapItem));
    scores_heap->size = 0;
    scores_heap->max_size = max_size;
}

void SwimdScoresHeap_free(SwimdScoresHeap* scores_heap) {
    free(scores_heap->arr);
}

void SwimdScoresHeap_cut_head(SwimdScoresHeap* scores_heap) {
    scores_heap->size--;
    scores_heap->arr[0] = scores_heap->arr[scores_heap->size];

    int ind = 0;
    while (1) {
        int left_ind = LEFT_HEAP(ind);
        if (left_ind < scores_heap->size &&
            scores_heap->arr[ind].score > scores_heap->arr[left_ind].score) {
            SWAP(scores_heap->arr[ind],
                    scores_heap->arr[left_ind],
                    SwimdScoresHeapItem);
            ind = left_ind;
            continue;
        }

        int right_ind = RIGHT_HEAP(ind);
        if (right_ind < scores_heap->size &&
            scores_heap->arr[ind].score > scores_heap->arr[right_ind].score) {
            SWAP(scores_heap->arr[ind],
                    scores_heap->arr[right_ind],
                    SwimdScoresHeapItem);
            ind = right_ind;
            continue;
        }
        break;
    }
}

void SwimdScoresHeap_insert(SwimdScoresHeap* scores_heap, SwimdScoresHeapItem item) {
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
        SwimdScoresHeap_cut_head(scores_heap);
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

int swimd_compare_heap_item(const void* a, const void* b) {
    SwimdScoresHeapItem* a_item = (SwimdScoresHeapItem*)a;
    SwimdScoresHeapItem* b_item = (SwimdScoresHeapItem*)b;
    if (a_item->score == b_item->score)
        return 0;
    return a_item->score > b_item->score ? 1 : -1;
}

void swimd_top_scores(int n) {
    SwimdScoresHeap_init(&swimd_state.scores_heap, n);
    for (int i = 0; i < swimd_state.files.length; i++) {
        SwimdScoresHeap_insert(&swimd_state.scores_heap, (SwimdScoresHeapItem){
                .score = swimd_state.scores[i],
                .index = i
        });
    }
    qsort(swimd_state.scores_heap.arr,
            swimd_state.scores_heap.size,
            sizeof(SwimdScoresHeapItem),
            swimd_compare_heap_item);
}

void swimd_top_scores_free(void) {
    SwimdScoresHeap_free(&swimd_state.scores_heap);
}

void swimd_state_init(const char* root_path) {
    swimd_log_append("Scanning path started %s", root_path);

    swimd_file_list_init(&swimd_state.files);
    swimd_folders_init(&swimd_state.folders.folder_lst);
    swimd_list_directories(root_path,
        &swimd_state.files,
        &swimd_state.folders);

    swimd_prep_files_vec(&swimd_state);
    swimd_prep_d_vec(&swimd_state);
    swimd_scores_init(&swimd_state);

    swimd_log_append("Scanning path completed");
}

DWORD WINAPI swimd_scan_loop(LPVOID lp_param) {
    swimd_log_append("Scanning loop start");
    while (1) {
        WaitForSingleObject(swimd_state.scan_begin, INFINITE);

        if (swimd_state.scan_terminate)
            break;

        SetEvent(swimd_state.scan_started);

        ResetEvent(swimd_state.scan_finished);

        swimd_state_init(swimd_state.scan_path);

        swimd_state.scan_in_progress = FALSE;
        SetEvent(swimd_state.scan_finished);
    }
    swimd_log_append("Scanning loop exit");
    return 0;
}

void swimd_scan_thread_init(void) {
    swimd_state.scan_thread = CreateThread(NULL, 0, swimd_scan_loop, NULL, 0, NULL);
    swimd_state.scan_begin = CreateEvent(NULL, FALSE, FALSE, NULL);
    swimd_state.scan_started = CreateEvent(NULL, FALSE, FALSE, NULL);
    swimd_state.scan_finished = CreateEvent(NULL, TRUE, TRUE, NULL);
}

void swimd_scan_path_free(void) {
    free(swimd_state.scan_path);
}

void swimd_state_free(void) {
    swimd_scores_free(&swimd_state);
    swimd_prep_files_vec_free(&swimd_state);
    swimd_list_directories_free(&swimd_state.files,
            &swimd_state.folders);
    swimd_folders_free(&swimd_state.folders.folder_lst);
    SwimdFilelist_free(&swimd_state.files);
}

void swimd_scan_thread_stop(void) {
    swimd_state.scan_cancelled = TRUE;
    WaitForSingleObject(swimd_state.scan_finished, INFINITE);
    swimd_state.scan_cancelled = FALSE;

    swimd_state.scan_terminate = TRUE;
    SetEvent(swimd_state.scan_begin);
    WaitForSingleObject(swimd_state.scan_thread, INFINITE);
    swimd_state.scan_terminate = FALSE;

    if (swimd_state.scan_path != NULL) {
        swimd_scan_path_free();
        swimd_state_free();
        swimd_state.scan_path = NULL;
    }

    CloseHandle(swimd_state.scan_begin);
    CloseHandle(swimd_state.scan_started);
    CloseHandle(swimd_state.scan_finished);
    CloseHandle(swimd_state.scan_thread);
}

void swimd_scan_setup_path(const char* scan_path) {
    swimd_state.scan_cancelled = TRUE;
    WaitForSingleObject(swimd_state.scan_finished, INFINITE);
    swimd_state.scan_cancelled = FALSE;

    if (swimd_state.scan_path != NULL) {
        swimd_scan_path_free();
        swimd_state_free();
        swimd_state.scan_path = NULL;
    }

    int scan_path_len = strlen(scan_path);
    swimd_state.scan_path = malloc((scan_path_len + 1) * sizeof(char));
    strcpy(swimd_state.scan_path, scan_path);

    SetEvent(swimd_state.scan_begin);
    swimd_state.scan_in_progress = TRUE;
    // need to wait until we start scanning, in order not to messup in cleanup when state has been not initialized
    WaitForSingleObject(swimd_state.scan_started, INFINITE); 
}

void swimd_str_reverse(char* buf, int buf_length) {
    for (int i = 0; i < buf_length / 2; i++) {
        SWAP(buf[i], buf[buf_length - i - 1], char);
    }
}

void swimd_print_path(char* buf, SwimdFile* file) {
    int buf_length = 0;
    for (int i = 0; i < file->name_length; i++) {
        buf[buf_length++] = file->name[file->name_length - i - 1];
    }
    buf[buf_length++] = '\\';
    SwimdFolderStruct* cur_folder = file->folder;
    while (1) {
        for (int i = 0; i < cur_folder->name_length; i++) {
            buf[buf_length++] = cur_folder->name[cur_folder->name_length - i - 1];
        }
        if (IS_ROOT_FOLDER(cur_folder))
            break;
        buf[buf_length++] = '\\';
        cur_folder = cur_folder->parent;
    }
    swimd_str_reverse(buf, buf_length);
    buf[buf_length++] = '\0';
}

void swimd_process_input(const char* needle, 
        int max_size, 
        SwimdProcessInputResult* result) {
    swimd_setup_needle(needle);

    swimd_simd_scores();
    swimd_top_scores(max_size);

    result->items = malloc(max_size * sizeof(SwimdProcessInputResultItem));

    for (int i = 0; i < swimd_state.scores_heap.size; i++) {
        SwimdScoresHeapItem heap_item = swimd_state.scores_heap.arr[i];
        SwimdFile file = swimd_state.files.arr[heap_item.index];

        char path[MAX_PATH_LENGTH];
        swimd_print_path(path, &file);
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

    swimd_top_scores_free();
    swimd_setup_needle_free();
}

void swimd_scan_process_input(const char* input, 
        int max_size, 
        SwimdProcessInputResult* result) {
    result->scanned_items_count = swimd_state.files.length;
    if (swimd_state.scan_in_progress) {
        result->scan_in_progress = TRUE;
        return;
    }
    result->scan_in_progress = FALSE;
    swimd_process_input(input, max_size, result);
}

void swimd_scan_process_input_free(SwimdProcessInputResult* result) {
    if (result->items == NULL)
        return;

    for (int i = 0; i < result->items_length; i++) {
        SwimdProcessInputResultItem item = result->items[i];
        free(item.path);
        free(item.name);
    }
    free(result->items);
}

int swimd_lua_init(lua_State *L) {
    if (swimd_state.initialized) {
        swimd_log_append("Already initialized");
        return 0;
    }
    swimd_state.initialized = TRUE;

    swimd_log_init();

    swimd_log_append("Initializing");

    swimd_scan_thread_init();

    swimd_log_append("Initializing completed");
    return 0;
}

int swimd_lua_shutdown(lua_State *L) {
    if (!swimd_state.initialized) {
        return 0;
    }
    swimd_log_append("Shutting down");

    swimd_scan_thread_stop();

    swimd_log_append("Shutting down completed");

    swimd_log_free();

    swimd_state.initialized = FALSE;
    return 0;
}

int swimd_lua_setup_workspace(lua_State *L) {
    const char* workspace = luaL_checkstring(L, 1);

    swimd_log_append("Setting up workspace path %s", workspace);

    swimd_scan_setup_path(workspace);

    swimd_log_append("Workspace path setup");
    return 0;
}

int swimd_lua_process_input(lua_State *L) {
    const char* input = luaL_checkstring(L, 1);
    int max_size = luaL_checknumber(L, 2);

    SwimdProcessInputResult result = {0};

    swimd_scan_process_input(input, max_size, &result);
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

int swimd_lua_sayhello(lua_State *L) {
    const char* str = luaL_checkstring(L, 1);
    char greeting[100] = "Hello, ";
    strcat(greeting, str);
    strcat(greeting, "!");
    lua_pushstring(L, greeting);
    return 1;
}

__declspec(dllexport)
int luaopen_swimd(lua_State *L) {
    static const luaL_Reg funcs[] = {
        {"init", swimd_lua_init},
        {"setup_workspace", swimd_lua_setup_workspace},
        {"process_input", swimd_lua_process_input},
        {"shutdown", swimd_lua_shutdown},
        {"say_hello", swimd_lua_sayhello},

        {NULL, NULL}
    };
    luaL_register(L, "swimd", funcs);
    return 1;
}

int main() {
    getchar();

    swimd_state.initialized = TRUE;
    swimd_log_init();
    swimd_scan_thread_init();

    // swimd_scan_setup_path("c:\\projects");
    // SwimdProcessInputResult result = {0};
    // swimd_scan_process_input("hello", 10, &result);
    // swimd_scan_process_input_free(&result);
    for (int i = 0; i < 10; i++) {
        swimd_scan_setup_path("c:\\projects");
        getchar();
        while(1) {
            SwimdProcessInputResult result = {0};
            swimd_scan_process_input("hello", 10, &result);

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
    swimd_scan_thread_stop();
    swimd_log_free();

    printf("Over\n");
    return 0;
}
