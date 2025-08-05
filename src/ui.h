#ifndef UI_H
#define UI_H

#include "theme.h"

int ui_draw_picker_box(const Theme* theme, int screen_cols, int screen_rows, int *x, int *y, int *w, int *h);
void ui_draw_popup(const Theme* theme, int diag_start_x, int diag_end_x, int cursor_y, int screen_cols, int screen_rows);
void ui_show_popup(const char *text, int line, int col_start, int col_end);
void ui_hide_popup(void);
int ui_is_popup_visible(void);
int ui_popup_line(void);
int ui_popup_col_start(void);
int ui_popup_col_end(void);

#endif
