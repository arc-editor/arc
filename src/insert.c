#define _GNU_SOURCE
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "editor.h"
#include "normal.h"
#include "history.h"
#include "utf8.h"

static int is_pasting = 0;
static char *paste_buffer = NULL;
static size_t paste_buffer_len = 0;
static size_t paste_buffer_capacity = 0;

static void append_to_paste_buffer(const char *s, size_t len) {
    if (!paste_buffer) {
        paste_buffer_capacity = 1024;
        if (paste_buffer_capacity < len) {
            paste_buffer_capacity = len * 2;
        }
        paste_buffer = malloc(paste_buffer_capacity);
        paste_buffer_len = 0;
    } else if (paste_buffer_len + len >= paste_buffer_capacity) {
        paste_buffer_capacity = (paste_buffer_len + len) * 2;
        paste_buffer = realloc(paste_buffer, paste_buffer_capacity);
    }
    memcpy(paste_buffer + paste_buffer_len, s, len);
    paste_buffer_len += len;
}

int insert_handle_input(const char *buf, int len) {
    const char *p = buf;
    const char *end = buf + len;

    while (p < end) {
        if (is_pasting) {
            const char *paste_end = memmem(p, end - p, "\x1b[201~", 6);
            if (paste_end) {
                size_t part_len = paste_end - p;
                append_to_paste_buffer(p, part_len);

                if (paste_buffer_len > 0) {
                    if (paste_buffer_len + 1 > paste_buffer_capacity) {
                        paste_buffer = realloc(paste_buffer, paste_buffer_len + 1);
                    }
                    paste_buffer[paste_buffer_len] = '\0';
                    editor_insert_string(paste_buffer);
                }

                free(paste_buffer);
                paste_buffer = NULL;
                paste_buffer_len = 0;
                paste_buffer_capacity = 0;
                is_pasting = 0;
                p = paste_end + 6;
            } else {
                append_to_paste_buffer(p, end - p);
                p = end;
            }
        } else {
            if (p + 6 <= end && strncmp(p, "\x1b[200~", 6) == 0) {
                is_pasting = 1;
                p += 6;
            } else {
                // Handle single char
                if (*p == 27) { // ESC
                    history_end_coalescing(editor_get_active_buffer()->history);
                    editor_handle_input = normal_handle_input;
                    editor_needs_draw();
                    p++;
                    continue;
                }
                if (*p == 127 || *p == 8) { // Backspace
                    editor_backspace();
                    p++;
                    continue;
                }

                int char_len = utf8_char_len(p);
                if (p + char_len > end) {
                    return 1;
                }

                char ch_str[8];
                strncpy(ch_str, p, char_len);
                ch_str[char_len] = '\0';

                if (strcmp(ch_str, "\r") == 0 || strcmp(ch_str, "\n") == 0) {
                    editor_insert_new_line();
                } else {
                    for (int i = 0; i < char_len; i++) {
                        normal_register_insertion(p[i]);
                    }
                    editor_insert_char(ch_str);
                }
                p += char_len;
            }
        }
    }
    return 1;
}
