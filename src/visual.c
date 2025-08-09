#include "visual.h"
#include <string.h>
#include "editor.h"
#include "normal.h"

static EditorCommand cmd;
static int is_waiting_for_specifier = 0;

void visual_mode_enter() {
    editor_command_reset(&cmd);
}

int visual_handle_input(const char *ch_str) {
    if (is_waiting_for_specifier) {
        strncpy(cmd.specifier, ch_str, sizeof(cmd.specifier) - 1);
        cmd.specifier[sizeof(cmd.specifier) - 1] = '\0';
        editor_command_exec(&cmd);
        is_waiting_for_specifier = 0;
        editor_command_reset(&cmd);
        return 1;
    }

    if (ch_str[1] != '\0') {
        // Not a single byte character, ignore for now.
        return 1;
    }
    char ch = ch_str[0];

    if (ch >= '0' && ch <= '9') {
        if (cmd.count) {
            cmd.count = cmd.count * 10 + (ch - '0');
        } else {
            cmd.count = ch - '0';
        }
        return 1;
    }

    switch (ch) {
        case 27: // ESC
            editor_handle_input = normal_handle_input;
            editor_request_redraw();
            editor_command_reset(&cmd);
            break;
        case 'j':
            editor_move_cursor_down();
            break;
        case 'k':
            editor_move_cursor_up();
            break;
        case 'h':
            editor_move_cursor_left();
            break;
        case 'l':
            editor_move_cursor_right();
            break;
        case ';': {
            Buffer *b = editor_get_active_buffer();
            int temp_y = b->selection_start_y;
            int temp_x = b->selection_start_x;
            b->selection_start_y = b->position_y;
            b->selection_start_x = b->position_x;
            b->position_y = temp_y;
            b->position_x = temp_x;
            editor_request_redraw();
            break;
        }
        case 'w':
        case 'e':
        case 'b':
        case 'W':
        case 'E':
        case 'B':
            cmd.target = ch;
            editor_command_exec(&cmd);
            editor_command_reset(&cmd);
            break;
        case 'd':
        case 'c':
            cmd.action = ch;
            editor_command_exec(&cmd);
            editor_command_reset(&cmd);
            break;
        case 'f':
        case 'F':
        case 't':
        case 'T':
            cmd.target = ch;
            is_waiting_for_specifier = 1;
            break;
    }
    return 1;
}
