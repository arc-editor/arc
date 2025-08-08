#include <string.h>
#include "editor.h"
#include "search.h"
#include "normal.h"

#define SEARCH_TERM_MAX_LEN 256

static char search_term[SEARCH_TERM_MAX_LEN];
static int search_term_len = 0;
static int search_direction = 1; // 1 for forward, -1 for backward
static char last_search_term[SEARCH_TERM_MAX_LEN] = {0};
static int last_search_direction = 1;


void search_init(int direction) {
    search_direction = direction;
    search_term_len = 0;
    search_term[0] = '\0';
    editor_handle_input = search_handle_input;
}

const char *search_get_term(void) {
    return search_term;
}

char search_get_prompt_char(void) {
    return search_direction == 1 ? '/' : '?';
}

int search_handle_input(const char *ch_str) {
    if (ch_str[0] == 27 && ch_str[1] == '\0') { // Escape key
        editor_handle_input = normal_handle_input;
        editor_needs_draw();
        return 1;
    }

    if (strcmp(ch_str, "\x0d") == 0) { // Enter key
        if (search_term_len > 0) {
            strcpy(last_search_term, search_term);
            last_search_direction = search_direction;
        }

        Buffer *buffer = editor_get_active_buffer();
        int y = buffer->position_y;
        int x = buffer->position_x;

        if (search_direction == 1) {
            if (buffer_find_forward(buffer, last_search_term, &y, &x)) {
                buffer->position_y = y;
                buffer->position_x = x;
            }
        } else {
            if (buffer_find_backward(buffer, last_search_term, &y, &x)) {
                buffer->position_y = y;
                buffer->position_x = x;
            }
        }

        editor_handle_input = normal_handle_input;
        editor_needs_draw();
        return 1;
    }

    if (strcmp(ch_str, "\x1b[Z") == 0) { // Shift-Enter, using Shift-Tab for now
        if (search_term_len > 0) {
            strcpy(last_search_term, search_term);
            last_search_direction = search_direction;
        }

        Buffer *buffer = editor_get_active_buffer();
        int y = buffer->position_y;
        int x = buffer->position_x;

        if (search_direction == 1) {
            if (buffer_find_backward(buffer, last_search_term, &y, &x)) {
                buffer->position_y = y;
                buffer->position_x = x;
            }
        } else {
            if (buffer_find_forward(buffer, last_search_term, &y, &x)) {
                buffer->position_y = y;
                buffer->position_x = x;
            }
        }
    }

    if (strcmp(ch_str, "\x7f") == 0) { // Backspace
        if (search_term_len > 0) {
            search_term_len--;
            search_term[search_term_len] = '\0';
            editor_needs_draw();
        }
        return 1;
    }

    // Handle regular character input
    if (strlen(ch_str) == 1 && search_term_len < SEARCH_TERM_MAX_LEN - 1) {
        search_term[search_term_len++] = ch_str[0];
        search_term[search_term_len] = '\0';
        editor_needs_draw();
    }

    return 1;
}
