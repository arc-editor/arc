#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <math.h>
#include "buffer.h"
#include "log.h"
#include "config.h"
#include "theme.h"
#include "history.h"
#include "utf8.h"

void buffer_set_line_num_width(Buffer *buffer) {
    if (buffer->line_count == 0) {
        buffer->line_num_width = 3;
        return;
    }
    buffer->line_num_width = (int)floor(log10(buffer->line_count)) + 3;
}

char *buffer_get_content(Buffer *b) {
    size_t total_len = 0;
    for (int i = 0; i < b->line_count; i++) {
        total_len += b->lines[i]->byte_count;
    }
    if (b->line_count > 0) {
        total_len += b->line_count - 1; // for newlines
    }

    char *content = malloc(total_len + 1);
    if (!content) {
        log_error("buffer.buffer_get_content: malloc failed");
        return NULL;
    }

    char *p = content;
    for (int i = 0; i < b->line_count; i++) {
        BufferLine *line = b->lines[i];
        memcpy(p, line->chars, line->byte_count);
        p += line->byte_count;
        if (i < b->line_count - 1) {
            *p++ = '\n';
        }
    }
    *p = '\0';
    return content;
}

int buffer_find_last_match_before(Buffer *b, const char *term, int start_y, int start_x, int *match_y, int *match_x) {
    if (!term || term[0] == '\0') {
        return 0;
    }

    for (int i = b->search_state.count - 1; i >= 0; i--) {
        int y = b->search_state.matches[i].y;
        int x = b->search_state.matches[i].x;
        if (y < start_y || (y == start_y && x < start_x)) {
            *match_y = y;
            *match_x = x;
            return 1;
        }
    }

    if (b->search_state.count > 0) {
        *match_y = b->search_state.matches[b->search_state.count - 1].y;
        *match_x = b->search_state.matches[b->search_state.count - 1].x;
        return 1;
    }

    return 0;
}

int buffer_find_first_match(Buffer *b, const char *term, int start_y, int start_x, int *match_y, int *match_x) {
    if (!term || term[0] == '\0') {
        return 0;
    }

    for (int i = 0; i < b->search_state.count; i++) {
        int y = b->search_state.matches[i].y;
        int x = b->search_state.matches[i].x;
        if (y > start_y || (y == start_y && x >= start_x)) {
            *match_y = y;
            *match_x = x;
            return 1;
        }
    }

    if (b->search_state.count > 0) {
        *match_y = b->search_state.matches[0].y;
        *match_x = b->search_state.matches[0].x;
        return 1;
    }

    return 0;
}

void buffer_clear_search_state(Buffer *b) {
    if (b->search_state.term) {
        free(b->search_state.term);
        b->search_state.term = NULL;
    }
    if (b->search_state.matches) {
        free(b->search_state.matches);
        b->search_state.matches = NULL;
    }
    b->search_state.count = 0;
    b->search_state.current = -1;
}

void buffer_update_search_matches(Buffer *b, const char *term) {
    buffer_clear_search_state(b);
    if (!term || term[0] == '\0') {
        return;
    }
    b->search_state.term = strdup(term);

    int capacity = 10;
    b->search_state.matches = malloc(capacity * sizeof(*b->search_state.matches));

    for (int i = 0; i < b->line_count; i++) {
        BufferLine *line = b->lines[i];
        char *line_str = line->chars;
        if (!line_str) continue;

        char *match = strstr(line_str, term);
        while (match) {
            if (b->search_state.count == capacity) {
                capacity *= 2;
                b->search_state.matches = realloc(b->search_state.matches, capacity * sizeof(*b->search_state.matches));
            }

            int match_byte_pos = match - line_str;
            int match_char_pos = utf8_char_count(line_str, match_byte_pos);

            b->search_state.matches[b->search_state.count].y = i;
            b->search_state.matches[b->search_state.count].x = match_char_pos;
            b->search_state.count++;

            match = strstr(match + 1, term);
        }
    }

    buffer_update_current_search_match(b);
}

void buffer_update_current_search_match(Buffer *b) {
    if (b->search_state.count == 0 || !b->search_state.term) {
        b->search_state.current = -1;
        return;
    }

    size_t term_len = utf8_strlen(b->search_state.term);
    for (int i = 0; i < b->search_state.count; i++) {
        if (b->search_state.matches[i].y == b->position_y &&
            b->position_x >= b->search_state.matches[i].x &&
            b->position_x < b->search_state.matches[i].x + (int)term_len) {
            b->search_state.current = i;
            return;
        }
    }

    b->search_state.current = -1;
}

