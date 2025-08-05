#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "fuzzy.h"
#include "picker.h"
#include "editor.h"

static char **files = NULL;
static int file_count = 0;
static int file_capacity = 0;
static int *filtered_indices = NULL;
static int results_count = 0;

static void add_file_to_list(const char *filepath) {
    if (file_count >= file_capacity) {
        file_capacity = file_capacity == 0 ? 16 : file_capacity * 2;
        files = realloc(files, sizeof(char*) * file_capacity);
        filtered_indices = realloc(filtered_indices, sizeof(int) * file_capacity);
    }
    files[file_count++] = strdup(filepath);
}

static void scan_directory_recursive(const char *dirpath) {
    DIR *dir = opendir(dirpath);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, entry->d_name);
        struct stat statbuf;
        if (stat(fullpath, &statbuf) == 0) {
            if (S_ISREG(statbuf.st_mode)) {
                const char *clean_path = strncmp(fullpath, "./", 2) == 0 ? fullpath + 2 : fullpath;
                add_file_to_list(clean_path);
            } else if (S_ISDIR(statbuf.st_mode)) {
                scan_directory_recursive(fullpath);
            }
        }
    }
    closedir(dir);
}

static void on_open() {
    if (file_count == 0) {
        scan_directory_recursive(".");
    }
    results_count = fuzzy_search((const char**)files, file_count, "", filtered_indices);
}

static void on_close() {
    for (int i = 0; i < file_count; i++) {
        free(files[i]);
    }
    free(files);
    free(filtered_indices);
    files = NULL;
    filtered_indices = NULL;
    file_count = 0;
    file_capacity = 0;
    results_count = 0;
}

static void on_select(int selection_idx, int *close_picker) {
    editor_open(files[filtered_indices[selection_idx]]);
    *close_picker = 1;
}

static int get_item_count() {
    return file_count;
}

static const char* get_item_text(int index) {
    return files[index];
}

static void update_results(const char *search) {
    results_count = fuzzy_search((const char**)files, file_count, search, filtered_indices);
}

static int get_results_count() {
    return results_count;
}

static int get_result_index(int result_idx) {
    return filtered_indices[result_idx];
}

static PickerDelegate delegate = {
    .on_open = on_open,
    .on_close = on_close,
    .on_select = on_select,
    .get_item_count = get_item_count,
    .get_item_text = get_item_text,
    .update_results = update_results,
    .get_results_count = get_results_count,
    .get_result_index = get_result_index,
    .get_item_style = NULL,
};

void picker_file_show() {
    picker_set_delegate(&delegate);
    picker_open();
}
