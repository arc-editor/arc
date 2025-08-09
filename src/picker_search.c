#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "picker.h"
#include "editor.h"
#include "log.h"
#include "picker_search.h"

// Data structure for a single search result
typedef struct {
    char *filename;
    int line_number;
    int column_number;
    char *content;
    char *display_text;
} SearchResult;

// List of all files to search
static char **files = NULL;
static int file_count = 0;
static int file_capacity = 0;

// List of search results
static SearchResult *results = NULL;
static int results_count = 0;
static int results_capacity = 0;

// --- File Scanner ---
static void add_file_to_list(const char *filepath) {
    if (file_count >= file_capacity) {
        file_capacity = file_capacity == 0 ? 1024 : file_capacity * 2;
        files = realloc(files, sizeof(char*) * file_capacity);
    }
    files[file_count++] = strdup(filepath);
}

static void scan_directory_recursive(const char *dirpath) {
    // Basic protection against recursive symlinks
    static int depth = 0;
    if (depth > 20) {
        return;
    }
    depth++;

    DIR *dir = opendir(dirpath);
    if (!dir) {
        depth--;
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        // Simple exclusion for .git
        if (strcmp(entry->d_name, ".git") == 0) {
            continue;
        }

        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, entry->d_name);
        struct stat statbuf;
        if (lstat(fullpath, &statbuf) == 0) {
            if (S_ISREG(statbuf.st_mode)) {
                const char *clean_path = strncmp(fullpath, "./", 2) == 0 ? fullpath + 2 : fullpath;
                add_file_to_list(clean_path);
            } else if (S_ISDIR(statbuf.st_mode) && !S_ISLNK(statbuf.st_mode)) {
                scan_directory_recursive(fullpath);
            }
        }
    }
    closedir(dir);
    depth--;
}

// --- Results Management ---
static void clear_results() {
    if (results) {
        for (int i = 0; i < results_count; i++) {
            free(results[i].filename);
            free(results[i].content);
            free(results[i].display_text);
        }
        free(results);
        results = NULL;
    }
    results_count = 0;
    results_capacity = 0;
}

static void add_result(const char *filename, int line_number, int column_number, const char *content) {
    if (results_count >= results_capacity) {
        results_capacity = results_capacity == 0 ? 128 : results_capacity * 2;
        results = realloc(results, sizeof(SearchResult) * results_capacity);
    }
    results[results_count].filename = strdup(filename);
    results[results_count].line_number = line_number;
    results[results_count].column_number = column_number;
    results[results_count].content = strdup(content);

    int display_text_len = snprintf(NULL, 0, "%s:%d:%d: %s", filename, line_number, column_number + 1, content);
    results[results_count].display_text = malloc(display_text_len + 1);
    snprintf(results[results_count].display_text, display_text_len + 1, "%s:%d:%d: %s", filename, line_number, column_number + 1, content);

    results_count++;
}


// --- Picker Delegate Functions ---
static void on_open() {
    if (file_count == 0) {
        scan_directory_recursive(".");
    }
}

static void on_close() {
    clear_results();
    // Free file list as well
    if (files) {
        for (int i = 0; i < file_count; i++) {
            free(files[i]);
        }
        free(files);
        files = NULL;
    }
    file_count = 0;
    file_capacity = 0;
}

static void on_select(int selection_idx, int *close_picker) {
    if (selection_idx < 0 || selection_idx >= results_count) {
        return;
    }
    SearchResult *result = &results[selection_idx];
    editor_open(result->filename);

    Buffer *b = editor_get_active_buffer();

    // Set line number, clamping to valid range
    b->position_y = result->line_number - 1;
    if (b->position_y >= b->line_count) {
        b->position_y = b->line_count > 0 ? b->line_count - 1 : 0;
    }

    // Set column number, clamping to valid range
    b->position_x = result->column_number;
    if (b->position_y < b->line_count) {
      BufferLine *current_line = b->lines[b->position_y];
      if (b->position_x >= current_line->char_count) {
          b->position_x = current_line->char_count > 0 ? current_line->char_count - 1 : 0;
      }
    } else {
      b->position_x = 0;
    }

    editor_center_view();
    editor_request_redraw();
    *close_picker = 1;
}

static const char* get_item_text(int index) {
    if (index < 0 || index >= results_count) {
        return "";
    }
    return results[index].display_text;
}

static void update_results(const char *search) {
    clear_results();
    if (strlen(search) == 0) {
        editor_request_redraw();
        return;
    }

    for (int i = 0; i < file_count; i++) {
        FILE *f = fopen(files[i], "r");
        if (!f) continue;

        char *line = NULL;
        size_t len = 0;
        int line_number = 1;
        while (getline(&line, &len, f) != -1) {
            char *line_ptr = line;
            char *match;
            while ((match = strstr(line_ptr, search))) {
                // Remove trailing newline
                line[strcspn(line, "\n")] = 0;
                int column_number = match - line;
                add_result(files[i], line_number, column_number, line);
                line_ptr = match + 1;
            }
            line_number++;
        }
        free(line);
        fclose(f);
    }
    editor_request_redraw();
}

static int get_results_count() {
    return results_count;
}

static int get_result_index(int result_idx) {
    return result_idx;
}

static PickerDelegate delegate = {
    .on_open = on_open,
    .on_close = on_close,
    .on_select = on_select,
    .get_item_count = get_results_count,
    .get_item_text = get_item_text,
    .update_results = update_results,
    .get_results_count = get_results_count,
    .get_result_index = get_result_index,
    .get_item_style = NULL,
};

void picker_search_show() {
    picker_set_delegate(&delegate);
    picker_open();
}
