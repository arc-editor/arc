#ifndef UI_H
#define UI_H

#include "theme.h"
#include "lsp.h"

int ui_draw_picker_box(const Theme* theme, int screen_cols, int screen_rows, int *x, int *y, int *w, int *h);
void ui_draw_popup(const Theme* theme, DiagnosticSeverity severity, const char *popup_text, int cursor_y, int screen_cols, int screen_rows);

#endif
