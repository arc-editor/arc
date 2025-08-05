#ifndef EDITOR_H
#define EDITOR_H

#define EDITOR_VERSION "debug"
#include "theme.h"
#include "buffer.h"

typedef struct {
  int count; // 1, 10
  char action; // 'c', 'd'
  char target; // 'w', 'e', 'B'
  char specifier; // '(', '}'
} EditorCommand;

extern int (*editor_handle_input)(char);

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

#endif
