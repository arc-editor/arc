#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "insert.h"
#include "editor.h"
#include "search.h"
#include "picker_file.h"
#include "picker_buffer.h"
#include "visual.h"

EditorCommand cmd;
EditorCommand prev_cmd;
int command_inited = 0;
char insertion_buffer[64];
int insertion_buffer_count = 0;
int is_insertion_bufferable = 1;
int is_space_mode = 0;
int is_waiting_for_specifier = 0;

void normal_register_insertion(char ch) {
    if (is_insertion_bufferable) {
      if (insertion_buffer_count == sizeof(insertion_buffer)) {
        is_insertion_bufferable = 0;
        insertion_buffer_count = 0;
        return;
      }
      insertion_buffer[insertion_buffer_count] = ch;
      insertion_buffer_count++;
    }
}

void normal_insertion_registration_init() {
    insertion_buffer_count = 0;
    is_insertion_bufferable = 1;
}

void dispatch_command() {
    prev_cmd = cmd;
    editor_command_exec(&cmd);
}

int normal_handle_input(const char *ch_str) {
  if (ch_str[0] == 27 && ch_str[1] == '\0') {
      editor_command_reset(&cmd);
      return 1;
  }

  if (is_waiting_for_specifier) {
    strncpy(cmd.specifier, ch_str, sizeof(cmd.specifier) - 1);
    cmd.specifier[sizeof(cmd.specifier) - 1] = '\0';
    dispatch_command();
    is_waiting_for_specifier = 0;
    return 1;
  }

  if (ch_str[1] != '\0') {
      // Not a single byte character, ignore for now for other commands
      return 1;
  }
  char ch = ch_str[0];

  if (!command_inited) {
    editor_command_reset(&cmd);
    editor_command_reset(&prev_cmd);
    command_inited = 1;
  }

  if (!cmd.action && ch == ' ') {
    is_space_mode = !is_space_mode;
    return 1;
  }

  if (is_space_mode) {
    switch (ch) {
      case 'f':
        picker_file_show();
        break;
      case 'b':
        picker_buffer_show();
        break;
      case 'c':
        editor_close_buffer(editor_get_active_buffer_idx());
        break;
    case 'q':
      return 0;
    case 'w':
      editor_write();
      break;
    }
    is_space_mode = 0;
    return 1;
  }

  if (ch >= '0' && ch <= '9') {
    if (cmd.count) {
      cmd.count = cmd.count * 10 + (ch - '0');
    } else {
      cmd.count = ch - '0';
    }
    return 1;
  }

  if (cmd.action) {
    switch (ch) {
      case 'f':
      case 'F':
      case 't':
      case 'T':
        cmd.target = ch;
        is_waiting_for_specifier = 1;
        break;
      case 'W':
      case 'w':
      case 'e':
      case 'E':
      case 'b':
      case 'l':
      case 'h':
      case 'B':
      case 'p':
        cmd.target = ch;
        dispatch_command();
        break;
    }
    return 1;
  }

  if (!cmd.target && ch == '.') {
    EditorCommand repeated = prev_cmd;
    int is_change = repeated.action == 'c';
    int buff_count = insertion_buffer_count;
    editor_command_exec(&repeated);
    if (is_change) {
      is_insertion_bufferable = 0;
      for (int i = 0; i < buff_count; i++) {
        // This is broken for utf8
        // insert_handle_input(insertion_buffer[i]);
      }
      is_insertion_bufferable = 1;
      editor_handle_input = normal_handle_input;
      insertion_buffer_count = buff_count;
    }
    return 1;
  }

  switch (ch) {
    case 'v':
      {
        Buffer *buffer = editor_get_active_buffer();
        buffer->selection_start_x = buffer->position_x;
        buffer->selection_start_y = buffer->position_y;
        editor_handle_input = visual_handle_input;
        visual_mode_enter();
        editor_request_redraw();
      }
      break;
    case 'x':
      editor_delete();
      break;
    case 'a':
      editor_handle_input = insert_handle_input;
      editor_move_cursor_right();
      break;
    case 'i':
      editor_handle_input = insert_handle_input;
      cmd.action = 'c';
      dispatch_command();
      break;
    case 'k':
      editor_move_cursor_up();
      break;
    case 'j':
      editor_move_cursor_down();
      break;
    case 'h':
      editor_move_cursor_left();
      break;
    case 'l':
      editor_move_cursor_right();
      break;
    case 'w':
    case 'e':
    case 'b':
    case 'W':
    case 'E':
    case 'B':
      cmd.target = ch;
      dispatch_command();
      break;
    case 'f':
    case 'F':
    case 't':
    case 'T':
    case 'n':
    case 'p':
      cmd.target = ch;
      is_waiting_for_specifier = 1;
      break;
    case 'c':
    case 'd':
    case 'g':
      cmd.action = ch;
      break;
    case 0x04: // ctrl d
      editor_move_n_lines_down(cmd.count);
      editor_command_reset(&cmd);
      break;
    case 0x15: // ctrl u
      editor_move_n_lines_up(cmd.count);
      editor_command_reset(&cmd);
      break;
    case '/':
      editor_enter_search_mode(1);
      break;
    case '?':
      editor_enter_search_mode(-1);
      break;
    case '\x0d': {
      const char *last_term = search_get_last_term();
      if (last_term && last_term[0] != '\0') {
        Buffer *buffer = editor_get_active_buffer();
        int y = buffer->position_y;
        int x = buffer->position_x;
        if (search_get_last_direction() == 1) {
          if (buffer_find_forward(buffer, last_term, &y, &x)) {
            buffer->position_y = y;
            buffer->position_x = x;
            editor_center_view();
          }
        } else {
          if (buffer_find_backward(buffer, last_term, &y, &x)) {
            buffer->position_y = y;
            buffer->position_x = x;
            editor_center_view();
          }
        }
        editor_needs_draw();
      }
      break;
    }
  }
  return 1;
}
