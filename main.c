#include "immintrin.h"
#include "stdio.h"
#include "windows.h"

#define MAX_PATH_LENGTH 300
#define LANES_COUNT_SHORT 16

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
    short* needle_vec;

    Swimd_File_List files;
    Swimd_File_Vec* files_vec;

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
}

void swimd_vec_sum() {
    short a[16] = {1,2,3,4,5,6,7,8, 1,2,3,4,5,6,7,8};
    short b[16] = {10,20,30,40,50,60,70,80, 10,20,30,40,50,60,70,80};
    short c[16];

    __m256i va = _mm256_loadu_si256((__m256i const*)a);
    __m256i vb = _mm256_loadu_si256((__m256i const*)b);
    __m256i vsum = _mm256_add_epi16(va, vb);
    _mm256_storeu_si256((__m256i*)&c[0], vsum);

    for (int i = 0; i < 16; i++) {
        printf("%d ", c[i]);
    }
    printf("\n");
}

void swimd_init_state(const char* root_path) {
    swimd_filelist_init(&swimd_state.files);
    swimd_folders_init(&swimd_state.folders.folder_lst);
    swimd_list_directories(root_path, 
        &swimd_state.files, 
        &swimd_state.folders);

    printf("over %d\n", swimd_state.files.length);
    swimd_prep_files_vec(&swimd_state);

    // for (int i = 0; i < swimd_state.files.length; i++) {
    //     printf("%d |%s|\n", i, swimd_state.files.arr[i].name);
    // }
    printf("over %d\n", swimd_state.files.length);
    // for (int i = 0; i < swimd_state.files.length / LANES_COUNT_SHORT; i++) {
    //     printf("%d |%s|\n", i, swimd_state.files.arr[i].name);
    // }
    // printf("over %d\n", swimd_state.files.length);
}

int main() {
    const char* root_path = "c:\\temp";
    swimd_init_state(root_path);
    // Swimd_File_List lst = {0};
    // Swimd_Folder_Struct root = {0};
    //
    // swimd_filelist_init(&lst);
    // swimd_folders_init(&root.folder_lst);
    // const char* root_path = "c:\\temp";
    // swimd_list_directories(root_path, &lst, &root);
    //
    // swimd_prep_files_vec(&swimd_state);
    //
    // for (int i = 0; i < lst.length; i++) {
    //     printf("%d |%s|\n", i, lst.arr[i].name);
    // }
    // printf("over %d\n", lst.length);
    //
    // swimd_list_directories_free(&lst, &root);
    // swimd_folders_free(&root.folder_lst);
    //
    // swimd_filelist_free(&lst);

    return 0;
}
