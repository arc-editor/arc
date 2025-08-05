#include <stdio.h>

int ui_draw_picker_box(int screen_cols, int screen_rows, int *x, int *y, int *w, int *h) {
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

  for (int row = 0; row < height; row++) {
    printf("\x1b[%d;%dH", margin_y + 1 + row, margin_x + 1);

    if (row == 0) {
      // Top border
      printf("┌");
      for (int col = 1; col < width - 1; col++) printf("─");
      printf("┐");
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
      printf("└");
      for (int col = 1; col < width - 1; col++) printf("─");
      printf("┘");
    } else {
      // Interior rows
      printf("│");
      for (int col = 1; col < width - 1; col++) printf(" ");
      printf("│");
    }
  }

  return 1;
}


