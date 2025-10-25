#include "immintrin.h"
#include "stdio.h"
#include "windows.h"

#define MAX_PATH_LENGTH 300
#define LANES_COUNT_SHORT 16
#define GAP_PENALTY 3
#define SUB_PENALTY 2
#define Vector __m256i

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define CEIL_DIV(a,b) ((a) % (b) == 0 ? ((a)/(b)) : ((a)/(b)) + 1)

typedef struct Swimd_Folder_Struct Swimd_Folder_Struct;

typedef struct {
    Swimd_Folder_Struct** arr;
    int length;
    int capacity;
} Swimd_Folder_Struct_List;

typedef struct Swimd_Folder_Struct
{
    char* name;
    int name_len;
    Swimd_Folder_Struct_List folder_lst;
    struct Swimd_Folder_Struct* parent;
} Swimd_Folder_Struct;

typedef struct {
    char* name;
    int name_len;
    Swimd_Folder_Struct* folder;
} Swimd_File;

typedef struct {
    Swimd_File* arr;
    int length;
    int capacity;
} Swimd_File_List;

typedef short Swimd_Algo_Matrix[MAX_PATH_LENGTH * MAX_PATH_LENGTH * LANES_COUNT_SHORT];

typedef struct {
    short* arr;
    int length;
} Swimd_File_Vec;

typedef struct {
    char* needle;
    int needle_length;
    short* needle_vec;
    int needle_vec_length;

    Swimd_File_List files;
    Swimd_File_Vec* files_vec;
    int files_vec_length;

    Swimd_Folder_Struct folders;
    short* scores;
    Swimd_Algo_Matrix d_vec;
} Swimd_Algo_State;

static Swimd_Algo_State swimd_state = {0};

void swimd_folders_init(Swimd_Folder_Struct_List* lst) {
    int default_size = 4;
    lst->arr = malloc(default_size * sizeof(Swimd_Folder_Struct*));
    lst->length = 0;
    lst->capacity = default_size;
}

void swimd_folders_append(Swimd_Folder_Struct_List* lst, Swimd_Folder_Struct* folder) {
    if (lst->length == lst->capacity) {
        lst->capacity = lst->capacity * 2;
        lst->arr = realloc(lst->arr, lst->capacity * sizeof(Swimd_Folder_Struct*));
    }
    lst->arr[lst->length] = folder;
    lst->length++;
}

void swimd_folders_free(Swimd_Folder_Struct_List* lst) {
    free(lst->arr);
}

void swimd_filelist_init(Swimd_File_List* lst) {
    int default_size = 4;
    lst->arr = malloc(default_size * sizeof(Swimd_File));
    lst->length = 0;
    lst->capacity = default_size;
}

void swimd_filelist_append(Swimd_File_List* lst, Swimd_File file) {
    if (lst->length == lst->capacity) {
        lst->capacity = lst->capacity * 2;
        lst->arr = realloc(lst->arr, lst->capacity * sizeof(Swimd_File));
    }
    lst->arr[lst->length] = file;
    lst->length++;
    if (lst->length % 10000 == 0)
        printf("lst->length = %d\n", lst->length);
}

void swimd_filelist_free(Swimd_File_List* lst) {
    free(lst->arr);
}

