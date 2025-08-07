#include "visual.h"
#include "editor.h"
#include "normal.h"

int visual_handle_input(char ch) {
    Buffer *buffer = editor_get_active_buffer();
    switch (ch) {
        case 27: // ESC
            editor_handle_input = normal_handle_input;
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
    }
    return 1;
}
