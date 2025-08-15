#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "editor.h"
#include "utf8.h"
#include "normal.h"
#include "history.h"

int insert_handle_input(const char *ch_str) {
    if (ch_str[0] == 27 && ch_str[1] == '\0') { // ESC
        history_end_coalescing(editor_get_active_buffer()->history);
        editor_handle_input = normal_handle_input;
        editor_needs_draw();
        return 1;
    }
    if ((ch_str[0] == 8 || ch_str[0] == 127) && ch_str[1] == '\0') { // backspace
        editor_backspace();
        // TODO: handle backspace in insertion_buffer for '.' command
        return 1;
    }

    const char *p = ch_str;
    const char *start_of_chunk = p;

    while (*p) {
        if (*p == '\n' || *p == '\r') {
            if (p > start_of_chunk) {
                size_t len = p - start_of_chunk;
                char* chunk = malloc(len + 1);
                strncpy(chunk, start_of_chunk, len);
                chunk[len] = '\0';
                normal_register_insertion_string(chunk);
                editor_insert_char(chunk);
                free(chunk);
            }
            editor_insert_new_line();
            normal_register_insertion('\n');
            if (*p == '\r' && *(p+1) == '\n') {
                 p++;
            }
            p++;
            start_of_chunk = p;
        } else {
            p += utf8_char_len(p);
        }
    }

    if (p > start_of_chunk) {
        size_t len = p - start_of_chunk;
        char* chunk = malloc(len + 1);
        strncpy(chunk, start_of_chunk, len);
        chunk[len] = '\0';
        normal_register_insertion_string(chunk);
        editor_insert_char(chunk);
        free(chunk);
    }

    return 1;
}
