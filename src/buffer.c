#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <math.h>
#include "buffer.h"
#include "log.h"
#include "config.h"
#include "theme.h"
#include "utf8.h"

static char* line_to_string(BufferLine *line);

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
        BufferLine *line = b->lines[i];
        for (int j = 0; j < line->char_count; j++) {
            total_len += strlen(line->chars[j].value);
        }
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
        for (int j = 0; j < line->char_count; j++) {
            const char *val = line->chars[j].value;
            size_t len = strlen(val);
            memcpy(p, val, len);
            p += len;
        }
        if (i < b->line_count - 1) {
            *p++ = '\n';
        }
    }
    *p = '\0';
    return content;
}


// Structure to store capture information with priority
typedef struct {
    uint32_t start_byte;
    uint32_t end_byte;
    const char *capture_name;
    int priority;
} HighlightCapture;

const char* find_best_capture_for_position(HighlightCapture *captures, uint32_t capture_count, uint32_t byte_pos) {
    const char *best_capture = NULL;
    int best_priority = -1;
    
    for (uint32_t i = 0; i < capture_count; i++) {
        HighlightCapture *cap = &captures[i];
        if (byte_pos >= cap->start_byte && byte_pos < cap->end_byte) {
            if (cap->priority > best_priority) {
                best_priority = cap->priority;
                best_capture = cap->capture_name;
            }
        }
        // Since captures are sorted by start_byte, we can break early
        if (cap->start_byte > byte_pos) {
            break;
        }
    }
    
    return best_capture;
}

int get_capture_priority(const char* capture_name) {
    const CaptureInfo* info = theme_get_capture_info(capture_name);
    if (info) {
        return info->priority;
    }
    
    if (capture_name) {
        log_warning("buffer.get_capture_priority: unrecognized capture_name %s", capture_name);
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
        char *line_str = line_to_string(line);
        if (!line_str) continue;

        char *match = strstr(line_str, term);
        while (match) {
            if (b->search_state.count == capacity) {
                capacity *= 2;
                b->search_state.matches = realloc(b->search_state.matches, capacity * sizeof(*b->search_state.matches));
            }

            int match_byte_pos = match - line_str;
            int match_char_pos = 0;
            int current_byte_pos = 0;
            for (int k = 0; k < line->char_count; k++) {
                if (current_byte_pos == match_byte_pos) {
                    match_char_pos = k;
                    break;
                }
                current_byte_pos += strlen(line->chars[k].value);
            }

            b->search_state.matches[b->search_state.count].y = i;
            b->search_state.matches[b->search_state.count].x = match_char_pos;
            b->search_state.count++;

            match = strstr(match + 1, term);
        }
        free(line_str);
    }

    buffer_update_current_search_match(b);
}