void swimd_list_directories(const char* root_dir,
        Swimd_File_List* file_list,
        Swimd_Folder_Struct* root_folder) {

    char root_mask[MAX_PATH_LENGTH];
    char inner_folder[MAX_PATH_LENGTH];

    strcpy(root_mask, root_dir);
    strcat(root_mask, "\\*");

    WIN32_FIND_DATA find_file_data;
    HANDLE h_find;

    h_find = FindFirstFile(root_mask, &find_file_data);

    if (h_find == INVALID_HANDLE_VALUE) {
        printf("FindFirstFile failed (%lu)\n", GetLastError());
        return;
    }

    do {
        const char* current_file = find_file_data.cFileName;
        int current_file_len = strlen(current_file);

        if (find_file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (strcmp(current_file, ".") != 0 && strcmp(current_file, "..") != 0) {
                char* folder_name = malloc((current_file_len + 1) * sizeof(char));
                strcpy(folder_name, current_file);

                Swimd_Folder_Struct* folder_node = malloc(sizeof(Swimd_Folder_Struct));

                folder_node->name = folder_name;
                folder_node->name_len = current_file_len;
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

            Swimd_File file_node = {
                .name = file_name,
                .name_len = current_file_len,
                .folder = root_folder
            };

            swimd_filelist_append(file_list, file_node);
        }
    } while (FindNextFile(h_find, &find_file_data) != 0);

    DWORD dw_error = GetLastError();
    if (dw_error != ERROR_NO_MORE_FILES) {
        printf("FindNextFile error (%lu)\n", dw_error);
    }

    FindClose(h_find);
}

void swimd_list_directories_folders_free(Swimd_Folder_Struct* root_folder) {
    for (int i = 0; i < root_folder->folder_lst.length; i++) {
        Swimd_Folder_Struct* folder = root_folder->folder_lst.arr[i];
        swimd_list_directories_folders_free(folder);
        swimd_folders_free(&folder->folder_lst);
        free(folder->name);
        free(folder);
    }
}

void swimd_list_directories_free(const Swimd_File_List* lst,
        Swimd_Folder_Struct* root_folder) {
    swimd_list_directories_folders_free(root_folder);
    for (int i = 0; i < lst->length; i++) {
        free(lst->arr[i].name);
    }
}

void swimd_prep_files_vec(Swimd_Algo_State* state) {
    Swimd_File_List files = state->files;
    int files_length = files.length;
    int files_vec_length = CEIL_DIV(files_length, LANES_COUNT_SHORT);
    Swimd_File_Vec* files_vec = malloc(files_vec_length * sizeof(Swimd_File_Vec));

    for (int i = 0; i < files_vec_length; i++) {
        int max_length = 0;
        for (int j = 0; j < LANES_COUNT_SHORT; j++) {
            if (i * LANES_COUNT_SHORT + j >= files_length)
                break;
            max_length = MAX(max_length, files.arr[i * LANES_COUNT_SHORT + j].name_len);
        }

        int file_vec_length = max_length * LANES_COUNT_SHORT;
        short* file_vec_arr = malloc(file_vec_length * sizeof(short));
        memset(file_vec_arr, 0, file_vec_length * sizeof(short));

        for (int k = 0; k < max_length; k++) {
            for (int j = 0; j < LANES_COUNT_SHORT; j++) {
                if (i * LANES_COUNT_SHORT + j >= files_length)
                    break;
                Swimd_File file = files.arr[i * LANES_COUNT_SHORT + j];
                if (k >= file.name_len)
                    continue;
                file_vec_arr[k * LANES_COUNT_SHORT + j] = (short)file.name[k];
            }
        }
        files_vec[i] = (Swimd_File_Vec){
            .arr = file_vec_arr,
            .length = file_vec_length,
        };
    }
    state->files_vec = files_vec;
    state->files_vec_length = files_vec_length;
}

void swimd_prep_files_vec_free(Swimd_Algo_State* state) {
    for (int i = 0; i < state->files_vec_length; i++) {
        Swimd_File_Vec file_vec = state->files_vec[i];
        free(file_vec.arr);
    }
    free(state->files_vec);
}

void swimd_prep_needle_vec(Swimd_Algo_State* state) {
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

void swimd_prep_needle_vec_free(Swimd_Algo_State* state) {
    free(state->needle_vec);
}

void swimd_setup_needle(char* needle) {
    swimd_state.needle = needle;
    swimd_state.needle_length = strlen(needle);
    swimd_prep_needle_vec(&swimd_state);
}

void swimd_setup_needle_free() {
    swimd_prep_needle_vec_free(&swimd_state);
}

void swimd_prep_d_vec(Swimd_Algo_State* state) {
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

void swimd_vec_sum() {
    short a[16] = {1,2,3,4,5,6,7,8, 1,2,3,4,5,6,7,80};
    short b[16] = {1,20,30,40,50,60,70,80, 10,20,30,40,50,60,70,80};
    short c[16];
    short diag[16] = {0, 0, 0, 0, 1, 1, 2, 2, 0, 0, 0, 0, -1, -1, -1, -1};
    short up[16] = {1, 1, 1, 1, 10, 10, 10, 10, 5, 5, 5, 5, -1, -1, -1, -1};
    short left[16] = {0, 0, 0, 0, 3, 3, 3, 3, 3, 3, 3, 3, -1, -1, -1, -1};

    Vector sub_pen = _mm256_set1_epi16(-SUB_PENALTY);
    Vector eq_reward = _mm256_set1_epi16(SUB_PENALTY);
    Vector gap = _mm256_set1_epi16(GAP_PENALTY);

    Vector va = _mm256_loadu_si256((__m256i const*)a);
    Vector vb = _mm256_loadu_si256((__m256i const*)b);
    Vector vdiag = _mm256_loadu_si256((__m256i const*)diag);
    Vector vup = _mm256_loadu_si256((__m256i const*)up);
    Vector vleft = _mm256_loadu_si256((__m256i const*)left);

    Vector veq = _mm256_cmpeq_epi16(va, vb);
    Vector c1 = _mm256_and_si256(eq_reward, veq);
    Vector c2 = _mm256_andnot_si256(veq, sub_pen); // flipped
    Vector o1 = _mm256_add_epi16(c1, c2);
    o1 = _mm256_add_epi16(o1, vdiag);

    Vector o2 = _mm256_sub_epi16(vup, gap);
    Vector o3 = _mm256_sub_epi16(vleft, gap);

    Vector o = _mm256_max_epi16(o1, o2);
    o = _mm256_max_epi16(o, o3);

    _mm256_storeu_si256((__m256i*)c, o);

    for (int i = 0; i < 16; i++) {
        printf("%d ", c[i]);
    }
    printf("\n");
}

void swimd_vec_estimate() {

}

void swimd_state_init(const char* root_path) {
    swimd_filelist_init(&swimd_state.files);
    swimd_folders_init(&swimd_state.folders.folder_lst);
    swimd_list_directories(root_path,
        &swimd_state.files,
        &swimd_state.folders);

    swimd_prep_files_vec(&swimd_state);
    swimd_prep_d_vec(&swimd_state);
}

void swimd_state_free() {
    swimd_prep_files_vec_free(&swimd_state);
    swimd_list_directories_free(&swimd_state.files,
            &swimd_state.folders);
    swimd_folders_free(&swimd_state.folders.folder_lst);
    swimd_filelist_free(&swimd_state.files);
}

int main() {
    swimd_vec_sum();
    // const char* root_path = "c:\\temp";
    // while (1) {
    //     swimd_state_init(root_path);
    //     swimd_setup_needle("hello");
    //
    //     swimd_setup_needle_free();
    //     swimd_state_free();
    //
    //     getchar();
    // }
    // return 0;
}
