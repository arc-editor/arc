#ifndef PICKER_H
#define PICKER_H

#include "theme.h"

typedef struct {
    char flag;
    int bold;
    int italic;
} PickerItemStyle;

typedef struct {
    void (*on_open)();
    void (*on_close)();
    void (*on_select)(int selection_idx, int *close_picker);
    int (*get_item_count)();
    const char* (*get_item_text)(int index);
    void (*update_results)(const char *search);
    int (*get_results_count)();
    int (*get_result_index)(int result_idx);
    void (*get_item_style)(int index, PickerItemStyle *style);
} PickerDelegate;

void picker_set_delegate(PickerDelegate *delegate);
void picker_open();
int picker_is_open();
void picker_close();
int picker_handle_input(const char *ch);
void picker_draw(int screen_cols, int screen_rows, Theme *theme);

#endif // PICKER_H