#include "immintrin.h"
#include "stdio.h"
#include "windows.h"

#define MAX_PATH_LENGTH 300

typedef struct {
    char** paths;
    int length;
    int capacity;
} FileList;

void swimd_filelist_init(FileList *lst) {
    int defaultSize = 4;
    lst->paths = malloc(defaultSize * sizeof(char*));
    lst->length = 0;
    lst->capacity = defaultSize;
}

void swimd_filelist_append(FileList* lst, char* path) {
    if (lst->length == lst->capacity) {
        lst->capacity = lst->capacity * 2;
        lst->paths = realloc(lst->paths, lst->capacity * sizeof(char*));
    }
    lst->paths[lst->length] = path;
    lst->length++;
}

void swimd_filelist_free(FileList* lst) {
    free(lst->paths);
}

void swimd_list_directories(const char* rootDir, FileList* fileList) {
    char rootMask[MAX_PATH_LENGTH];
    char innerFolder[MAX_PATH_LENGTH];
    strcpy(rootMask, rootDir);
    strcat(rootMask, "\\*");


    WIN32_FIND_DATA findFileData;
    HANDLE hFind;

    hFind = FindFirstFile(rootMask, &findFileData);

    if (hFind == INVALID_HANDLE_VALUE) {
        printf("FindFirstFile failed (%lu)\n", GetLastError());
        return;
    } 

    do {
        const char* currentFile = findFileData.cFileName;

        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (strcmp(currentFile, ".") != 0 && strcmp(currentFile, "..") != 0) {

                strcpy(innerFolder, rootDir);
                strcat(innerFolder, "\\");
                strcat(innerFolder, currentFile);

                swimd_list_directories(innerFolder, fileList);
            }
        } else {
            char* fullPath = malloc(MAX_PATH_LENGTH * sizeof(char*));

            strcpy(fullPath, rootDir);
            strcat(fullPath, "\\");
            strcat(fullPath, currentFile);

            swimd_filelist_append(fileList, fullPath);
        }
    } while (FindNextFile(hFind, &findFileData) != 0);

    DWORD dwError = GetLastError();
    if (dwError != ERROR_NO_MORE_FILES) {
        printf("FindNextFile error (%lu)\n", dwError);
    }

    FindClose(hFind);
}

void swimd_list_directories_free(const FileList* lst) {
    for (int i = 0; i < lst->length; i++) {
        free(lst->paths[i]);
    }
}

void swimd_vec_sum() {
    short a[8] = {1,2,3,4,5,6,7,8};
    short b[8] = {10,20,30,40,50,60,70,80};
    short c[8];

    __m128i vec_a = _mm_loadu_si128((__m128i*)a);
    __m128i vec_b = _mm_loadu_si128((__m128i*)b);
    __m128i vec_c = _mm_add_epi16(vec_a, vec_b);
    _mm_storeu_si128((__m128i*)c, vec_c);

    for (int i = 0; i < 8; i++) {
        printf("%d ", c[i]);
    }
    printf("\n");
}

int main() {
    // FileList lst = {0};
    // swimd_filelist_init(&lst);
    // swimd_list_directories("c:\\temp", &lst);
    //
    // for (int i = 0; i < lst.length; i++) {
    //     printf("%s\n", lst.paths[i]);
    // }
    //
    // swimd_list_directories_free(&lst);
    //
    // swimd_filelist_free(&lst);
    //
    // return 0;
}