const char *buffer_read(void *payload, uint32_t start_byte __attribute__((unused)), TSPoint position, uint32_t *bytes_read) {
    Buffer *buffer = (Buffer *)payload;
    if ((int)position.row >= buffer->line_count) {
        *bytes_read = 0;
        return "";
    }
    BufferLine *line = buffer->lines[position.row];

    if (line->byte_count + 2 > (int)buffer->read_buffer_capacity) {
        size_t new_capacity = line->byte_count + 2;
        char *new_buffer = realloc(buffer->read_buffer, new_capacity);
        if (!new_buffer) {
            log_error("buffer.buffer_read: realloc read buffer failed");
            exit(1);
        }
        buffer->read_buffer = new_buffer;
        buffer->read_buffer_capacity = new_capacity;
    }

    memcpy(buffer->read_buffer, line->chars, line->byte_count);
    buffer->read_buffer[line->byte_count] = '\n';
    buffer->read_buffer[line->byte_count + 1] = '\0';

    if (position.column > (uint32_t)line->byte_count) {
        *bytes_read = 0;
        return "";
    }

    *bytes_read = line->byte_count - position.column + 1;
    return buffer->read_buffer + position.column;
}


void buffer_parse(Buffer *b) {
    TSTree *old_tree = b->tree;
    TSInput ts_input = {
        .read = buffer_read,
        .encoding = TSInputEncodingUTF8,
        .payload = b,
    };
    if (b->parser) {
        b->tree = ts_parser_parse(b->parser, old_tree, ts_input);
        b->root = ts_tree_root_node(b->tree);
    }
    if (old_tree) {
        uint32_t range_count;
        TSRange *changed_ranges = ts_tree_get_changed_ranges(old_tree, b->tree, &range_count);
        for (uint32_t i = 0; i < range_count; i++) {
            int start_line = (int)changed_ranges[i].start_point.row;
            int end_line = (int)changed_ranges[i].end_point.row;
            for (int j = start_line; j <= end_line && j < b->line_count; j++) {
                b->lines[j]->needs_highlight = 1;
            }
        }
        free(changed_ranges);
        ts_tree_delete(old_tree);
    }
    b->needs_parse = 0;
}

int buffer_get_visual_position_x(Buffer *buffer) {
    BufferLine *line = buffer->lines[buffer->position_y];
    int x = buffer->line_num_width + 1;
    char *ptr = line->chars;
    for (int i = 0; i < buffer->position_x; i++) {
        if (*ptr == '\t') {
            x += buffer->tab_width;
            ptr++;
        } else {
            x += utf8_char_width(ptr);
            ptr += utf8_char_len(ptr);
        }
    }
    return x - buffer->offset_x;
}

void buffer_line_realloc_for_capacity(BufferLine *line, int new_needed_capacity) {
    if (new_needed_capacity <= line->capacity) {
        return;
    }
    int new_capacity = line->capacity;
    while (new_capacity < new_needed_capacity) {
        new_capacity *= 2;
    }
    char *new_chars = realloc(line->chars, new_capacity);
    if (new_chars == NULL) {
        log_error("buffer.buffer_line_realloc_for_capacity: failed to reallocate line characters");
        exit(1);
    }
    line->chars = new_chars;
    line->capacity = new_capacity;
}

void buffer_realloc_lines_for_capacity(Buffer *buffer) {
    if (buffer->line_count + 1 <= buffer->capacity) {
        return;
    }
    int new_capacity = buffer->capacity;
    while (new_capacity < buffer->line_count + 1) {
        new_capacity *= 2;
    }
    BufferLine **new_lines = realloc(buffer->lines, new_capacity * sizeof(BufferLine *));
    if (new_lines == NULL) {
        log_error("buffer.realloc_buffer_lines_for_capacity: failed to reallocate lines");
        exit(1);
    }
    buffer->lines = new_lines;
    buffer->capacity = new_capacity;
}

void buffer_reset_offset_y(Buffer *buffer, int screen_rows) {
    if (buffer->offset_y < 0) buffer->offset_y = 0;
    if (buffer->position_y - buffer->offset_y > screen_rows - 7) {
        buffer->offset_y = buffer->position_y - screen_rows + 7;
    } else if (buffer->position_y - buffer->offset_y < 5 && buffer->offset_y) {
        buffer->offset_y = buffer->position_y - 5;
        if (buffer->offset_y < 0) buffer->offset_y = 0;
    }
}

void buffer_reset_offset_x(Buffer *buffer, int screen_cols) {
    int visual_x = buffer_get_visual_position_x(buffer) + buffer->offset_x;
    if (visual_x <= screen_cols - 5) {
        buffer->offset_x = 0;
    } else {
        buffer->offset_x = visual_x - screen_cols + 5;
    }
}

