#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "theme.h"

static int popup_visible = 0;
static char *popup_text = NULL;
static int popup_line;
static int popup_col_start;
static int popup_col_end;

void ui_show_popup(const char *text, int line, int col_start, int col_end) {
    if (popup_text) {
        free(popup_text);
    }
    popup_text = strdup(text);
    popup_visible = 1;
    popup_line = line;
    popup_col_start = col_start;
    popup_col_end = col_end;
}

void ui_hide_popup(void) {
    if (popup_text) {
        free(popup_text);
        popup_text = NULL;
    }
    popup_visible = 0;
}

int ui_is_popup_visible(void) {
    return popup_visible;
}

int ui_popup_line(void) {
    return popup_line;
}

int ui_popup_col_start(void) {
    return popup_col_start;
}

int ui_popup_col_end(void) {
    return popup_col_end;
}

void ui_draw_popup(const Theme* theme, int diag_start_x, int diag_end_x, int cursor_y, int screen_cols, int screen_rows) {
    if (!popup_visible || !popup_text) {
        return;
    }

    int text_len = strlen(popup_text);
    int width = text_len + 4;
    int height = 5;

    int x = diag_start_x;
    if (x + width > screen_cols) {
        x = diag_end_x - width;
    }

    int y = cursor_y + 1;
    if (y + height > screen_rows) {
        y = cursor_y - height;
    }

    if (x < 1) x = 1;
    if (y < 1) y = 1;

    // Draw box
    printf("\x1b[38;2;%d;%d;%dm", theme->popup_border.fg_r, theme->popup_border.fg_g, theme->popup_border.fg_b);
    for (int row = 0; row < height; row++) {
        printf("\x1b[%d;%dH", y + row, x);
        if (row == 0) {
            printf("╭");
            for (int col = 1; col < width - 1; col++) printf("─");
            printf("╮");
        } else if (row == height - 1) {
            printf("╰");
            for (int col = 1; col < width - 1; col++) printf("─");
            printf("╯");
        } else {
            printf("│");
            for (int col = 1; col < width - 1; col++) printf(" ");
            printf("│");
        }
    }
    printf("\x1b[0m");

    // Draw text
    printf("\x1b[%d;%dH", y + 2, x + 2);
    printf("%s", popup_text);
}

int ui_draw_picker_box(const Theme* theme, int screen_cols, int screen_rows, int *x, int *y, int *w, int *h) {
  // printf("\x1b[?25l"); // Hide cursor
  int margin_y = 2;
  int margin_x = 5;
  int width = screen_cols - margin_x * 2; 
  int height = screen_rows - margin_y * 2 - 1;

  if (width <= 0 || height <= 0) return 0;

  *x = margin_x + 2;
  *y = margin_y + 2;
  *w = width - 2;
  *h = height;

  printf("\x1b[38;2;%d;%d;%dm", theme->picker_border.fg_r, theme->picker_border.fg_g, theme->picker_border.fg_b);
  for (int row = 0; row < height; row++) {
    printf("\x1b[%d;%dH", margin_y + 1 + row, margin_x + 1);

    if (row == 0) {
      // Top border
      printf("╭");
      for (int col = 1; col < width - 1; col++) printf("─");
      printf("╮");
    } else if (row == 1) {
      // Empty row
      printf("│");
      for (int col = 1; col < width - 1; col++) printf(" ");
      printf("│");
    } else if (row == 2) {
      // Horizontal rule
      printf("├");
      for (int col = 1; col < width - 1; col++) printf("─");
      printf("┤");
    } else if (row == height - 1) {
      // Bottom border
      printf("╰");
      for (int col = 1; col < width - 1; col++) printf("─");
      printf("╯");
    } else {
      // Interior rows
      printf("│");
      for (int col = 1; col < width - 1; col++) printf(" ");
      printf("│");
    }
  }
  printf("\x1b[0m");

  return 1;
}