void buffer_update_current_search_match(Buffer *b) {
    if (b->search_state.count == 0 || !b->search_state.term) {
        b->search_state.current = -1;
        return;
    }

    size_t term_len = strlen(b->search_state.term);
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

void buffer_line_apply_syntax_highlighting(Buffer *b, BufferLine *line, uint32_t start_byte, Theme *theme) {
    if (!b->cursor) {
        for (int char_idx = 0; char_idx < line->char_count; char_idx++) {
            Char *ch = &line->chars[char_idx];
            Style *style = &theme->syntax_variable;
            ch->r = style->fg_r;
            ch->g = style->fg_g;
            ch->b = style->fg_b;
            ch->style = style->style;
        }
        return;
    }

    size_t line_byte_len = 0;
    for (int i = 0; i < line->char_count; i++) {
        line_byte_len += strlen(line->chars[i].value);
    }

    ts_query_cursor_set_byte_range(b->cursor, start_byte, start_byte + line_byte_len);
    ts_query_cursor_exec(b->cursor, b->query, b->root);

    TSQueryMatch match;
    uint32_t capture_index;
    HighlightCapture *captures = NULL;
    uint32_t capture_count = 0;
    uint32_t capture_capacity = 0;

    while (ts_query_cursor_next_capture(b->cursor, &match, &capture_index)) {
        TSQueryCapture capture = match.captures[capture_index];
        TSNode node = capture.node;
        uint32_t start = ts_node_start_byte(node);
        uint32_t end = ts_node_end_byte(node);

        if (capture_count >= capture_capacity) {
            capture_capacity = capture_capacity ? capture_capacity * 2 : 16;
            captures = realloc(captures, capture_capacity * sizeof(HighlightCapture));
            if (!captures) {
                log_error("buffer.reset_offset_y: failed to allocate captures array");
                exit(1);
            }
        }
        uint32_t capture_name_length;
        const char *capture_name = ts_query_capture_name_for_id(b->query, capture.index, &capture_name_length);
        captures[capture_count].start_byte = start;
        captures[capture_count].end_byte = end;
        captures[capture_count].capture_name = capture_name;
        captures[capture_count].priority = get_capture_priority(capture_name);
        capture_count++;
    }

    uint32_t current_byte = start_byte;
    for (int char_idx = 0; char_idx < line->char_count; char_idx++) {
        const char *best_capture = find_best_capture_for_position(captures, capture_count, current_byte);
        Char *ch = &line->chars[char_idx];
        Style *style = theme_get_capture_style(best_capture, theme);
        ch->r = style->fg_r;
        ch->g = style->fg_g;
        ch->b = style->fg_b;
        ch->style = style->style;
        current_byte += strlen(ch->value);
    }

    if (captures) {
        free(captures);
    }
    line->needs_highlight = 0;
}

const char *buffer_read(void *payload, uint32_t start_byte __attribute__((unused)), TSPoint position, uint32_t *bytes_read) {
    Buffer *buffer = (Buffer *)payload;
    if ((int)position.row >= buffer->line_count) {
        *bytes_read = 0;
        return "";
    }
    BufferLine *line = buffer->lines[position.row];

    size_t line_byte_len = 0;
    for (int i = 0; i < line->char_count; i++) {
        line_byte_len += strlen(line->chars[i].value);
    }

    if (line_byte_len + 2 > buffer->read_buffer_capacity) {
        size_t new_capacity = line_byte_len + 2;
        char *new_buffer = realloc(buffer->read_buffer, new_capacity);
        if (!new_buffer) {
            log_error("buffer.buffer_read: realloc read buffer failed");
            exit(1);
        }
        buffer->read_buffer = new_buffer;
        buffer->read_buffer_capacity = new_capacity;
    }

    char *p = buffer->read_buffer;
    for (int i = 0; i < line->char_count; i++) {
        const char *val = line->chars[i].value;
        size_t len = strlen(val);
        memcpy(p, val, len);
        p += len;
    }
    *p = '\n';
    *(p + 1) = '\0';

    if (position.column > line_byte_len) {
        *bytes_read = 0;
        return "";
    }

    *bytes_read = line_byte_len - position.column + 1;
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
    for (int i = 0; i < buffer->position_x; i++) {
        if (strcmp(line->chars[i].value, "\t") == 0) {
            x += buffer->tab_width;
        } else {
            x += line->chars[i].width;
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
    Char *new_chars = realloc(line->chars, new_capacity * sizeof(Char));
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

void buffer_move_position_left(Buffer *buffer) {
    buffer->position_x--;
    int visual_x = buffer_get_visual_position_x(buffer);
    if (visual_x - buffer->line_num_width < 6 && buffer->offset_x) {
        buffer->offset_x--;
    }
}

void buffer_move_position_right(Buffer *buffer, int screen_cols) {
    buffer->position_x++;
    int visual_x = buffer_get_visual_position_x(buffer);
    if (visual_x > screen_cols - 5) {
        buffer->offset_x++;
    }
}

void buffer_reset_offset_x(Buffer *buffer, int screen_cols) {
    int visual_x = buffer_get_visual_position_x(buffer) + buffer->offset_x;
    if (visual_x <= screen_cols) {
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
    int best_diff = abs(visual_before - 0);
    for (int i = 0; i <= line->char_count; i++) {
        buffer->position_x = i;
        int current_visual = buffer_get_visual_position_x(buffer);
        int diff = visual_before - current_visual;
        if (diff < 0) continue;
        if (diff < best_diff) {
            best_diff = diff;
            best_x = i;
        }
    }
    buffer->position_x = best_x;
}

void buffer_line_init(BufferLine *line) {
    line->char_count = 0;
    line->needs_highlight = 1;
    line->capacity = 4;
    line->chars = (Char *)malloc(sizeof(Char) * line->capacity);
    if (line->chars == NULL) {
        log_error("buffer.buffer_line_init: failed to allocate BufferLine line");
        exit(1);
    }
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
    if (file_name == NULL) {
        return;
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
        char utf8_buf[5];
        int bytes_read;
        while ((bytes_read = read_utf8_char(fp, utf8_buf, sizeof(utf8_buf))) > 0) {
            if (strcmp(utf8_buf, "\n") == 0) {
                buffer_realloc_lines_for_capacity(b);
                b->lines[b->line_count] = (BufferLine *)malloc(sizeof(BufferLine));
                if (b->lines[b->line_count] == NULL) {
                    log_error("buffer.buffer_init: failed to allocate BufferLine");
                    exit(1);
                }
                buffer_line_init(b->lines[b->line_count]);
                b->line_count++;
            } else {
                BufferLine *line = b->lines[b->line_count - 1];
                buffer_line_realloc_for_capacity(line, line->char_count + 1);
                Char new_char = { .style = 0 };
                strncpy(new_char.value, utf8_buf, sizeof(new_char.value));
                new_char.width = utf8_char_width(utf8_buf);
                line->chars[line->char_count] = new_char;
                line->char_count++;
            }
        }
        if (bytes_read == -1) {
            log_error("buffer.buffer_init: error reading UTF-8 character from file");
        }
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
    free(b->read_buffer);
}

int is_line_empty(BufferLine *line) {
    if (line->char_count == 0) {
        return 1;
    }
    for (int i = 0; i < line->char_count; i++) {
        if (strcmp(line->chars[i].value, " ") != 0 && strcmp(line->chars[i].value, "\t") != 0) {
            return 0;
        }
    }
    return 1;
}

int buffer_get_visual_x_for_line_pos(Buffer *buffer, int y, int logical_x) {
    if (y >= buffer->line_count) return 0;
    BufferLine *line = buffer->lines[y];
    int x_pos = buffer->line_num_width + 1;
    for (int i = 0; i < logical_x && i < line->char_count; i++) {
        if (strcmp(line->chars[i].value, "\t") == 0) {
            x_pos += buffer->tab_width;
        } else {
            x_pos += line->chars[i].width;
        }
    }
    return x_pos;
}

int buffer_get_byte_position_x(Buffer *buffer) {
    BufferLine *line = buffer->lines[buffer->position_y];
    int byte_pos = 0;
    for (int i = 0; i < buffer->position_x; i++) {
        byte_pos += strlen(line->chars[i].value);
    }
    return byte_pos;
}

static char* line_to_string(BufferLine *line) {
    int len = 0;
    for (int i = 0; i < line->char_count; i++) {
        len += strlen(line->chars[i].value);
    }
    char *str = malloc(len + 1);
    if (!str) return NULL;

    char *ptr = str;
    for (int i = 0; i < line->char_count; i++) {
        int char_len = strlen(line->chars[i].value);
        memcpy(ptr, line->chars[i].value, char_len);
        ptr += char_len;
    }
    *ptr = '\0';
    return str;
}

int buffer_find_forward(Buffer *b, const char *term, int *y, int *x) {
    int term_len = strlen(term);
    if (term_len == 0) return 0;

    int original_y = *y;
    int original_x = *x;

    for (int i = *y; i < b->line_count; i++) {
        BufferLine *line = b->lines[i];
        char *line_str = line_to_string(line);
        if (!line_str) continue;

        int start_char_pos = (i == *y) ? *x + 1 : 0;
        if (start_char_pos >= line->char_count) {
            free(line_str);
            continue;
        }

        int start_byte_pos = 0;
        for(int k=0; k<start_char_pos; k++) {
            start_byte_pos += strlen(line->chars[k].value);
        }

        if (start_byte_pos >= (int)strlen(line_str)) {
            free(line_str);
            continue;
        }

        char* match = strstr(line_str + start_byte_pos, term);
        if (match) {
            int match_byte_pos = match - line_str;
            int match_char_pos = 0;
            int current_byte_pos = 0;
            for (int k = 0; k < line->char_count; k++) {
                if (current_byte_pos == match_byte_pos) {
                    match_char_pos = k;
                    break;
                }
                current_byte_pos += strlen(line->chars[k].value);
            }
            *y = i;
            *x = match_char_pos;
            free(line_str);
            return 1;
        }
        free(line_str);
    }

    // Wrap around
    for (int i = 0; i <= original_y; i++) {
        BufferLine *line = b->lines[i];
        char *line_str = line_to_string(line);
        if (!line_str) continue;

        int end_char_pos = (i == original_y) ? original_x : line->char_count - 1;

        char* match = strstr(line_str, term);
        while (match) {
            int match_byte_pos = match - line_str;
            int match_char_pos = 0;
            int current_byte_pos = 0;
            for (int k = 0; k < line->char_count; k++) {
                if (current_byte_pos == match_byte_pos) {
                    match_char_pos = k;
                    break;
                }
                current_byte_pos += strlen(line->chars[k].value);
            }

            if (i < original_y || match_char_pos <= end_char_pos) {
                *y = i;
                *x = match_char_pos;
                free(line_str);
                return 1;
            }
            match = strstr(match + 1, term);
        }
        free(line_str);
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
        char *line_str = line_to_string(line);
        if (!line_str) continue;

        int start_char_pos = (i == *y) ? *x - 1 : line->char_count - 1;

        for (int j = start_char_pos; j >= 0; j--) {
            int start_byte_pos = 0;
            for(int k=0; k<j; k++) {
                start_byte_pos += strlen(line->chars[k].value);
            }

            if (strncmp(line_str + start_byte_pos, term, term_len) == 0) {
                *y = i;
                *x = j;
                free(line_str);
                return 1;
            }
        }
        free(line_str);
    }

    // Wrap around
    for (int i = b->line_count - 1; i >= original_y; i--) {
        BufferLine *line = b->lines[i];
        if (!line) continue;
        char *line_str = line_to_string(line);
        if (!line_str) continue;

        int start_char_pos = (i == original_y) ? original_x : line->char_count - 1;

        for (int j = start_char_pos; j >= 0; j--) {
            int start_byte_pos = 0;
            for(int k=0; k<j; k++) {
                start_byte_pos += strlen(line->chars[k].value);
            }

            if (strncmp(line_str + start_byte_pos, term, term_len) == 0) {
                if (i > original_y || j < original_x) {
                    *y = i;
                    *x = j;
                    free(line_str);
                    return 1;
                }
            }
        }
        free(line_str);
    }

    return 0;
}
