#ifndef EDITOR_STATE_H
#define EDITOR_STATE_H

#include "buffer.h"
#include "theme.h"
#include "config.h"
#include <stdatomic.h>

#define SEARCH_TERM_MAX_LEN 256

typedef struct {
    int screen_rows;
    int screen_cols;
    atomic_int resize_requested;
    atomic_int redraw_requested;
    Buffer **buffers;
    int buffer_count;
    int buffer_capacity;
    int active_buffer_idx;
    Theme current_theme;
    Config config;

    // Search state
    char search_term[SEARCH_TERM_MAX_LEN];
    int search_term_len;
    int search_direction; // 1 for forward, -1 for backward
    char last_search_term[SEARCH_TERM_MAX_LEN];
    int last_search_direction;
} EditorState;

#endif
