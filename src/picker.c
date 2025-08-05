#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "picker.h"
#include "ui.h"
#include "editor.h"
#include "normal.h"
#include "theme.h"
#include "fuzzy.h"

static int selection = 0;
static char search[256];
static unsigned char search_len = 0;
static int is_open = 0;

static PickerDelegate *delegate = NULL;

void picker_set_delegate(PickerDelegate *new_delegate) {
    delegate = new_delegate;
}

void picker_open() {
    if (!delegate) return;
    is_open = 1;
    selection = 0;
    search_len = 0;
    search[0] = '\0';
    if (delegate->on_open) {
        delegate->on_open();
    }
    editor_handle_input = picker_handle_input;
    editor_request_redraw();
}

int picker_is_open() {
    return is_open;
}

void picker_close() {
    if (delegate && delegate->on_close) {
        delegate->on_close();
    }
    is_open = 0;
    delegate = NULL;
    editor_handle_input = normal_handle_input;
    editor_request_redraw();
}

int picker_handle_input(char ch) {
    if (!delegate) return 0;

    if (ch == 13) { // Enter key
        if (delegate->get_results_count() > 0) {
            int close_picker = 1;
            delegate->on_select(selection, &close_picker);
            if (close_picker) {
                picker_close();
            }
        }
        return 1;
    }
    if (ch == 27) { // Escape key
        picker_close();
        return 1;
    }
    if (ch == 14) { // Down arrow
        if (selection < delegate->get_results_count() - 1) {
            selection++;
            editor_request_redraw();
        }
        return 1;
    } else if (ch == 16) { // Up arrow
        if (selection > 0) {
            selection--;
            editor_request_redraw();
        }
        return 1;
    }
    
    selection = 0;
    if (ch == 8 || ch == 127) { // Backspace
        if (search_len > 0) {
            search_len--;
            search[search_len] = '\0';
            editor_request_redraw();
        }
    } else if ((ch >= 32 && ch <= 126)) {
        if (search_len < sizeof(search) - 1) {
            search[search_len++] = ch;
            search[search_len] = '\0';
            editor_request_redraw();
        }
    }

    if (delegate->update_results) {
        delegate->update_results(search);
    }

    return 1;
}

void picker_draw(int screen_cols, int screen_rows, Theme *theme) {
    if (!is_open || !delegate) return;

    int x, y, w, h;
    if (!ui_draw_picker_box(theme, screen_cols, screen_rows, &x, &y, &w, &h)) {
        return;
    }
    int initial_x = x;
    int initial_y = y;

    // Draw search input
    Style search_style = theme->picker_item_text;
    search_style.bg_r = theme->content_background.bg_r;
    search_style.bg_g = theme->content_background.bg_g;
    search_style.bg_b = theme->content_background.bg_b;
    editor_set_style(&search_style, 1, 1);
    printf("\x1b[%d;%dH", y, x);
    char *search_ptr = search;
    int adj_search_len = search_len;
    if (adj_search_len > w - 2) {
        search_ptr += search_len - (w - 2);
        adj_search_len = w - 2;
    }
    printf(" %s", search_ptr);
    for (int i = search_len + 1; i < w; i++) {
        putchar(' ');
    }
    y += 2;
    h -= 4;

    // Draw results
    int results_count = delegate->get_results_count();

    for (int i = 0; i < results_count && i < h; i++) {
        int item_index = delegate->get_result_index(i);
        printf("\x1b[%d;%dH", y + i, x);
        const char *item_text = delegate->get_item_text(item_index);
        
        Style style = theme->picker_item_text;
        if (selection == i) {
            style.bg_r = theme->content_cursor_line.bg_r;
            style.bg_g = theme->content_cursor_line.bg_g;
            style.bg_b = theme->content_cursor_line.bg_b;
        } else {
            style.bg_r = theme->content_background.bg_r;
            style.bg_g = theme->content_background.bg_g;
            style.bg_b = theme->content_background.bg_b;
        }

        PickerItemStyle item_style = { .flag = ' ', .bold = 0, .italic = 0 };
        if (delegate->get_item_style) {
            delegate->get_item_style(item_index, &item_style);
        }

        style.bold = item_style.bold;
        style.italic = item_style.italic;
        editor_set_style(&style, 1, 1);

        printf(" %c", item_style.flag);

        int len = strlen(item_text);
        int display_width = w - 2 - 2;
        const char *truncated_item_text = item_text;
        if (len > display_width) {
            truncated_item_text += len - display_width;
        }
        
        int matches[256];
        int match_count = fuzzy_match(item_text, search, matches, 256);
        int match_idx = 0;

        printf(" ");
        for (int j = 0; j < (int)strlen(truncated_item_text); j++) {
            int original_idx = truncated_item_text - item_text + j;
            if (match_idx < match_count && original_idx == matches[match_idx]) {
                Style highlight_style = style;
                highlight_style.fg_r = theme->picker_item_text_highlight.fg_r;
                highlight_style.fg_g = theme->picker_item_text_highlight.fg_g;
                highlight_style.fg_b = theme->picker_item_text_highlight.fg_b;
                editor_set_style(&highlight_style, 1, 1);
                printf("%c", truncated_item_text[j]);
                editor_set_style(&style, 1, 1);
                match_idx++;
            } else {
                printf("%c", truncated_item_text[j]);
            }
        }
        printf(" ");

        int text_len = strlen(truncated_item_text);
        for (int j = text_len + 2; j < w - 2; j++) {
            putchar(' ');
        }
    }

    // editor_set_cursor_shape(5);
    printf("\x1b[%d;%dH", initial_y, initial_x + 1 + adj_search_len);
}
