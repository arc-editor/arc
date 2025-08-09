#include <string.h>
#include "editor.h"
#include "search.h"
#include "normal.h"
#include "buffer.h"

extern Editor editor;

void search_init(int direction) {
    editor.search_direction = direction;
    editor.search_term_len = 0;
    editor.search_term[0] = '\0';
    editor_handle_input = search_handle_input;

    Buffer *buffer = editor_get_active_buffer();
    editor.search_start_y = buffer->position_y;
    editor.search_start_x = buffer->position_x;

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

static void search_update_and_jump() {
    Buffer *buffer = editor_get_active_buffer();
    buffer_update_search_matches(buffer, editor.search_term);

    int match_y, match_x;
    if (buffer_find_first_match(buffer, editor.search_term, editor.search_start_y, editor.search_start_x, &match_y, &match_x)) {
        buffer->position_y = match_y;
        buffer->position_x = match_x;
        editor_center_view();
    } else {
        buffer->position_y = editor.search_start_y;
        buffer->position_x = editor.search_start_x;
    }
    editor_needs_draw();
}

int search_handle_input(const char *ch_str) {
    if (ch_str[0] == 27 && ch_str[1] == '\0') { // Escape key
        Buffer *buffer = editor_get_active_buffer();
        buffer->position_y = editor.search_start_y;
        buffer->position_x = editor.search_start_x;
        buffer_update_search_matches(buffer, editor.last_search_term);
        editor_handle_input = normal_handle_input;
        editor_needs_draw();
        return 1;
    }

    if (strcmp(ch_str, "\x0d") == 0) { // Enter key
        if (editor.search_term_len > 0) {
            strcpy(editor.last_search_term, editor.search_term);
            editor.last_search_direction = editor.search_direction;
        }
        editor_handle_input = normal_handle_input;
        editor_needs_draw();
        return 1;
    }

    if (strcmp(ch_str, "\x7f") == 0) { // Backspace
        if (editor.search_term_len > 0) {
            editor.search_term_len--;
            editor.search_term[editor.search_term_len] = '\0';
            if (editor.search_term_len == 0) {
                buffer_clear_search_state(editor_get_active_buffer());
                Buffer *buffer = editor_get_active_buffer();
                buffer->position_y = editor.search_start_y;
                buffer->position_x = editor.search_start_x;
                editor_needs_draw();
            } else {
                search_update_and_jump();
            }
        }
        return 1;
    }

    // Handle regular character input
    if (strlen(ch_str) == 1 && editor.search_term_len < SEARCH_TERM_MAX_LEN - 1) {
        editor.search_term[editor.search_term_len++] = ch_str[0];
        editor.search_term[editor.search_term_len] = '\0';
        search_update_and_jump();
    }

    return 1;
}
