#include <stdlib.h>
#include "fuzzy.h"
#include "picker.h"
#include "editor.h"
#include "buffer.h"

static int *filtered_indices = NULL;
static int results_count = 0;
static int results_capacity = 0;

static char **buffer_names = NULL;
static int buffer_names_capacity = 0;

static void on_open() {
    int buffer_count;
    Buffer **buffers = editor_get_buffers(&buffer_count);

    if (buffer_count > buffer_names_capacity) {
        buffer_names_capacity = buffer_count;
        buffer_names = realloc(buffer_names, sizeof(char*) * buffer_names_capacity);
        results_capacity = buffer_count;
        filtered_indices = realloc(filtered_indices, sizeof(int) * results_capacity);
    }

    int real_buffer_count = 0;
    for (int i = 0; i < buffer_count; i++) {
        if (buffers[i]->file_name) {
            buffer_names[real_buffer_count] = buffers[i]->file_name;
            real_buffer_count++;
        }
    }
    results_count = fuzzy_search((const char**)buffer_names, real_buffer_count, "", filtered_indices);
}

static void on_close() {
    if (filtered_indices) {
        free(filtered_indices);
        filtered_indices = NULL;
    }
    if (buffer_names) {
        free(buffer_names);
        buffer_names = NULL;
    }
    results_count = 0;
    results_capacity = 0;
    buffer_names_capacity = 0;
}

static void on_select(int selection_idx, int *close_picker) {
    editor_set_active_buffer(filtered_indices[selection_idx]);
    *close_picker = 1;
}

static int get_item_count() {
    int count;
    editor_get_buffers(&count);
    return count;
}

static const char* get_item_text(int index) {
    int buffer_count;
    Buffer **buffers = editor_get_buffers(&buffer_count);
    if (index < buffer_count) {
        return buffers[index]->file_name ? buffers[index]->file_name : "[No Name]";
    }
    return "";
}

static void update_results(const char *search) {
    int buffer_count;
    editor_get_buffers(&buffer_count);
    results_count = fuzzy_search((const char**)buffer_names, buffer_count, search, filtered_indices);
}

static int get_results_count() {
    return results_count;
}

static int get_result_index(int result_idx) {
    return filtered_indices[result_idx];
}

static void get_item_style(int index, PickerItemStyle *style) {
    int buffer_count;
    Buffer **buffers = editor_get_buffers(&buffer_count);
    if (index < buffer_count) {
        if (buffers[index]->dirty) {
            style->flag = '+';
        } else {
            style->flag = ' ';
        }
        if (index == editor_get_active_buffer_idx()) {
            style->style |= STYLE_BOLD;
        } else {
            style->style &= ~STYLE_BOLD;
        }
        style->style &= ~STYLE_ITALIC;
    }
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
    .get_item_style = get_item_style,
};

void picker_buffer_show() {
    picker_set_delegate(&delegate);
    picker_open();
}
