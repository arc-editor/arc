#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include "theme.h"
#include "lsp.h"

typedef struct {
    const char* text;
    int length;
} DrawableLine;
static int foo = 3;
void ui_draw_popup(const Theme* theme, DiagnosticSeverity severity, const char *popup_text, int cursor_y, int screen_cols, int screen_rows) {
    #define MAX_POPUP_LINES 20
    DrawableLine lines[MAX_POPUP_LINES];
    int num_lines = 0;
    int max_line_len = 0;
    int current_line_len = 0;
    const char* temp_ptr = popup_text;
    while (*temp_ptr) {
        if (*temp_ptr == '\n') {
            if (current_line_len > max_line_len) {
                max_line_len = current_line_len;
            }
            current_line_len = 0;
        } else {
            current_line_len++;
        }
        temp_ptr++;
    }
    if (current_line_len > max_line_len) {
        max_line_len = current_line_len;
    }

    int max_width = screen_cols / 2;
    int width = (max_line_len + 4 < max_width) ? max_line_len + 4 : max_width;

    if (width < 5) width = 5;
    if (width > screen_cols) width = screen_cols;

    int content_width = width - 4;

    const char* line_start = popup_text;
    while (*line_start && num_lines < MAX_POPUP_LINES) {
        const char* line_end = strchr(line_start, '\n');
        const char* segment_end = line_end ? line_end : line_start + strlen(line_start);
        int segment_len = segment_end - line_start;

        if (segment_len == 0) { // Handle empty lines (e.g., "a\n\nb")
             lines[num_lines].text = line_start;
             lines[num_lines].length = 0;
             num_lines++;
        } else {
            const char* segment_ptr = line_start;
            while(segment_ptr < segment_end) {
                int remaining_in_segment = segment_end - segment_ptr;
                int chunk_len = (remaining_in_segment > content_width) ? content_width : remaining_in_segment;

                // If we're breaking a word in the middle (not at a space), add a hyphen
                if (chunk_len < remaining_in_segment && 
                    segment_ptr[chunk_len-1] != ' ' && 
                    segment_ptr[chunk_len] != ' ') {
                    // Reduce chunk length by 1 to make room for hyphen
                    if (chunk_len > 1) {
                        chunk_len--;
                    }
                    // Add the chunk with hyphen
                    if (num_lines < MAX_POPUP_LINES) {
                        lines[num_lines].text = segment_ptr;
                        lines[num_lines].length = chunk_len;
                        // Add hyphen at the end (we'll print it with the text)
                        num_lines++;
                    }
                    segment_ptr += chunk_len;
                } else {
                    if (num_lines < MAX_POPUP_LINES) {
                        lines[num_lines].text = segment_ptr;
                        lines[num_lines].length = chunk_len;
                        num_lines++;
                    }
                    segment_ptr += chunk_len;
                }
            }
        }

        if (line_end) {
            line_start = line_end + 1; // Move to the start of the next line
        } else {
            break; // No more newlines, we're done
        }
    }
    if (num_lines == 0 && strlen(popup_text) == 0) num_lines = 1; // Popup for empty string

    int height = num_lines + 2;
    int x = screen_cols - width;
    int y;

    if (cursor_y < screen_rows / 2) {
        y = screen_rows - height;
    } else {
        y = 1;
    }

    if (x < 1) x = 1;
    if (y < 1) y = 1;

    const Style *style;
    switch (severity) {
        case LSP_DIAGNOSTIC_SEVERITY_ERROR:
            style = &theme->diagnostics_error;
            break;
        case LSP_DIAGNOSTIC_SEVERITY_WARNING:
            style = &theme->diagnostics_warning;
            break;
        case LSP_DIAGNOSTIC_SEVERITY_INFO:
            style = &theme->diagnostics_info;
            break;
        case LSP_DIAGNOSTIC_SEVERITY_HINT:
            style = &theme->diagnostics_hint;
            break;
        default:
            style = &theme->popup_border;
            break;
    }

    // Draw box
    printf("\x1b[38;2;%d;%d;%dm\x1b[48;2;%d;%d;%dm", style->fg_r, style->fg_g, style->fg_b, theme->content_background.bg_r, theme->content_background.bg_g, theme->content_background.bg_b);
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

    // Draw content background
    for (int i = 0; i < num_lines; i++) {
      printf("\x1b[%d;%dH", y + 1 + i, x + 2);
      for (int j = 0; j < width - 4; j++) {
        printf(" ");
      }
    }

    printf("\x1b[38;2;%d;%d;%dm", style->fg_r, style->fg_g, style->fg_b);
    for (int i = 0; i < num_lines; i++) {
        printf("\x1b[%d;%dH", y + 1 + i, x + 2);
        if (lines[i].length > 0) {
            printf("%.*s", lines[i].length, lines[i].text);
            // Add hyphen if we're at the end of a broken word
            if (i < num_lines - 1 &&
                lines[i].text[lines[i].length-1] != ' ' &&
                lines[i+1].text[0] != ' ' &&
                lines[i].text + lines[i].length == lines[i+1].text) {
                printf("-");
            }
        }
    }
    printf("\x1b[0m");
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

  printf("\x1b[38;2;%d;%d;%dm\x1b[48;2;%d;%d;%dm", theme->picker_border.fg_r, theme->picker_border.fg_g, theme->picker_border.fg_b, theme->content_background.bg_r, theme->content_background.bg_g, theme->content_background.bg_b);
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