void buffer_set_logical_position_x(Buffer *buffer, int visual_before) {
    BufferLine *line = buffer->lines[buffer->position_y];
    if (buffer->position_x > line->char_count) {
        buffer->position_x = line->char_count;
    }
    int best_x = buffer->position_x;
    int best_diff = abs(visual_before - (buffer->line_num_width + 1));
    int current_visual = buffer->line_num_width + 1;
    char *ptr = line->chars;
    for (int i = 0; i < line->char_count; i++) {
        int diff = abs(visual_before - current_visual);
        if (diff < best_diff) {
            best_diff = diff;
            best_x = i;
        }
        if (*ptr == '\t') {
            current_visual += buffer->tab_width;
            ptr++;
        } else {
            current_visual += utf8_char_width(ptr);
            ptr += utf8_char_len(ptr);
        }
    }
    int diff = abs(visual_before - current_visual);
    if (diff < best_diff) {
        best_x = line->char_count;
    }
    buffer->position_x = best_x;
}

void buffer_line_init(BufferLine *line) {
    line->char_count = 0;
    line->byte_count = 0;
    line->needs_highlight = 1;
    line->capacity = 4;
    line->chars = (char *)malloc(line->capacity);
    if (line->chars == NULL) {
        log_error("buffer.buffer_line_init: failed to allocate BufferLine line");
        exit(1);
    }
    line->chars[0] = '\0';
}

void buffer_line_destroy(BufferLine *line) {
    free(line->chars);
}

char *get_file_extension(const char *file_name) {
    if (file_name == NULL) return NULL;
    char *dot = strrchr(file_name, '.');
    if (!dot || dot == file_name) return NULL;
    return dot + 1;
}

void buffer_init(Buffer *b, char *file_name) {
    b->diagnostics_version = 0;
    b->history = history_create();
    b->tab_width = 4;
    b->line_num_width = 3;
    if (file_name) {
        b->file_name = strdup(file_name);
    } else {
        b->file_name = NULL;
    }
    b->needs_draw = 1;
    b->dirty = 0;
    b->needs_parse = 1;
    b->capacity = 8;
    b->position_y = 0;
    b->read_buffer_capacity = 1024;
    b->read_buffer = malloc(b->read_buffer_capacity);
    b->position_x = 0;
    b->offset_y = 0;
    b->offset_x = 0;
    b->version = 1;
    b->search_state.term = NULL;
    b->search_state.matches = NULL;
    b->search_state.count = 0;
    b->search_state.current = -1;
    b->lines = (BufferLine **)malloc(sizeof(BufferLine *) * b->capacity);
    if (b->lines == NULL) {
        log_error("buffer.buffer_init: failed to allocate lines for buffer");
        exit(1);
    }
    b->lines[0] = (BufferLine *)malloc(sizeof(BufferLine));
    if (b->lines[0] == NULL) {
        log_error("buffer.buffer_init: failed to allocate first BufferLine");
        exit(1);
    }
    buffer_line_init(b->lines[0]);
    b->line_count = 1;
    b->query = NULL;
    b->cursor = NULL;
    b->parser = NULL;
    b->tree = NULL;
    b->mtime = 0;
    if (file_name == NULL) {
        return;
    }

    struct stat st;
    if (stat(file_name, &st) == 0) {
        b->mtime = st.st_mtime;
    }

    char *lang_name = get_file_extension(file_name);
    TSLanguage *lang = config_load_language(lang_name);
    if (lang) {
        b->parser = ts_parser_new();
        ts_parser_set_language(b->parser, lang);
        b->query = config_load_highlights(lang, lang_name);
        if (b->query) {
            b->cursor = ts_query_cursor_new();
            if (!b->cursor) {
                log_error("buffer.buffer_init: failed to create query cursor");
                exit(1);
            }
        }
    }

    FILE *fp = fopen(file_name, "r");
    if (fp) {
        char *line = NULL;
        size_t len = 0;
        ssize_t read;
        int first_line = 1;

        while ((read = getline(&line, &len, fp)) != -1) {
            if (first_line) {
                buffer_line_destroy(b->lines[0]);
                free(b->lines[0]);
                b->line_count = 0;
                first_line = 0;
            }
            if (read > 0 && line[read - 1] == '\n') {
                line[read - 1] = '\0';
                read--;
            }

            buffer_realloc_lines_for_capacity(b);
            b->lines[b->line_count] = (BufferLine *)malloc(sizeof(BufferLine));
            if (b->lines[b->line_count] == NULL) {
                log_error("buffer.buffer_init: failed to allocate BufferLine");
                exit(1);
            }
            buffer_line_init(b->lines[b->line_count]);
            BufferLine *bl = b->lines[b->line_count];
            buffer_line_realloc_for_capacity(bl, read + 1);
            memcpy(bl->chars, line, read);
            bl->chars[read] = '\0';
            bl->byte_count = read;
            bl->char_count = utf8_strlen(bl->chars);
            b->line_count++;
        }
        free(line);
        fclose(fp);
    } else {
        log_error("buffer.buffer_init: failed to open file");
    }

    if (b->parser) {
        buffer_parse(b);
    }
}

