#include <termios.h>
#include <unistd.h>
#include <string.h>
#include "editor.h"
#include "normal.h"
#include "history.h"

int insert_handle_input(const char *ch_str) {
    if (ch_str[0] == 27 && ch_str[1] == '\0') { // ESC
        history_end_coalescing(editor_get_active_buffer()->history);
        editor_handle_input = normal_handle_input;
        editor_needs_draw();
        return 1;
    }
    // normal_register_insertion(ch); // TODO: Fix this
    if ((ch_str[0] == 8 || ch_str[0] == 127) && ch_str[1] == '\0') { // backspace
        editor_backspace();
        return 1;
    }
    if (strcmp(ch_str, "\r") == 0 || strcmp(ch_str, "\n") == 0) {
        editor_insert_new_line();
        return 1;
    }

    editor_insert_char(ch_str);
    return 1;
}
