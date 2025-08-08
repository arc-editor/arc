#include <string.h>
#include "editor.h"
#include "search.h"
#include "normal.h"
#include "buffer.h"
#include "editor_state.h"

extern EditorState editor;

void search_init(int direction) {
    editor.search_direction = direction;
    editor.search_term_len = 0;
    editor.search_term[0] = '\0';
    editor_handle_input = search_handle_input;

    int buffer_count;
    Buffer **buffers = editor_get_buffers(&buffer_count);
    for (int i = 0; i < buffer_count; i++) {
        buffer_clear_search_state(buffers[i]);
    }
}

const char *search_get_last_term(void) {
    return editor.last_search_term;
}

int search_get_last_direction(void) {
    return editor.last_search_direction;
}

const char *search_get_term(void) {
    return editor.search_term;
}

char search_get_prompt_char(void) {
    return editor.search_direction == 1 ? '/' : '?';
}

int search_handle_input(const char *ch_str) {
    if (ch_str[0] == 27 && ch_str[1] == '\0') { // Escape key
        editor_handle_input = normal_handle_input;
        editor_needs_draw();
        return 1;
    }

    if (strcmp(ch_str, "\x0d") == 0) { // Enter key
        if (editor.search_term_len > 0) {
            strcpy(editor.last_search_term, editor.search_term);
            editor.last_search_direction = editor.search_direction;
        } else {
            editor.last_search_term[0] = '\0';
        }

        Buffer *buffer = editor_get_active_buffer();
        int y = buffer->position_y;
        int x = buffer->position_x;

        if (editor.search_direction == 1) {
            if (buffer_find_forward(buffer, editor.last_search_term, &y, &x)) {
                buffer->position_y = y;
                buffer->position_x = x;
                editor_center_view();
            }
        } else {
            if (buffer_find_backward(buffer, editor.last_search_term, &y, &x)) {
                buffer->position_y = y;
                buffer->position_x = x;
                editor_center_view();
            }
        }
        buffer_update_search_matches(buffer, editor.last_search_term);

        editor_handle_input = normal_handle_input;
        editor_needs_draw();
        return 1;
    }

    if (strcmp(ch_str, "\x1b[Z") == 0) { // Shift-Enter, using Shift-Tab for now
        if (editor.search_term_len > 0) {
            strcpy(editor.last_search_term, editor.search_term);
            editor.last_search_direction = editor.search_direction;
        }

        Buffer *buffer = editor_get_active_buffer();
        int y = buffer->position_y;
        int x = buffer->position_x;

        if (editor.search_direction == 1) {
            if (buffer_find_backward(buffer, editor.last_search_term, &y, &x)) {
                buffer->position_y = y;
                buffer->position_x = x;
            }
        } else {
            if (buffer_find_forward(buffer, editor.last_search_term, &y, &x)) {
                buffer->position_y = y;
                buffer->position_x = x;
            }
        }
    }

    if (strcmp(ch_str, "\x7f") == 0) { // Backspace
        if (editor.search_term_len > 0) {
            editor.search_term_len--;
            editor.search_term[editor.search_term_len] = '\0';
            editor_needs_draw();
        }
        return 1;
    }

    // Handle regular character input
    if (strlen(ch_str) == 1 && editor.search_term_len < SEARCH_TERM_MAX_LEN - 1) {
        editor.search_term[editor.search_term_len++] = ch_str[0];
        editor.search_term[editor.search_term_len] = '\0';
        editor_needs_draw();
    }

    return 1;
}