void buffer_destroy(Buffer *b) {
    for (int i = 0; i < b->line_count; i++) {
        buffer_line_destroy(b->lines[i]);
        free(b->lines[i]);
    }
    free(b->lines);
    if (b->file_name) {
        free(b->file_name);
    }
    if (b->cursor) {
        ts_query_cursor_delete(b->cursor);
    }
    if (b->query) {
        ts_query_delete(b->query);
    }
    if (b->parser) {
        ts_parser_delete(b->parser);
    }
    if (b->tree) {
        ts_tree_delete(b->tree);
    }
    if (b->history) {
        history_destroy(b->history);
    }
    if (b->read_buffer) {
        free(b->read_buffer);
    }
}

int is_line_empty(BufferLine *line) {
    if (line->char_count == 0) {
        return 1;
    }
    for (int i = 0; i < line->byte_count; i++) {
        if (line->chars[i] != ' ' && line->chars[i] != '\t') {
            return 0;
        }
    }
    return 1;
}

int buffer_get_visual_x_for_line_pos(Buffer *buffer, int y, int logical_x) {
    if (y >= buffer->line_count) return 0;
    BufferLine *line = buffer->lines[y];
    int x_pos = buffer->line_num_width + 1;
    char *ptr = line->chars;
    for (int i = 0; i < logical_x && i < line->char_count; i++) {
        if (*ptr == '\t') {
            x_pos += buffer->tab_width;
            ptr++;
        } else {
            x_pos += utf8_char_width(ptr);
            ptr += utf8_char_len(ptr);
        }
    }
    return x_pos;
}

int buffer_get_byte_position_x(Buffer *buffer) {
    BufferLine *line = buffer->lines[buffer->position_y];
    return utf8_char_to_byte_index(line->chars, buffer->position_x);
}

int buffer_find_forward(Buffer *b, const char *term, int *y, int *x) {
    int term_len = strlen(term);
    if (term_len == 0) return 0;

    int original_y = *y;
    int original_x = *x;

    for (int i = *y; i < b->line_count; i++) {
        BufferLine *line = b->lines[i];
        char *line_str = line->chars;
        if (!line_str) continue;

        int start_char_pos = (i == *y) ? *x + 1 : 0;
        if (start_char_pos >= line->char_count) {
            continue;
        }

        int start_byte_pos = utf8_char_to_byte_index(line_str, start_char_pos);

        if (start_byte_pos >= line->byte_count) {
            continue;
        }

        char* match = strstr(line_str + start_byte_pos, term);
        if (match) {
            int match_byte_pos = match - line_str;
            int match_char_pos = utf8_char_count(line_str, match_byte_pos);
            *y = i;
            *x = match_char_pos;
            return 1;
        }
    }

    // Wrap around
    for (int i = 0; i <= original_y; i++) {
        BufferLine *line = b->lines[i];
        char *line_str = line->chars;
        if (!line_str) continue;

        int end_char_pos = (i == original_y) ? original_x : line->char_count - 1;

        char* match = strstr(line_str, term);
        while (match) {
            int match_byte_pos = match - line_str;
            int match_char_pos = utf8_char_count(line_str, match_byte_pos);

            if (i < original_y || match_char_pos <= end_char_pos) {
                *y = i;
                *x = match_char_pos;
                return 1;
            }
            match = strstr(match + 1, term);
        }
    }

    return 0;
}

int buffer_find_backward(Buffer *b, const char *term, int *y, int *x) {
    int term_len = strlen(term);
    if (term_len == 0) return 0;

    int original_y = *y;
    int original_x = *x;

    for (int i = *y; i >= 0; i--) {
        BufferLine *line = b->lines[i];
        if (!line) continue;
        char *line_str = line->chars;
        if (!line_str) continue;

        int start_char_pos = (i == *y) ? *x - 1 : line->char_count - 1;

        for (int j = start_char_pos; j >= 0; j--) {
            int start_byte_pos = utf8_char_to_byte_index(line_str, j);
            if (strncmp(line_str + start_byte_pos, term, term_len) == 0) {
                *y = i;
                *x = j;
                return 1;
            }
        }
    }

    // Wrap around
    for (int i = b->line_count - 1; i >= original_y; i--) {
        BufferLine *line = b->lines[i];
        if (!line) continue;
        char *line_str = line->chars;
        if (!line_str) continue;

        int start_char_pos = (i == original_y) ? original_x : line->char_count - 1;

        for (int j = start_char_pos; j >= 0; j--) {
            int start_byte_pos = utf8_char_to_byte_index(line_str, j);
            if (strncmp(line_str + start_byte_pos, term, term_len) == 0) {
                if (i > original_y || j < original_x) {
                    *y = i;
                    *x = j;
                    return 1;
                }
            }
        }
    }

    return 0;
}
