#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include "fuzzy.h"
#include "picker.h"
#include "picker_diagnostics.h"
#include "editor.h"
#include "lsp.h"
#include "buffer.h"
#include "log.h"
#include "str.h"

// For both pickers
static Diagnostic *diagnostics = NULL;
static int diagnostic_count = 0;
static char **formatted_diagnostics = NULL;
static int *filtered_indices = NULL;
static int results_count = 0;
static int is_workspace_diagnostics = 0;

// Common cleanup function
static void on_close() {
    if (diagnostics) {
        for (int i = 0; i < diagnostic_count; i++) {
            free(diagnostics[i].message);
            if (is_workspace_diagnostics && diagnostics[i].uri) {
                free(diagnostics[i].uri);
            }
        }
        free(diagnostics);
        diagnostics = NULL;
    }
    if (formatted_diagnostics) {
        for (int i = 0; i < diagnostic_count; i++) {
            free(formatted_diagnostics[i]);
        }
        free(formatted_diagnostics);
        formatted_diagnostics = NULL;
    }
    if (filtered_indices) {
        free(filtered_indices);
        filtered_indices = NULL;
    }
    diagnostic_count = 0;
    results_count = 0;
}

// Common delegate functions
static int get_item_count() {
    return diagnostic_count;
}

static const char* get_item_text(int index) {
    return formatted_diagnostics[index];
}

static void update_results(const char *search) {
    results_count = fuzzy_search((const char**)formatted_diagnostics, diagnostic_count, search, filtered_indices);
}

static int get_results_count() {
    return results_count;
}

static int get_result_index(int result_idx) {
    return filtered_indices[result_idx];
}

static void get_item_style(int index, PickerItemStyle *style) {
    style->style = 0;
    switch (diagnostics[index].severity) {
        case LSP_DIAGNOSTIC_SEVERITY_ERROR:
            style->flag = 'E';
            break;
        case LSP_DIAGNOSTIC_SEVERITY_WARNING:
            style->flag = 'W';
            break;
        case LSP_DIAGNOSTIC_SEVERITY_INFO:
            style->flag = 'I';
            break;
        case LSP_DIAGNOSTIC_SEVERITY_HINT:
            style->flag = 'H';
            break;
        default:
            style->flag = ' ';
            break;
    }
}


// --- File Diagnostics Picker ---

static void file_on_open() {
    is_workspace_diagnostics = 0;
    Buffer *buffer = editor_get_active_buffer();
    if (!buffer || !buffer->file_name) {
        return;
    }
    lsp_get_diagnostics(buffer->file_name, &diagnostics, &diagnostic_count);
    if (diagnostic_count == 0) {
        return;
    }

    formatted_diagnostics = malloc(sizeof(char*) * diagnostic_count);
    filtered_indices = malloc(sizeof(int) * diagnostic_count);

    for (int i = 0; i < diagnostic_count; i++) {
        // Format: "line:col | message"
        int len = snprintf(NULL, 0, "%d:%d | %s", diagnostics[i].line + 1, diagnostics[i].col_start + 1, diagnostics[i].message);
        formatted_diagnostics[i] = malloc(len + 1);
        snprintf(formatted_diagnostics[i], len + 1, "%d:%d | %s", diagnostics[i].line + 1, diagnostics[i].col_start + 1, diagnostics[i].message);
    }

    results_count = fuzzy_search((const char**)formatted_diagnostics, diagnostic_count, "", filtered_indices);
}

static void file_on_select(int selection_idx, int *close_picker) {
    int original_index = filtered_indices[selection_idx];
    Diagnostic *d = &diagnostics[original_index];
    Buffer *buffer = editor_get_active_buffer();
    if (buffer && buffer->file_name) {
        editor_open_and_jump_to_line(buffer->file_name, d->line + 1, d->col_start + 1);
    }
    *close_picker = 1;
}

static PickerDelegate file_delegate = {
    .on_open = file_on_open,
    .on_close = on_close,
    .on_select = file_on_select,
    .get_item_count = get_item_count,
    .get_item_text = get_item_text,
    .update_results = update_results,
    .get_results_count = get_results_count,
    .get_result_index = get_result_index,
    .get_item_style = get_item_style,
};

void picker_file_diagnostics_show() {
    picker_set_delegate(&file_delegate);
    picker_open();
}


// --- Workspace Diagnostics Picker ---

static void workspace_on_open() {
    is_workspace_diagnostics = 1;
    diagnostic_count = lsp_get_all_diagnostics(&diagnostics);
    if (diagnostic_count == 0) {
        return;
    }

    formatted_diagnostics = malloc(sizeof(char*) * diagnostic_count);
    filtered_indices = malloc(sizeof(int) * diagnostic_count);

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        log_error("picker_diagnostics.workspace_on_open: unable to getcwd");
        return;
    }
    for (int i = 0; i < diagnostic_count; i++) {
        const char *path = "??";
        if (diagnostics[i].uri) {
            path = str_uri_to_relative_path(diagnostics[i].uri, cwd);
        }

        // Format: "path:line:col | message"
        int len = snprintf(NULL, 0, "%s:%d:%d | %s", path, diagnostics[i].line + 1, diagnostics[i].col_start + 1, diagnostics[i].message);
        formatted_diagnostics[i] = malloc(len + 1);
        snprintf(formatted_diagnostics[i], len + 1, "%s:%d:%d | %s", path, diagnostics[i].line + 1, diagnostics[i].col_start + 1, diagnostics[i].message);
    }

    results_count = fuzzy_search((const char**)formatted_diagnostics, diagnostic_count, "", filtered_indices);
}


static void workspace_on_select(int selection_idx, int *close_picker) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        log_error("picker_diagnostics.workspace_on_select: unable to getcwd");
        return;
    }
    int original_index = filtered_indices[selection_idx];
    Diagnostic *d = &diagnostics[original_index];
    if (d->uri) {
        const char *path = str_uri_to_relative_path(d->uri, cwd);
        editor_open_and_jump_to_line(path, d->line + 1, d->col_start + 1);
    }
    *close_picker = 1;
}

static PickerDelegate workspace_delegate = {
    .on_open = workspace_on_open,
    .on_close = on_close,
    .on_select = workspace_on_select,
    .get_item_count = get_item_count,
    .get_item_text = get_item_text,
    .update_results = update_results,
    .get_results_count = get_results_count,
    .get_result_index = get_result_index,
    .get_item_style = get_item_style,
};

void picker_workspace_diagnostics_show() {
    picker_set_delegate(&workspace_delegate);
    picker_open();
}
