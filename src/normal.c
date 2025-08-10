#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "log.h"
#include "insert.h"
#include "editor.h"
#include "history.h"
#include "search.h"
#include "picker_file.h"
#include "picker_buffer.h"
#include "picker_search.h"
#include "visual.h"

EditorCommand cmd;
EditorCommand prev_cmd;
int command_inited = 0;
char insertion_buffer[64];
int insertion_buffer_count = 0;
int is_insertion_bufferable = 1;
int is_space_mode = 0;
int is_waiting_for_specifier = 0;
int is_waiting_for_text_object_specifier = 0;

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
    history_start_coalescing(editor_get_active_buffer()->history);
}

void dispatch_command() {
    prev_cmd = cmd;
    editor_command_exec(&cmd);
    is_waiting_for_text_object_specifier = 0;
}

void normal_exec_motion(char ch) {
    if (cmd.action) {
        if (cmd.action == 'z') {
            switch(ch) {
                case 'z':
                    editor_center_view();
                    break;
                case 't':
                    editor_scroll_to_top();
                    break;
                case 'b':
                    editor_scroll_to_bottom();
                    break;
            }
            editor_command_reset(&cmd);
            return;
        }

        if (cmd.action == 'g') {
            switch(ch) {
                case 'h':
                    editor_move_to_start_of_line();
                    editor_command_reset(&cmd);
                    return;
                case 'l':
                    editor_move_to_end_of_line();
                    editor_command_reset(&cmd);
                    return;
            }
        }

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
                if (cmd.specifier[0] == '\0') {
                    ch = (ch == 'w') ? 'e' : 'E';
                }
                // fallthrough
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
            default:
                editor_command_reset(&cmd);
                break;
        }
        return;
    }

    switch (ch) {
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
            cmd.target = ch;
            is_waiting_for_specifier = 1;
            break;
        case 'n': {
            const char *last_term = search_get_last_term();
            if (last_term && last_term[0] != '\0') {
                editor_search_next(search_get_last_direction());
            } else {
                cmd.target = ch;
                is_waiting_for_specifier = 1;
            }
            break;
        }
        case 'p': {
            const char *last_term = search_get_last_term();
            if (last_term && last_term[0] != '\0') {
                editor_search_next(-search_get_last_direction());
            } else {
                cmd.target = ch;
                is_waiting_for_specifier = 1;
            }
            break;
        }
        case 'g':
        case 'z':
            cmd.action = ch;
            break;
        case 'c':
        case 'd':
            cmd.action = ch;
            is_waiting_for_text_object_specifier = 1;
            break;
        case 0x04: // ctrl d
            editor_move_n_lines_down(cmd.count);
            editor_command_reset(&cmd);
            break;
        case 0x15: // ctrl u
            editor_move_n_lines_up(cmd.count);
            editor_command_reset(&cmd);
            break;
    }
}

int normal_handle_input(const char *ch_str) {
  if (ch_str[0] == 27 && ch_str[1] == '\0') {
      editor_command_reset(&cmd);
      is_waiting_for_text_object_specifier = 0;
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

  if (is_waiting_for_text_object_specifier) {
    if (ch == 'i' || ch == 'a') {
      strncpy(cmd.specifier, &ch, 1);
      cmd.specifier[1] = '\0';
      is_waiting_for_text_object_specifier = 0;
      return 1;
    }
    is_waiting_for_text_object_specifier = 0;
  }

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
      case '/':
        picker_search_show();
        break;
      case 'b':
        picker_buffer_show();
        break;
      case 'c':
        if (editor_get_active_buffer()->dirty) {
        } else {
          editor_close_buffer(editor_get_active_buffer_idx());
        }
        break;
      case 'C':
        editor_close_buffer(editor_get_active_buffer_idx());
        break;
      case 'q':
        if (editor_is_any_buffer_dirty()) {
        } else {
          return 0;
        }
        break;
      case 'Q':
        return 0;
      case 'w':
        editor_write();
        break;
      case 'W':
        editor_write_force();
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
    normal_exec_motion(ch);
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
    case 'u':
      editor_undo();
      break;
    case 'U':
      editor_redo();
      break;
    case 'J':
      {
        Buffer *buf = editor_get_active_buffer();
        if (buf->position_y < buf->line_count - 1) {
            history_start_coalescing(buf->history);
            editor_move_to_end_of_line();
            int original_x = buf->position_x;
            if (original_x > 0) {
                editor_insert_char(" ");
            }
            editor_delete();
            buf->position_x = original_x;
            history_end_coalescing(buf->history);
        }
      }
      break;
    case 'V':
      {
        Buffer *buffer = editor_get_active_buffer();
        buffer->visual_mode = VISUAL_MODE_LINE;
        buffer->selection_start_x = buffer->position_x;
        buffer->selection_start_y = buffer->position_y;
        editor_handle_input = visual_handle_input;
        visual_mode_enter();
        editor_request_redraw();
      }
      break;
    case 'v':
      {
        Buffer *buffer = editor_get_active_buffer();
        buffer->visual_mode = VISUAL_MODE_CHARACTER;
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
      normal_insertion_registration_init();
      editor_handle_input = insert_handle_input;
      editor_move_cursor_right();
      break;
    case 'i':
      normal_insertion_registration_init();
      editor_handle_input = insert_handle_input;
      cmd.action = 'c';
      dispatch_command();
      break;
    case 'o':
      editor_move_to_end_of_line();
      editor_insert_new_line();
      normal_insertion_registration_init();
      editor_handle_input = insert_handle_input;
      break;
    case 'O':
      editor_move_to_start_of_line();
      editor_insert_new_line();
      editor_move_cursor_up();
      normal_insertion_registration_init();
      editor_handle_input = insert_handle_input;
      break;
    case 'k':
    case 'j':
    case 'h':
    case 'l':
    case 'w':
    case 'e':
    case 'b':
    case 'W':
    case 'E':
    case 'B':
    case 'f':
    case 'F':
    case 't':
    case 'T':
    case 'n':
    case 'p':
    case 'g':
    case 'z':
    case 'c':
    case 'd':
    case 0x04: // ctrl d
    case 0x15: // ctrl u
        normal_exec_motion(ch);
        break;
    case '/':
      search_init(1);
      editor_needs_draw();
      break;
    case '?':
      search_init(-1);
      editor_needs_draw();
      break;
    case '\x0d':
      editor_search_next(search_get_last_direction());
      break;
  }
  return 1;
}
