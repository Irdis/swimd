#include "immintrin.h"
#include "stdio.h"
#include "windows.h"

#define MAX_PATH_LENGTH 300

typedef struct Swimd_Folder_Struct Swimd_Folder_Struct;

typedef struct {
    Swimd_Folder_Struct** arr;
    int length;
    int capacity;
} Swimd_Folder_Struct_List;

typedef struct Swimd_Folder_Struct
{
    char* name;
    Swimd_Folder_Struct_List folder_lst;
    struct Swimd_Folder_Struct* parent;
} Swimd_Folder_Struct;

typedef struct {
    char* name;
    Swimd_Folder_Struct* folder;
} Swimd_File;

typedef struct {
    Swimd_File* arr;
    int length;
    int capacity;
} Swimd_File_List;

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

int main() {
    Swimd_File_List lst = {0};
    Swimd_Folder_Struct root = {0};

    swimd_filelist_init(&lst);
    swimd_folders_init(&root.folder_lst);
    const char* rootDir = "c:\\temp";
    swimd_list_directories(rootDir, &lst, &root);

    for (int i = 0; i < lst.length; i++) {
        printf("%d |%s|\n", i, lst.arr[i].name);
    }
    printf("over %d\n", lst.length);
    // getchar();

    swimd_list_directories_free(&lst, &root);
    swimd_folders_free(&root.folder_lst);

    swimd_filelist_free(&lst);

    return 0;
}
