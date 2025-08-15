#include "visual.h"
#include <string.h>
#include "editor.h"
#include "normal.h"

extern EditorCommand cmd;
extern int is_waiting_for_specifier;

void visual_mode_enter() {
    editor_command_reset(&cmd);
}

#include "utf8.h"

int visual_handle_input(const char *buf, int len) {
    if (is_waiting_for_specifier) {
        char ch_str[8];
        int char_len = utf8_char_len(buf);
        if (char_len > len) return 1; // Incomplete char
        strncpy(ch_str, buf, char_len);
        ch_str[char_len] = '\0';

        strncpy(cmd.specifier, ch_str, sizeof(cmd.specifier) - 1);
        cmd.specifier[sizeof(cmd.specifier) - 1] = '\0';
        dispatch_command();
        is_waiting_for_specifier = 0;
        editor_command_reset(&cmd);
        return 1;
    }

    if (len > 1) {
        // Not a single byte character, ignore for now.
        return 1;
    }
    char ch = buf[0];

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
            editor_get_active_buffer()->visual_mode = VISUAL_MODE_NONE;
            editor_handle_input = normal_handle_input;
            editor_request_redraw();
            editor_command_reset(&cmd);
            break;
        case ';': {
            Buffer *b = editor_get_active_buffer();
            int temp_y = b->selection_start_y;
            int temp_x = b->selection_start_x;
            b->selection_start_y = b->position_y;
            b->selection_start_x = b->position_x;
            b->position_y = temp_y;
            b->position_x = temp_x;
            buffer_reset_offset_x(b, editor.screen_cols);
            buffer_reset_offset_y(b, editor.screen_rows);
            editor_request_redraw();
            break;
        }
        case 'd':
        case 'c':
            cmd.action = ch;
            editor_command_exec(&cmd);
            editor_command_reset(&cmd);
            editor_get_active_buffer()->visual_mode = VISUAL_MODE_NONE;
            break;
        default:
            normal_exec_motion(ch);
            break;
    }
    return 1;
}
