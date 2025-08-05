#ifndef BUFFER_H
#define BUFFER_H

#include "tree_sitter/api.h"
#include "theme.h"

typedef struct {
    char value;
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char italic;
    unsigned char bold;
    unsigned char underline;
} Char;

typedef struct {
    int char_count;
    int capacity;
    Char *chars;
    int needs_highlight;
} BufferLine;

typedef struct {
    int line_count;
    int capacity;
    int dirty;
    int needs_draw;
    int needs_parse;
    int position_y;
    int position_x;
    int offset_y;
    int offset_x;
    char *read_buffer;
    size_t read_buffer_capacity;
    char *file_name;
    int tab_width;
    int line_num_width;
    BufferLine **lines;
    TSParser *parser;
    TSTree *tree;
    TSNode root;
    TSQuery *query;
    TSQueryCursor *cursor;
} Buffer;

void buffer_line_apply_syntax_highlighting(Buffer *b, BufferLine *line, uint32_t start_byte, Theme *theme);
const char *buffer_read(void *payload, uint32_t, TSPoint position, uint32_t *bytes_read);
void buffer_parse(Buffer *b);
int buffer_get_visual_position_x(Buffer *buffer);
void buffer_line_realloc_for_capacity(BufferLine *line, int new_needed_capacity);
void buffer_realloc_lines_for_capacity(Buffer *buffer);
void buffer_reset_offset_y(Buffer *buffer, int screen_rows);
void buffer_move_position_left(Buffer *buffer);
void buffer_move_position_right(Buffer *buffer, int screen_cols);
void buffer_reset_offset_x(Buffer *buffer, int screen_cols);
void buffer_set_logical_position_x(Buffer *buffer, int visual_before);
void buffer_line_init(BufferLine *line);
void buffer_line_destroy(BufferLine *line);
void buffer_init(Buffer *b, char *file_name);
void buffer_destroy(Buffer *b);
void buffer_set_line_num_width(Buffer *buffer);

#endif
