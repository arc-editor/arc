#include <termios.h>
#include <unistd.h>
#include "editor.h"
#include "normal.h"

int insert_handle_input(char ch) {
  if (ch == 27) {
      editor_handle_input = normal_handle_input;
      editor_needs_draw();
      return 1;
  }
  normal_register_insertion(ch);
  if (ch == 8 || ch == 127) {
      editor_backspace();
      return 1;
  }
  if (ch == '\r') {
      ch = '\n';
  }
  if ((ch >= 32 && ch <= 126) || ch == '\t') {
      editor_insert_char(ch);
  }
  if (ch == '\n') {
      editor_insert_new_line();
  }
  return 1;
}
