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

static inline int utf8_char_len(const char *s) {
    unsigned char c = (unsigned char)*s;
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1; // Fallback for invalid char
}

void buffer_line_ensure_capacity(BufferLine *line, int new_char_capacity, int new_byte_capacity) {
    if (new_char_capacity > line->char_capacity) {
        int old_capacity = line->char_capacity;
        line->char_capacity = new_char_capacity > old_capacity * 2 ? new_char_capacity : old_capacity * 2;
        if (line->char_capacity == 0) line->char_capacity = 4;
        line->styles = realloc(line->styles, sizeof(CharStyle) * line->char_capacity);
        if (!line->styles) {
            log_error("buffer.buffer_line_ensure_capacity: realloc styles failed");
            exit(1);
        }
    }
    if (new_byte_capacity + 1 > line->byte_capacity) {
        int old_capacity = line->byte_capacity;
        line->byte_capacity = new_byte_capacity + 1 > old_capacity * 2 ? new_byte_capacity + 1 : old_capacity * 2;
        if (line->byte_capacity == 0) line->byte_capacity = 8;
        line->text = realloc(line->text, line->byte_capacity);
        if (!line->text) {
            log_error("buffer.buffer_line_ensure_capacity: realloc text failed");
            exit(1);
        }
    }
}

static void buffer_line_append_char(BufferLine *line, const char *utf8_char, int char_len) {
    buffer_line_ensure_capacity(line, line->char_count + 1, line->byte_count + char_len);
    memcpy(line->text + line->byte_count, utf8_char, char_len);
    line->byte_count += char_len;
    line->text[line->byte_count] = '\0';
    line->styles[line->char_count] = (CharStyle){ .r=255, .g=255, .b=255, .style=0 };
    line->char_count++;
}

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
        memcpy(p, line->text, line->byte_count);
        p += line->byte_count;
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
        char *line_str = line_to_string(line);
        if (!line_str) continue;

        char *match = strstr(line_str, term);
        while (match) {
            if (b->search_state.count == capacity) {
                capacity *= 2;
                b->search_state.matches = realloc(b->search_state.matches, capacity * sizeof(*b->search_state.matches));
            }

            char *p = line_str;
            int match_char_pos = 0;
            while (p < match) {
                p += utf8_char_len(p);
                match_char_pos++;
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

void buffer_line_apply_syntax_highlighting(Buffer *b, BufferLine *line, uint32_t start_byte, Theme *theme) {
    if (!b->cursor) {
        for (int char_idx = 0; char_idx < line->char_count; char_idx++) {
            Style *style = &theme->syntax_variable;
            line->styles[char_idx].r = style->fg_r;
            line->styles[char_idx].g = style->fg_g;
            line->styles[char_idx].b = style->fg_b;
            line->styles[char_idx].style = style->style;
        }
        return;
    }

    ts_query_cursor_set_byte_range(b->cursor, start_byte, start_byte + line->byte_count);
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

    uint32_t current_byte_in_line = 0;
    int char_idx = 0;
    char *p = line->text;
    char *end = line->text + line->byte_count;
    while (p < end) {
        const char *best_capture = find_best_capture_for_position(captures, capture_count, start_byte + current_byte_in_line);
        Style *style = theme_get_capture_style(best_capture, theme);
        line->styles[char_idx].r = style->fg_r;
        line->styles[char_idx].g = style->fg_g;
        line->styles[char_idx].b = style->fg_b;
        line->styles[char_idx].style = style->style;

        int len = utf8_char_len(p);
        p += len;
        current_byte_in_line += len;
        char_idx++;
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

    if (line->byte_count + 2 > buffer->read_buffer_capacity) {
        size_t new_capacity = line->byte_count + 2;
        char *new_buffer = realloc(buffer->read_buffer, new_capacity);
        if (!new_buffer) {
            log_error("buffer.buffer_read: realloc read buffer failed");
            exit(1);
        }
        buffer->read_buffer = new_buffer;
        buffer->read_buffer_capacity = new_capacity;
    }

    memcpy(buffer->read_buffer, line->text, line->byte_count);
    buffer->read_buffer[line->byte_count] = '\n';
    buffer->read_buffer[line->byte_count + 1] = '\0';

    if (position.column > line->byte_count) {
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
    int char_idx = 0;
    char *p = line->text;
    char *end = line->text + line->byte_count;

    while (p < end && char_idx < buffer->position_x) {
        if (*p == '\t') {
            x += buffer->tab_width;
            p++;
        } else {
            x += utf8_char_width(p);
            p += utf8_char_len(p);
        }
        char_idx++;
    }
    return x - buffer->offset_x;
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

    int best_x = 0;
    int best_diff = visual_before - (buffer->line_num_width + 1);
    if (best_diff < 0) best_diff = -best_diff;

    int current_visual = buffer->line_num_width + 1;
    int char_idx = 0;
    char *p = line->text;
    char *end = line->text + line->byte_count;

    while (p < end) {
        if (*p == '\t') {
            current_visual += buffer->tab_width;
            p++;
        } else {
            current_visual += utf8_char_width(p);
            p += utf8_char_len(p);
        }
        char_idx++;

        int diff = visual_before - current_visual;
        if (diff < 0) diff = -diff;

        if (diff < best_diff) {
            best_diff = diff;
            best_x = char_idx;
        }
    }

    if (buffer->position_x > line->char_count) {
        buffer->position_x = line->char_count;
    } else {
        buffer->position_x = best_x;
    }
}

void buffer_line_init(BufferLine *line) {
    line->char_count = 0;
    line->byte_count = 0;
    line->needs_highlight = 1;
    line->char_capacity = 0;
    line->byte_capacity = 0;
    line->text = NULL;
    line->styles = NULL;
}

void buffer_line_destroy(BufferLine *line) {
    if (line->text) {
        free(line->text);
    }
    if (line->styles) {
        free(line->styles);
    }
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
        char utf8_buf[5];
        int bytes_read;
        while ((bytes_read = read_utf8_char(fp, utf8_buf, sizeof(utf8_buf))) > 0) {
            utf8_buf[bytes_read] = '\0';
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
                buffer_line_append_char(line, utf8_buf, bytes_read);
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
    if (b->history) {
        history_destroy(b->history);
    }
    if (b->read_buffer) {
        free(b->read_buffer);
    }
}

int is_line_empty(BufferLine *line) {
    return line->char_count == 0;
}

int buffer_get_visual_x_for_line_pos(Buffer *buffer, int y, int logical_x) {
    if (y >= buffer->line_count) return 0;
    BufferLine *line = buffer->lines[y];
    int x_pos = buffer->line_num_width + 1;
    int char_idx = 0;
    char *p = line->text;
    char *end = line->text + line->byte_count;

    while (p < end && char_idx < logical_x) {
        if (*p == '\t') {
            x_pos += buffer->tab_width;
            p++;
        } else {
            x_pos += utf8_char_width(p);
            p += utf8_char_len(p);
        }
        char_idx++;
    }
    return x_pos;
}

int buffer_get_byte_position_x(Buffer *buffer) {
    BufferLine *line = buffer->lines[buffer->position_y];
    char *p = line->text;
    char *end = line->text + line->byte_count;
    int char_idx = 0;
    int byte_pos = 0;
    while (p < end && char_idx < buffer->position_x) {
        int len = utf8_char_len(p);
        p += len;
        byte_pos += len;
        char_idx++;
    }
    return byte_pos;
}

static char* line_to_string(BufferLine *line) {
    if (line->byte_count == 0) {
        char *s = malloc(1);
        if (s) s[0] = '\0';
        return s;
    }
    return strndup(line->text, line->byte_count);
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

        char *p = line_str;
        for(int k=0; k<start_char_pos; k++) {
            p += utf8_char_len(p);
        }

        char* match = strstr(p, term);
        if (match) {
            p = line_str;
            int match_char_pos = 0;
            while (p < match) {
                p += utf8_char_len(p);
                match_char_pos++;
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
            char *p = line_str;
            int match_char_pos = 0;
            while (p < match) {
                p += utf8_char_len(p);
                match_char_pos++;
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
            char *p = line_str;
            for(int k=0; k<j; k++) {
                p += utf8_char_len(p);
            }

            if (strncmp(p, term, term_len) == 0) {
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
            char *p = line_str;
            for(int k=0; k<j; k++) {
                p += utf8_char_len(p);
            }

            if (strncmp(p, term, term_len) == 0) {
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
