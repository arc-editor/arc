#include "str.h"
#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
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
#include "git.h"


void buffer_set_line_num_width(Buffer *buffer) {
    buffer->line_num_width = (int)floor(log10(buffer->line_count)) + 3;
}

char *buffer_get_content(Buffer *b) {
    size_t total_len = 0;
    for (int i = 0; i < b->line_count; i++) {
        total_len += b->lines[i]->text_len;
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
        memcpy(p, line->text, line->text_len);
        p += line->text_len;
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
        char *line_str = line->text;

        char *match = strstr(line_str, term);
        while (match) {
            if (b->search_state.count == capacity) {
                capacity *= 2;
                b->search_state.matches = realloc(b->search_state.matches, capacity * sizeof(*b->search_state.matches));
            }

            int match_byte_pos = match - line_str;
            int match_char_pos = 0;
            char *p = line_str;
            int current_byte_pos = 0;
            while(current_byte_pos < match_byte_pos) {
                int len = utf8_char_len(p);
                p += len;
                current_byte_pos += len;
                match_char_pos++;
            }

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

void buffer_line_apply_syntax_highlighting(Buffer *b, BufferLine *line, uint32_t start_byte, Theme *theme) {
    if (line->highlight_runs) {
        free(line->highlight_runs);
        line->highlight_runs = NULL;
    }
    line->highlight_runs_count = 0;
    line->highlight_runs_capacity = 1;
    line->highlight_runs = malloc(sizeof(HighlightRun) * line->highlight_runs_capacity);

    if (!b->cursor || !b->query) {
        if (line->char_count > 0) {
            line->highlight_runs[0].count = line->char_count;
            line->highlight_runs[0].style = theme->syntax_variable;
            line->highlight_runs_count = 1;
        }
        return;
    }

    ts_query_cursor_set_byte_range(b->cursor, start_byte, start_byte + line->text_len);
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
                log_error("buffer.buffer_line_apply_syntax_highlighting: failed to allocate captures array");
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

    if (line->char_count == 0) {
        if (captures) free(captures);
        return;
    }

    uint32_t current_byte = start_byte;
    char *p = line->text;
    Style *last_style = NULL;

    for (int char_idx = 0; char_idx < line->char_count; char_idx++) {
        const char *best_capture = find_best_capture_for_position(captures, capture_count, current_byte);
        Style *current_style = theme_get_capture_style(best_capture, theme);

        if (char_idx == 0) {
            line->highlight_runs[0].count = 1;
            line->highlight_runs[0].style = *current_style;
            line->highlight_runs_count = 1;
        } else {
            if (current_style->fg_r == last_style->fg_r &&
                current_style->fg_g == last_style->fg_g &&
                current_style->fg_b == last_style->fg_b &&
                current_style->bg_r == last_style->bg_r &&
                current_style->bg_g == last_style->bg_g &&
                current_style->bg_b == last_style->bg_b &&
                current_style->style == last_style->style) {
                line->highlight_runs[line->highlight_runs_count - 1].count++;
            } else {
                if (line->highlight_runs_count == line->highlight_runs_capacity) {
                    line->highlight_runs_capacity *= 2;
                    line->highlight_runs = realloc(line->highlight_runs, sizeof(HighlightRun) * line->highlight_runs_capacity);
                }
                line->highlight_runs[line->highlight_runs_count].count = 1;
                line->highlight_runs[line->highlight_runs_count].style = *current_style;
                line->highlight_runs_count++;
            }
        }
        last_style = current_style;
        int len = utf8_char_len(p);
        current_byte += len;
        p += len;
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

    if (line->text_len + 2 > buffer->read_buffer_capacity) {
        int new_capacity = line->text_len + 2;
        char *new_buffer = realloc(buffer->read_buffer, new_capacity);
        if (!new_buffer) {
            log_error("buffer.buffer_read: realloc read buffer failed");
            exit(1);
        }
        buffer->read_buffer = new_buffer;
        buffer->read_buffer_capacity = new_capacity;
    }

    memcpy(buffer->read_buffer, line->text, line->text_len);
    buffer->read_buffer[line->text_len] = '\n';
    buffer->read_buffer[line->text_len + 1] = '\0';

    if (position.column > (uint32_t)line->text_len) {
        *bytes_read = 0;
        return "";
    }

    *bytes_read = line->text_len - position.column + 1;
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
    int x = buffer->line_num_width + 3 + 1; // gutter width

    char *p = line->text;
    int char_idx = 0;
    while (*p && char_idx < buffer->position_x) {
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

void buffer_line_realloc_for_capacity(BufferLine *line, int new_needed_capacity) {
    if (new_needed_capacity <= line->capacity) {
        return;
    }
    int new_capacity = line->capacity;
    while (new_capacity < new_needed_capacity) {
        new_capacity *= 2;
    }
    char *new_text = realloc(line->text, new_capacity);
    if (new_text == NULL) {
        log_error("buffer.buffer_line_realloc_for_capacity: failed to reallocate line text");
        exit(1);
    }
    line->text = new_text;
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

void buffer_line_init_without_text(BufferLine *line) {
    line->char_count = 0;
    line->text_len = 0;
    line->capacity = 8;
    line->needs_highlight = 1;
    line->highlight_runs = NULL;
    line->highlight_runs_count = 0;
    line->highlight_runs_capacity = 0;
}

void buffer_line_init(BufferLine *line) {
    buffer_line_init_without_text(line);
    line->text = malloc(line->capacity);
    if (line->text == NULL) {
        log_error("buffer.buffer_line_init: failed to allocate line text");
        exit(1);
    }
    line->text[0] = '\0';
}

void buffer_line_destroy(BufferLine *line) {
    free(line->text);
    if (line->highlight_runs) {
        free(line->highlight_runs);
    }
}

void buffer_update_git_diff(Buffer *b) {
    git_update_diff(b);
}

void buffer_init(Buffer *b, const char *file_name) {
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
    b->hunks = NULL;
    b->hunk_count = 0;
    if (file_name == NULL) {
        return;
    }
    buffer_update_git_diff(b);

    struct stat st;
    if (stat(file_name, &st) == 0) {
        b->mtime = st.st_mtime;
    }

    const char *lang_name = str_get_lang_name_from_file_name(file_name);
    if (lang_name) {
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
    }

    FILE *fp = fopen(file_name, "r");
    if (fp) {
        char *line_buf = NULL;
        size_t line_cap = 0;
        ssize_t line_len;
        int trailing_newline = 0;

        // Read the first line, if it exists
        if ((line_len = getline(&line_buf, &line_cap, fp)) != -1) {
            trailing_newline = (line_buf[line_len - 1] == '\n');
            // Overwrite the initial empty line
            BufferLine *first_line = b->lines[0];
            free(first_line->text); // free the initial empty string buffer

            // Strip newline characters
            if (line_len > 0 && line_buf[line_len - 1] == '\n') line_buf[--line_len] = '\0';
            if (line_len > 0 && line_buf[line_len - 1] == '\r') line_buf[--line_len] = '\0';

            first_line->text = malloc(line_len + 1);
            memcpy(first_line->text, line_buf, line_len + 1);
            first_line->text_len = line_len;
            first_line->capacity = line_len + 1;
            first_line->char_count = utf8_strlen(first_line->text);
            first_line->needs_highlight = 1;

            // Read subsequent lines
            while ((line_len = getline(&line_buf, &line_cap, fp)) != -1) {
                trailing_newline = (line_buf[line_len - 1] == '\n');
                if (line_len > 0 && line_buf[line_len - 1] == '\n') line_buf[--line_len] = '\0';
                if (line_len > 0 && line_buf[line_len - 1] == '\r') line_buf[--line_len] = '\0';

                buffer_realloc_lines_for_capacity(b);
                b->lines[b->line_count] = (BufferLine *)malloc(sizeof(BufferLine));
                buffer_line_init_without_text(b->lines[b->line_count]);
                BufferLine *new_line = b->lines[b->line_count];
                new_line->text = malloc(line_len + 1);
                if (new_line->text == NULL) {
                    log_error("buffer.buffer_init: unable to malloc line text");
                    exit(1);
                }
                memcpy(new_line->text, line_buf, line_len + 1);
                new_line->text_len = line_len;
                new_line->capacity = line_len + 1;
                new_line->char_count = utf8_strlen(new_line->text);

                b->line_count++;
            }
        }

        if (trailing_newline) {
            buffer_realloc_lines_for_capacity(b);
            b->lines[b->line_count] = (BufferLine *)malloc(sizeof(BufferLine));
            buffer_line_init(b->lines[b->line_count]);
            b->line_count++;
        }

        free(line_buf);
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
    if (b->hunks) {
        free(b->hunks);
    }
    free(b->read_buffer);
}

int is_line_empty(BufferLine *line) {
    if (line->text_len == 0) {
        return 1;
    }
    for (int i = 0; i < line->text_len; i++) {
        if (line->text[i] != ' ' && line->text[i] != '\t') {
            return 0;
        }
    }
    return 1;
}

int buffer_get_visual_x_for_line_pos(Buffer *buffer, int y, int logical_x) {
    if (y >= buffer->line_count) return 0;
    BufferLine *line = buffer->lines[y];
    int x_pos = buffer->line_num_width + 1;

    char *p = line->text;
    int char_idx = 0;
    while (*p && char_idx < logical_x) {
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

#include "utf8.h"

int buffer_get_byte_position_x(Buffer *buffer) {
    BufferLine *line = buffer->lines[buffer->position_y];
    int byte_pos = 0;
    char *p = line->text;
    int char_idx = 0;
    while (*p && char_idx < buffer->position_x) {
        int len = utf8_char_len(p);
        byte_pos += len;
        p += len;
        char_idx++;
    }
    return byte_pos;
}


int buffer_find_forward(Buffer *b, const char *term, int *y, int *x) {
    int term_len = strlen(term);
    if (term_len == 0) return 0;

    int original_y = *y;
    int original_x = *x;

    for (int i = *y; i < b->line_count; i++) {
        BufferLine *line = b->lines[i];
        char *line_str = line->text;

        int start_char_pos = (i == *y) ? *x + 1 : 0;
        if (start_char_pos >= line->char_count) {
            continue;
        }

        int start_byte_pos = 0;
        char *p = line_str;
        int char_idx = 0;
        while(*p && char_idx < start_char_pos) {
            int len = utf8_char_len(p);
            start_byte_pos += len;
            p += len;
            char_idx++;
        }

        if (start_byte_pos >= line->text_len) {
            continue;
        }

        char* match = strstr(line_str + start_byte_pos, term);
        if (match) {
            int match_byte_pos = match - line_str;
            int match_char_pos = 0;
            p = line_str;
            int current_byte_pos = 0;
            while(current_byte_pos < match_byte_pos) {
                int len = utf8_char_len(p);
                p += len;
                current_byte_pos += len;
                match_char_pos++;
            }
            *y = i;
            *x = match_char_pos;
            return 1;
        }
    }

    // Wrap around
    for (int i = 0; i <= original_y; i++) {
        BufferLine *line = b->lines[i];
        char *line_str = line->text;

        int end_char_pos = (i == original_y) ? original_x : line->char_count - 1;

        char* match = strstr(line_str, term);
        while (match) {
            int match_byte_pos = match - line_str;
            int match_char_pos = 0;
            char *p = line_str;
            int current_byte_pos = 0;
            while(current_byte_pos < match_byte_pos) {
                int len = utf8_char_len(p);
                p += len;
                current_byte_pos += len;
                match_char_pos++;
            }

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
        char *line_str = line->text;

        int start_char_pos = (i == *y) ? *x - 1 : line->char_count - 1;

        for (int j = start_char_pos; j >= 0; j--) {
            int start_byte_pos = 0;
            char *p = line_str;
            int char_idx = 0;
            while(*p && char_idx < j) {
                int len = utf8_char_len(p);
                start_byte_pos += len;
                p += len;
                char_idx++;
            }

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
        char *line_str = line->text;

        int start_char_pos = (i == original_y) ? original_x : line->char_count - 1;

        for (int j = start_char_pos; j >= 0; j--) {
            int start_byte_pos = 0;
            char *p = line_str;
            int char_idx = 0;
            while(*p && char_idx < j) {
                int len = utf8_char_len(p);
                start_byte_pos += len;
                p += len;
                char_idx++;
            }

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
