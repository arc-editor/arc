#ifndef EDITOR_H
#define EDITOR_H

#define EDITOR_VERSION "debug"
#include "theme.h"
#include "buffer.h"

typedef struct {
    int x_start;
    int x_end;
    int y_start;
    int y_end;
} Range;

typedef struct {
  int count; // 1, 10
  char action; // 'c', 'd'
  char target; // 'w', 'e', 'B'
  uint32_t specifier; // '(', '}'
} EditorCommand;

#include <stdint.h>

extern int (*editor_handle_input)(uint32_t);

void editor_command_reset(EditorCommand *cmd);
void editor_command_exec(EditorCommand *cmd);
void editor_draw();
void editor_init(char *file_name);
void editor_start(char *file_name);
void editor_open(char *file_name);
Buffer **editor_get_buffers(int *count);
void editor_set_active_buffer(int index);
int editor_get_active_buffer_idx(void);
void editor_close_buffer(int buffer_index);
void editor_needs_draw();
void editor_clear_screen();
void editor_move_cursor_right();
void editor_move_cursor_left();
void editor_move_cursor_down();
void editor_move_cursor_up();
void editor_insert_char(int);
void editor_insert_new_line();
void editor_write();
void editor_delete();
void editor_backspace();
void editor_move_n_lines_down(int n);
void editor_move_n_lines_up(int n);
void editor_set_style(Style *style, int fg, int bg);
void editor_set_cursor_shape(int shape_code);
void editor_request_redraw(void);
Buffer *editor_get_active_buffer(void);
void range_delete(Buffer *b, Range *range, EditorCommand *cmd);
int range_get_left_boundary(Range *range);
int range_get_right_boundary(Range *range);
int range_get_top_boundary(Range *range);
int range_get_bottom_boundary(Range *range);
int is_word_char(uint32_t ch);
int is_whitespace(uint32_t ch);
int range_expand_right(BufferLine **line, Range *range);
int range_expand_left(BufferLine **line, Range *range);
void range_expand_e(BufferLine *line, int count, Range *range);
void range_expand_E(BufferLine *line, int count, Range *range);
void range_expand_b(BufferLine *line, int count, Range *range);
void range_expand_B(BufferLine *line, int count, Range *range);

#endif
