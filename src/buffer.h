#ifndef BUFFER_H
#define BUFFER_H

#include <time.h>
#include "tree_sitter/api.h"
#include "theme.h"
#include "history.h"
#include "buffer_types.h"

typedef enum {
    VISUAL_MODE_NONE,
    VISUAL_MODE_CHARACTER,
    VISUAL_MODE_LINE,
} VisualMode;

typedef struct {
    int line_count;
    int capacity;
    int dirty;
    int needs_draw;
    int needs_parse;
    int position_y;
    int position_x;
    int selection_start_y;
    int selection_start_x;
    VisualMode visual_mode;
    int offset_y;
    int offset_x;
    char *read_buffer;
    size_t read_buffer_capacity;
    char *file_name;
    time_t mtime;
    int tab_width;
    int line_num_width;
    int version;
    BufferLinesNode *lines;
    TSParser *parser;
    TSTree *tree;
    TSNode root;
    TSQuery *query;
    TSQueryCursor *cursor;
    int diagnostics_version;
    History *history;

    struct {
        char *term;
        int current;
        int count;
        struct {
            int y;
            int x;
        } *matches;
    } search_state;
} Buffer;

void buffer_line_apply_syntax_highlighting(Buffer *b, BufferLine *line, uint32_t start_byte, Theme *theme);
const char *buffer_read(void *payload, uint32_t, TSPoint position, uint32_t *bytes_read);
void buffer_parse(Buffer *b);
int buffer_get_visual_position_x(Buffer *buffer);
int buffer_get_byte_position_x(Buffer *buffer);
void buffer_line_realloc_for_capacity(BufferLine *line, int new_needed_capacity);
void buffer_realloc_lines_for_capacity(Buffer *buffer);
void buffer_reset_offset_y(Buffer *buffer, int screen_rows);
void buffer_reset_offset_x(Buffer *buffer, int screen_cols);
void buffer_set_logical_position_x(Buffer *buffer, int visual_before);
void buffer_line_init(BufferLine *line);
void buffer_line_destroy(BufferLine *line);
void buffer_init(Buffer *b, char *file_name);
void buffer_destroy(Buffer *b);
void buffer_set_line_num_width(Buffer *b);
char *buffer_get_content(Buffer *b);
int is_line_empty(BufferLine *line);
int buffer_get_visual_x_for_line_pos(Buffer *buffer, int y, int logical_x);
int buffer_find_forward(Buffer *b, const char *term, int *y, int *x);
int buffer_find_backward(Buffer *b, const char *term, int *y, int *x);
void buffer_update_search_matches(Buffer *b, const char *term);
void buffer_clear_search_state(Buffer *b);
void buffer_update_current_search_match(Buffer *b);
int buffer_find_first_match(Buffer *b, const char *term, int start_y, int start_x, int *match_y, int *match_x);
int buffer_find_last_match_before(Buffer *b, const char *term, int start_y, int start_x, int *match_y, int *match_x);

#endif
