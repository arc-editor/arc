#define _POSIX_C_SOURCE 200809L


#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <math.h>
#include "buffer.h"
#include "log.h"
#include "config.h"

void buffer_set_line_num_width(Buffer *buffer) {
    buffer->line_num_width = (int)floor(log10(buffer->line_count)) + 3;
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

typedef struct {
    const char* name;
    int priority;
} CapturePriorityTableEntry;

// Static lookup table - MUST be sorted alphabetically by name for bsearch
static const CapturePriorityTableEntry priority_table[] = {
    {"attribute", 15},
    {"comment", 10},
    {"constant", 85},
    {"constant.builtin.boolean", 87},
    {"constant.character", 87},
    {"constant.character.escape", 88},
    {"constant.numeric", 87},
    {"error", 5},
    {"function", 100},
    {"function.builtin", 105},
    {"function.special", 110},
    {"info", 5},
    {"keyword", 80},
    {"keyword.control", 81},
    {"keyword.control.conditional", 81},
    {"keyword.control.repeat", 81},
    {"keyword.control.return", 81},
    {"keyword.directive", 83},
    {"keyword.storage.modifier", 82},
    {"keyword.storage.type", 82},
    {"label", 25},
    {"operator", 70},
    {"punctuation", 20},
    {"punctuation.bracket", 20},
    {"punctuation.delimiter", 20},
    {"string", 60},
    {"type", 90},
    {"type.builtin", 95},
    {"type.enum.variant", 92},
    {"variable", 90},
    {"variable.other.member", 52},
    {"variable.parameter", 91},
    {"warning", 5}
};

static const size_t priority_table_size = sizeof(priority_table) / sizeof(priority_table[0]);

static int compare_capture_names(const void* key, const void* elem) {
    const CapturePriorityTableEntry* entry = (const CapturePriorityTableEntry*)elem;
    return strcmp(key, entry->name);
}

int get_capture_priority(const char* capture_name) {
    if (capture_name == NULL) return 0;
    
    CapturePriorityTableEntry* result = bsearch(
        capture_name,
        priority_table,
        priority_table_size,
        sizeof(CapturePriorityTableEntry),
        compare_capture_names
    );
    
    if (result != NULL) {
        return result->priority;
    }
    
    log_warning("editor.get_capture_priority: unrecognized capture_name %s", capture_name);
    return 0;
}

void buffer_line_apply_syntax_highlighting(Buffer *b, BufferLine *line, uint32_t start_byte, Theme *theme) {
    if (!b->cursor) {
        for (int char_idx = 0; char_idx < line->char_count; char_idx++) {
            Char *ch = &line->chars[char_idx];
            Style *style = &theme->syntax_variable;
            ch->r = style->fg_r;
            ch->g = style->fg_g;
            ch->b = style->fg_b;
            ch->bold = style->bold;
            ch->italic = style->italic;
        }
        return;
    }
    ts_query_cursor_set_byte_range(b->cursor, start_byte, start_byte + line->char_count);
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

    for (int char_idx = 0; char_idx < line->char_count; char_idx++) {
        const char *best_capture = find_best_capture_for_position(captures, capture_count, start_byte + char_idx);
        Char *ch = &line->chars[char_idx];
        Style *style = theme_get_capture_style(best_capture, theme);
        ch->r = style->fg_r;
        ch->g = style->fg_g;
        ch->b = style->fg_b;
        ch->bold = style->bold;
        ch->italic = style->italic;
    }

    if (captures) {
        free(captures);
    }
    line->needs_highlight = 0;
}

const char *buffer_read(void *payload, uint32_t, TSPoint position, uint32_t *bytes_read) {
    Buffer *buffer = (Buffer *)payload;
    if ((int)position.row >= buffer->line_count) {
        *bytes_read = 0;
        return "";
    }
    BufferLine *line = buffer->lines[position.row];
    int remaining = line->char_count - (int)position.column;
    if (remaining < 0) remaining = 0;
    size_t needed = (size_t)remaining + 1;
    if (needed > buffer->read_buffer_capacity) {
        size_t new_capacity = needed * 2;
        char *new_buffer = realloc(buffer->read_buffer, new_capacity);
        if (!new_buffer) {
            log_error("buffer.buffer_read: malloc read buffer");
            exit(1);
        }
        buffer->read_buffer = new_buffer;
        buffer->read_buffer_capacity = new_capacity;
    }
    for (int i = 0; i < remaining; i++) {
        buffer->read_buffer[i] = line->chars[position.column + i].value;
    }
    buffer->read_buffer[remaining] = '\n';
    *bytes_read = remaining + 1;
    return buffer->read_buffer;
}


void buffer_parse(Buffer *b) {
    TSTree *old_tree = b->tree;
    TSInput ts_input = {
        .read = buffer_read,
        .encoding = TSInputEncodingUTF8,
        .payload = b,
    };
    if (b->cursor) {
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
    int x = buffer->position_x + buffer->line_num_width + 1;
    for (int i = 0; i < buffer->position_x; i++) {
        if (line->chars[i].value == '\t') {
            x += buffer->tab_width - 1;
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

    char *lang_name = get_file_extension(file_name);
    TSLanguage *lang = config_load_language(lang_name);
    if (lang) {
        b->parser = ts_parser_new();
        ts_parser_set_language(b->parser, lang);
        b->query = config_load_highlights(lang, lang_name);
        if (!b->query) {
            return;
        }
        b->cursor = ts_query_cursor_new();
        if (!b->cursor) {
            log_error("buffer.buffer_init: failed to create query cursor");
            exit(1);
        }
    }

    if (file_name == NULL) {
        return;
    }
    FILE *fp = fopen(file_name, "r");
    if (fp == NULL) {
        log_error("buffer.buffer_init: failed to allocate first BufferLine");
        exit(1);
    }
        
    char ch;
    while ((ch = fgetc(fp)) != EOF) {
        if (ch == '\n') {
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
            Char new_char = line->chars[line->char_count];
            new_char.value = ch;
            line->chars[line->char_count] = new_char;
            line->char_count++;
        }
    }
    fclose(fp);
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
