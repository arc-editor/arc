#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 199309L

#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
#include <limits.h>
#include "log.h"
#include "picker.h"
#include "picker_file.h"
#include "tree_sitter/api.h"
#include "insert.h"
#include "normal.h"
#include "visual.h"
#include "editor.h"
#include "git.h"
#include "theme.h"
#include "config.h"
#include "buffer.h"
#include "picker_file.h"
#include "lsp.h"
#include "ui.h"
#include "utf8.h"
#include "search.h"

static pthread_mutex_t editor_mutex = PTHREAD_MUTEX_INITIALIZER;

int (*editor_handle_input)(const char *);

static int screen_rows;
static int screen_cols;
static atomic_int resize_requested = 0;
static atomic_int redraw_requested = 0;
static Buffer **buffers;
static int buffer_count = 0;
static int buffer_capacity = 0;
static int active_buffer_idx = 0;

#define buffer (buffers[active_buffer_idx])
static Theme current_theme;
static Config config;

void editor_command_reset(EditorCommand *cmd) {
    memset(cmd, 0, sizeof(EditorCommand));
}

void editor_set_style(Style *style, int fg, int bg) {
    char escape_seq[128];
    char *p = escape_seq;
    p += sprintf(p, "\x1b[");

    if (style->style & STYLE_BOLD) {
        p += sprintf(p, "1;");
    } else {
        p += sprintf(p, "22;");
    }
    if (style->style & STYLE_ITALIC) {
        p += sprintf(p, "3;");
    } else {
        p += sprintf(p, "23;");
    }
    if (style->style & STYLE_UNDERLINE) {
        p += sprintf(p, "4;");
    } else {
        p += sprintf(p, "24;");
    }

    if (fg) {
        p += sprintf(p, "38;2;%d;%d;%d", style->fg_r, style->fg_g, style->fg_b);
        if (bg) {
            p += sprintf(p, ";");
        }
    }
    if (bg) {
        p += sprintf(p, "48;2;%d;%d;%d", style->bg_r, style->bg_g, style->bg_b);
    }

    *p++ = 'm';
    *p = '\0';

    printf("%s", escape_seq);
}

void handle_sigwinch(int arg __attribute__((unused))) {
    atomic_store(&resize_requested, 1);
}

#ifndef TEST_BUILD
void init_terminal_size() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        log_error("editor.init_terminal_size: ioctl");
        exit(1);
    }
    screen_rows = ws.ws_row;
    screen_cols = ws.ws_col;

    struct sigaction sa;
    sa.sa_handler = handle_sigwinch;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGWINCH, &sa, NULL) == -1) {
        log_error("editor.init_terminal_size: sigaction");
        exit(1);
    }
}
#else
void init_terminal_size() {}
#endif

void check_for_resize() {
    if (atomic_exchange(&resize_requested, 0)) {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
            if (ws.ws_row > 0 && ws.ws_col > 0) {
                screen_rows = ws.ws_row;
                screen_cols = ws.ws_col;
                buffer->needs_draw = 1;
            }
        }
    }
}

void check_for_redraw_request() {
    if (atomic_exchange(&redraw_requested, 0)) {
        buffer->needs_draw = 1;
    }
}

void editor_clear_screen() {
    printf("\033[2J\033[H");
}

void editor_set_cursor_shape(int shape_code) {
    printf("\x1b[%d q", shape_code);
}

void draw_statusline() {
    printf("\x1b[%d;1H", screen_rows);

    const char *mode;
    if (editor_handle_input == insert_handle_input) {
        editor_set_style(&current_theme.statusline_mode_insert, 1, 1);
        mode = " INSERT ";
    } else if (editor_handle_input == visual_handle_input) {
        editor_set_style(&current_theme.statusline_mode_visual, 1, 1);
        mode = " VISUAL ";
    } else if (editor_handle_input == search_handle_input) {
        editor_set_style(&current_theme.statusline_mode_command, 1, 1);
        mode = " COMMAND ";
    } else {
        editor_set_style(&current_theme.statusline_mode_normal, 1, 1);
        mode = " NORMAL ";
    }

    char position[16];
    int position_len = snprintf(position, sizeof(position), " %d:%d ", buffer->position_y + 1, buffer->position_x + 1);

    char line_count[8];
    int line_count_len = snprintf(line_count, sizeof(line_count), " %d ", buffer->line_count);

    int mode_len = strlen(mode);
    int file_name_len = 0;
    if (buffer->file_name) {
        file_name_len = strlen(buffer->file_name) + 2;
    }
    if (buffer->dirty) {
        file_name_len += buffer->file_name ? 4 : 5;
    }

    char branch_name[256];
    git_current_branch(branch_name, sizeof(branch_name));
    int branch_name_len = strlen(branch_name);
    if (branch_name_len) {
        branch_name_len += 2;
    }

    printf("%s", mode);
    editor_set_style(&current_theme.statusline_text, 1, 1);

    if (editor_handle_input == search_handle_input) {
        const char *search_term = search_get_term();
        char prompt_char = search_get_prompt_char();
        int search_len = printf(" %c%s", prompt_char, search_term);
        for (int i = 0; i < screen_cols - mode_len - search_len; i++) {
            putchar(' ');
        }
    } else {
        int left_len = mode_len + branch_name_len;
        int right_len = position_len + line_count_len;
        char search_stats[32] = {0};
        int search_stats_len = 0;
        if (buffer->search_state.count > 0 && buffer->search_state.current != -1) {
            search_stats_len = snprintf(search_stats, sizeof(search_stats), "[%d/%d]", buffer->search_state.current + 1, buffer->search_state.count);
            right_len += search_stats_len + 1;
        }

        int half_cols = screen_cols / 2;
        int half_file_name_len = file_name_len / 2;
        int left_space = half_cols - left_len - half_file_name_len;
        int right_space = (screen_cols - half_cols) - right_len - (file_name_len - half_file_name_len);

        if (branch_name_len) {
            printf(" %s ", branch_name);
        }
        for (int i = 0; i < left_space; i++) putchar(' ');
        if (buffer->file_name) {
            printf(" %s ", buffer->file_name);
        }
        if (buffer->dirty) {
            if (buffer->file_name) {
                printf("[+] ");
            } else {
                printf(" [+] ");
            }
        }

        for (int i = 0; i < right_space; i++) putchar(' ');

        if (search_stats_len > 0) {
            printf("%s ", search_stats);
        }
        printf("%s%s", position, line_count);
    }
    printf("\x1b[0m");
}

static int is_in_selection(int y, int x) {
    Buffer *buf = buffer;
    int start_y = buf->selection_start_y;
    int start_x = buf->selection_start_x;
    int end_y = buf->position_y;
    int end_x = buf->position_x;
    if ((end_x <= start_x && end_y <= start_y) || end_y < start_y) start_x++;

    if (start_y > end_y || (start_y == end_y && start_x > end_x)) {
        int tmp_y = start_y;
        int tmp_x = start_x;
        start_y = end_y;
        start_x = end_x;
        end_y = tmp_y;
        end_x = tmp_x;
    }

    if (y < start_y || y > end_y) {
        return 0;
    }
    if (y == start_y && x < start_x) {
        return 0;
    }
    if (y == end_y && x >= end_x) {
        return 0;
    }
    return 1;
}

void draw_buffer(Diagnostic *diagnostics, int diagnostics_count, int update_diagnostics) {
    uint32_t start_byte = 0;
    for (int i = 0; i < buffer->offset_y; i++) {
        BufferLine *line = buffer->lines[i];
        for (int j = 0; j < line->char_count; j++) {
            start_byte += strlen(line->chars[j].value);
        }
        start_byte++; // for newline
    }
    for (int row = buffer->offset_y; row < buffer->offset_y + screen_rows - 1; row++) {
        int relative_y = row - buffer->offset_y;
        printf("\x1b[%d;1H", relative_y + 1);
        if (row >= buffer->line_count) {
            int chars_to_print = screen_cols;
            editor_set_style(&current_theme.content_background, 0, 1);
            while (chars_to_print > 0) {
                putchar(' ');
                chars_to_print--;
            }
            continue;
        }
        BufferLine *line = buffer->lines[row];

        if (line->needs_highlight) {
            buffer_line_apply_syntax_highlighting(buffer, line, start_byte, &current_theme);
        }
        if (update_diagnostics) {
            for (int j = 0; j < line->char_count; j++) {
                line->chars[j].style &= ~STYLE_UNDERLINE;
            }
            for (int i = 0; i < diagnostics_count; i++) {
                Diagnostic d = diagnostics[i];
                if (d.line == row) {
                    for (int j = d.col_start; j < d.col_end && j < line->char_count; j++) {
                        line->chars[j].style |= STYLE_UNDERLINE;
                    }
                }
            }
        }
        char line_num_str[16];
        int line_num_len = snprintf(line_num_str, sizeof(line_num_str), "%*d ", buffer->line_num_width - 1, row + 1);
        if (buffer->offset_x) {
            editor_set_style(&current_theme.content_line_number_sticky, 1, 1);
        } else if (row == buffer->position_y) {
            editor_set_style(&current_theme.content_line_number_active, 1, 1);
        } else {
            editor_set_style(&current_theme.content_line_number, 1, 1);
        }
        printf("%.*s", line_num_len, line_num_str);

        int is_visual_mode = editor_handle_input == visual_handle_input;
        Style *line_style;
        if (row == buffer->position_y) {
            line_style = &current_theme.content_cursor_line;
        } else {
            line_style = &current_theme.content_background;
        }
        editor_set_style(line_style, 0, 1);

        int cols_to_skip = buffer->offset_x;
        int chars_to_print = screen_cols - buffer->line_num_width;

        // Iterate over characters in the line
        for (int ch_idx = 0; ch_idx < line->char_count && chars_to_print > 0; ch_idx++) {
            Char ch = line->chars[ch_idx];
            if (strcmp(ch.value, "\t") == 0) {
                if (cols_to_skip >= buffer->tab_width) {
                    cols_to_skip -= buffer->tab_width;
                    continue;
                }
                int width = buffer->tab_width - (cols_to_skip % buffer->tab_width);
                cols_to_skip = 0;
                for (int i = 0; i < width && chars_to_print > 0; i++) {
                    putchar(' ');
                    chars_to_print--;
                }
                continue;
            }
            if (cols_to_skip) {
                cols_to_skip--;
                continue;
            }
            
            Style* style = line_style;
            int in_selection = is_visual_mode && is_in_selection(row, ch_idx);

            if (in_selection) {
                style = &current_theme.content_selection;
            }

            Style char_style;
            char_style.fg_r = ch.r;
            char_style.fg_g = ch.g;
            char_style.fg_b = ch.b;
            char_style.bg_r = style->bg_r;
            char_style.bg_g = style->bg_g;
            char_style.bg_b = style->bg_b;
            char_style.style = ch.style;

            editor_set_style(&char_style, 1, 1);
            printf("%s", ch.value);
            chars_to_print--;
        }
        editor_set_style(line_style, 1, 1);
        while (chars_to_print > 0) {
            putchar(' ');
            chars_to_print--;
        }
        for (int i = 0; i < line->char_count; i++) {
            start_byte += strlen(line->chars[i].value);
        }
        start_byte++; // for newline
    }
    printf("\x1b[0m");
}

void draw_cursor() {
    printf("\x1b[?25h"); // Show cursor
    if (editor_handle_input == insert_handle_input) {
        editor_set_cursor_shape(5);
    } else {
        editor_set_cursor_shape(2);
    }
    int y = buffer->position_y - buffer->offset_y + 1;
    int x = buffer_get_visual_position_x(buffer);
    printf("\x1b[%d;%dH", y, x);
}

void draw_diagnostics(const Diagnostic *diagnostics, int diagnostics_count) {
    for (int i = 0; i < diagnostics_count; i++) {
        Diagnostic d = diagnostics[i];
        if (d.line != buffer->position_y) continue;
        if (d.col_start == d.col_end || (d.col_start <= buffer->position_x && d.col_end > buffer->position_x)) {
            int y = buffer->position_y - buffer->offset_y + 1;
            ui_draw_popup(&current_theme, d.severity, d.message, y, screen_cols, screen_rows);
        }
    }
}

void editor_draw() {
    if (!buffer->needs_draw) {
        return;
    }
    if (buffer->needs_parse) {
        buffer_parse(buffer);
    }
    // TODO: free once malloc was used
    Diagnostic *diagnostics = NULL;
    int diagnostic_count = 0;
    int prev_diag_version = buffer->diagnostics_version;
    int update_diagnostics = 0;
    if (buffer->file_name) {
        buffer->diagnostics_version = lsp_get_diagnostics(buffer->file_name, &diagnostics, &diagnostic_count);
        if (buffer->diagnostics_version != prev_diag_version) {
            update_diagnostics = 1;
        }
    }
    editor_clear_screen();
    draw_buffer(diagnostics, diagnostic_count, update_diagnostics);
    draw_statusline();
    if (picker_is_open()) {
        picker_draw(screen_cols, screen_rows, &current_theme);
    }
    if (editor_handle_input == normal_handle_input) {
        draw_diagnostics(diagnostics, diagnostic_count);
    }
    if (diagnostics) {
        free(diagnostics);
        for (int i = 0; i < diagnostic_count; i++) {
            free(diagnostics[i].message);
        }
    }
    draw_cursor();
    fflush(stdout);
    buffer->needs_draw = 0;
}

void editor_request_redraw(void) {
    atomic_store(&redraw_requested, 1);
}

void editor_needs_draw() {
    pthread_mutex_lock(&editor_mutex);
    buffer->needs_draw  = 1;
    pthread_mutex_unlock(&editor_mutex);
}

void editor_did_change_buffer() {
    buffer->dirty = 1;
    buffer->needs_draw = 1;
    if (buffer->parser) {
        buffer->needs_parse = 1;
    }
    buffer->version++;

    if (buffer->file_name) {
        char *content = buffer_get_content(buffer);
        if (content) {
            char absolute_path[PATH_MAX];
            if (realpath(buffer->file_name, absolute_path) != NULL) {
                char file_uri[PATH_MAX + 7];
                snprintf(file_uri, sizeof(file_uri), "file://%s", absolute_path);
                lsp_did_change(file_uri, content, buffer->version);
            }
            free(content);
        }
    }
    buffer_clear_search_state(buffer);
}

void *render_loop(void * arg __attribute__((unused))) {
    struct timespec req = {0};
    while (1) {
        req.tv_sec = 0;
        req.tv_nsec = 3 * 1000 * 1000;
        nanosleep(&req, NULL);
        pthread_mutex_lock(&editor_mutex);
        check_for_resize();
        check_for_redraw_request();
        editor_draw();
        pthread_mutex_unlock(&editor_mutex);
    }
    return NULL;
}

void editor_insert_new_line() {
    pthread_mutex_lock(&editor_mutex);
    BufferLine *current_line = buffer->lines[buffer->position_y];

    uint32_t start_byte = 0;
    for (int i = 0; i < buffer->position_y; i++) {
        for (int j = 0; j < buffer->lines[i]->char_count; j++) {
            start_byte += strlen(buffer->lines[i]->chars[j].value);
        }
        start_byte++; // for newline
    }
    int byte_pos_x = buffer_get_byte_position_x(buffer);
    start_byte += byte_pos_x;

    buffer_realloc_lines_for_capacity(buffer);

    for (int i = buffer->line_count; i > buffer->position_y + 1; i--) {
        buffer->lines[i] = buffer->lines[i - 1];
    }

    buffer->lines[buffer->position_y + 1] = (BufferLine *)malloc(sizeof(BufferLine));
    if (buffer->lines[buffer->position_y + 1] == NULL) {
        log_error("editor.editor_insert_new_line: failed to allocate BufferLine");
        exit(1);
    }
    buffer_line_init(buffer->lines[buffer->position_y + 1]);

    int chars_to_move = current_line->char_count - buffer->position_x;
    if (chars_to_move > 0) {
        buffer_line_realloc_for_capacity(buffer->lines[buffer->position_y + 1], chars_to_move);
        memcpy(buffer->lines[buffer->position_y + 1]->chars,
               &current_line->chars[buffer->position_x],
               chars_to_move * sizeof(Char));
        buffer->lines[buffer->position_y + 1]->char_count = chars_to_move;
        current_line->char_count = buffer->position_x;
    }

    if (buffer->parser) {
        current_line->needs_highlight = 1;
        buffer->lines[buffer->position_y + 1]->needs_highlight = 1;
    }

    buffer->line_count++;
    if (buffer->parser) {
        ts_tree_edit(buffer->tree, &(TSInputEdit){
            .start_byte = start_byte,
            .old_end_byte = start_byte,
            .new_end_byte = start_byte + 1,
            .start_point = { (uint32_t)buffer->position_y, (uint32_t)byte_pos_x },
            .old_end_point = { (uint32_t)buffer->position_y, (uint32_t)byte_pos_x },
            .new_end_point = { (uint32_t)(buffer->position_y + 1), 0 }
        });
    }
    buffer->position_y++;
    buffer_reset_offset_y(buffer, screen_rows);
    buffer->position_x = 0;
    editor_did_change_buffer();
    buffer_set_line_num_width(buffer);

    if (buffer->parser) {
        buffer->needs_parse = 1;
    }

    pthread_mutex_unlock(&editor_mutex);
}

struct termios orig_termios;

void reset_terminal() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    printf("\033[?1049l"); // Return to normal screen
    fflush(stdout);
    theme_destroy();
}

void handle_sigint(int sig) {
    (void)sig;
}

void handle_sigpipe(int sig) {
    (void)sig; // Ignore SIGPIPE
}

#ifndef TEST_BUILD
void setup_terminal() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(reset_terminal);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = handle_sigpipe;
    sigaction(SIGPIPE, &sa, NULL);

    printf("\033[?1049h");
    fflush(stdout);
}
#else
void setup_terminal() {}
#endif

void editor_init(char *file_name) {
    init_terminal_size();
    setup_terminal();
    buffer_capacity = 1;
    buffers = malloc(sizeof(Buffer*) * buffer_capacity);
    if (!buffers) {
        log_error("editor.editor_init: alloc buffers failed");
        exit(1);
    }
    buffer_count = 1;
    active_buffer_idx = 0;
    buffers[0] = (Buffer *)malloc(sizeof(Buffer));
    if (buffers[0] == NULL) {
        log_error("editor.editor_init: alloc current buffer failed");
        exit(1);
    }
    theme_init();
    buffer_init(buffer, file_name);
    if (file_name) {
        editor_handle_input = normal_handle_input;
    } else {
        picker_file_show();
    }
    editor_set_cursor_shape(2);
    buffer_set_line_num_width(buffer);
    config_load(&config);
    config_load_theme(config.theme, &current_theme);
}

void editor_open(char *file_name) {
    pthread_mutex_lock(&editor_mutex);

    if (buffer_count == 1 && buffers[0]->file_name == NULL) {
        buffer_destroy(buffers[0]);
        buffer_init(buffers[0], file_name);
        buffer_set_line_num_width(buffer);
        editor_handle_input = normal_handle_input;
        
        char absolute_path[PATH_MAX];
        if (realpath(file_name, absolute_path) != NULL) {
            char *content = buffer_get_content(buffer);
            if (content) {
                char file_uri[PATH_MAX + 7];
                snprintf(file_uri, sizeof(file_uri), "file://%s", absolute_path);
                lsp_did_open(file_uri, "c", content);
                free(content);
            }
        }

        pthread_mutex_unlock(&editor_mutex);
        return;
    }

    if (buffer_count == buffer_capacity) {
        buffer_capacity *= 2;
        buffers = realloc(buffers, sizeof(Buffer*) * buffer_capacity);
        if (!buffers) {
            log_error("editor.editor_open: realloc buffers failed");
            exit(1);
        }
    }
    buffer_count++;
    active_buffer_idx = buffer_count - 1;
    buffers[active_buffer_idx] = (Buffer *)malloc(sizeof(Buffer));
    if (buffers[active_buffer_idx] == NULL) {
        log_error("editor.editor_open: alloc current buffer failed");
        exit(1);
    }
    buffer_init(buffer, file_name);
    buffer_set_line_num_width(buffer);
    editor_handle_input = normal_handle_input;

    char absolute_path[PATH_MAX];
    if (realpath(file_name, absolute_path) != NULL) {
        char *content = buffer_get_content(buffer);
        if (content) {
            char file_uri[PATH_MAX + 7];
            snprintf(file_uri, sizeof(file_uri), "file://%s", absolute_path);
            lsp_did_open(file_uri, "c", content);
            free(content);
        }
    }

    pthread_mutex_unlock(&editor_mutex);
}

Buffer **editor_get_buffers(int *count) {
    *count = buffer_count;
    return buffers;
}

void editor_set_active_buffer(int index) {
    if (index >= 0 && index < buffer_count) {
        active_buffer_idx = index;
    }
    editor_handle_input = normal_handle_input;
    buffer->needs_draw = 1;
}

void editor_close_buffer(int buffer_index) {
    pthread_mutex_lock(&editor_mutex);
    if (buffer_index < 0 || buffer_index >= buffer_count) {
        pthread_mutex_unlock(&editor_mutex);
        return;
    }

    buffer_destroy(buffers[buffer_index]);
    free(buffers[buffer_index]);

    for (int i = buffer_index; i < buffer_count - 1; i++) {
        buffers[i] = buffers[i + 1];
    }
    buffer_count--;

    if (buffer_count == 0) {
        buffer_capacity = 1;
        buffers = realloc(buffers, sizeof(Buffer*) * buffer_capacity);
        buffers[0] = (Buffer *)malloc(sizeof(Buffer));
        buffer_init(buffers[0], NULL);
        buffer_count = 1;
        active_buffer_idx = 0;
        buffer->needs_draw = 1;
        pthread_mutex_unlock(&editor_mutex);
        picker_file_show();
        return;
    } else if (active_buffer_idx >= buffer_index) {
        if (active_buffer_idx > 0) {
            active_buffer_idx--;
        }
    }
    
    buffer->needs_draw = 1;
    pthread_mutex_unlock(&editor_mutex);
}

int editor_get_active_buffer_idx(void) {
    return active_buffer_idx;
}

void editor_start(char *file_name) {
    editor_init(file_name);
    pthread_t render_thread_id;
    editor_mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    if (pthread_create(&render_thread_id, NULL, render_loop, NULL) != 0) {
        log_error("editor.editor_start: unable to create render thread");
        exit(1);
    }
    char utf8_buf[8];
    while (read_utf8_char_from_stdin(utf8_buf, sizeof(utf8_buf)) > 0) {
        if (!editor_handle_input(utf8_buf)) {
            break;
        }
    }
    editor_clear_screen();
}

void editor_insert_char(const char *ch) {
    pthread_mutex_lock(&editor_mutex);
    BufferLine *line = buffer->lines[buffer->position_y];
    buffer_line_realloc_for_capacity(line, line->char_count + 1);

    unsigned char new_style = 0;
    if (buffer->parser && buffer->tree && buffer->position_x > 0 && buffer->position_x < line->char_count) {
        Char *prev_char = &line->chars[buffer->position_x - 1];
        Char *next_char = &line->chars[buffer->position_x];

        if ((prev_char->style & STYLE_UNDERLINE) && (next_char->style & STYLE_UNDERLINE)) {
            TSNode root_node = ts_tree_root_node(buffer->tree);
            TSPoint prev_point = {(uint32_t)buffer->position_y, (uint32_t)buffer->position_x - 1};
            TSPoint next_point = {(uint32_t)buffer->position_y, (uint32_t)buffer->position_x};
            
            TSNode prev_leaf = ts_node_named_descendant_for_point_range(root_node, prev_point, prev_point);
            TSNode next_leaf = ts_node_named_descendant_for_point_range(root_node, next_point, next_point);

            if (!ts_node_is_null(prev_leaf) && ts_node_eq(prev_leaf, next_leaf)) {
                new_style |= STYLE_UNDERLINE;
            }
        }
    }

    Char new;
    strncpy(new.value, ch, sizeof(new.value));
    new.width = utf8_char_width(ch);
    new.style = new_style;
    new.r = 255; new.g = 255; new.b = 255;

    if (buffer->position_x < line->char_count) {
        memmove(&line->chars[buffer->position_x + 1],
                &line->chars[buffer->position_x],
                (line->char_count - buffer->position_x) * sizeof(Char));
    }

    line->chars[buffer->position_x] = new;
    line->char_count++;
    buffer->position_x++;
    buffer_reset_offset_x(buffer, screen_cols);
    editor_did_change_buffer();

    if (buffer->parser) {
        uint32_t start_byte = 0;
        for (int i = 0; i < buffer->position_y; i++) {
            for (int j = 0; j < buffer->lines[i]->char_count; j++) {
                start_byte += strlen(buffer->lines[i]->chars[j].value);
            }
            start_byte++; // for newline
        }
        int byte_pos_x = buffer_get_byte_position_x(buffer);
        start_byte += byte_pos_x - strlen(ch);

        ts_tree_edit(buffer->tree, &(TSInputEdit){
            .start_byte = start_byte,
            .old_end_byte = start_byte,
            .new_end_byte = start_byte + strlen(ch),
            .start_point = {(uint32_t)buffer->position_y, (uint32_t)(byte_pos_x - strlen(ch))},
            .old_end_point = {(uint32_t)buffer->position_y, (uint32_t)(byte_pos_x - strlen(ch))},
            .new_end_point = {(uint32_t)buffer->position_y, (uint32_t)byte_pos_x}
        });
        line->needs_highlight = 1;
        buffer->needs_parse = 1;
    }
    pthread_mutex_unlock(&editor_mutex);
}

void editor_move_cursor_right() {
    pthread_mutex_lock(&editor_mutex);
    BufferLine *line = buffer->lines[buffer->position_y];
    if (buffer->position_y == buffer->line_count - 1 &&
        buffer->position_x >= line->char_count) {
        pthread_mutex_unlock(&editor_mutex);
        return;
    }
    if ((buffer->position_x >= line->char_count) && (buffer->position_y != buffer->line_count - 1)) {
        buffer->position_x = 0;
        buffer->position_y++;
        buffer_reset_offset_y(buffer, screen_rows);
        buffer->offset_x = 0;
    } else {
        buffer_move_position_right(buffer, screen_cols);
    }
    buffer->needs_draw = 1;
    pthread_mutex_unlock(&editor_mutex);
}


void editor_move_cursor_left() {
    pthread_mutex_lock(&editor_mutex);
    BufferLine *line = buffer->lines[buffer->position_y];
    if (buffer->position_x == 0) {
        if (buffer->position_y == 0) {
            pthread_mutex_unlock(&editor_mutex);
            return;
        }
        buffer->position_y--;
        buffer_reset_offset_y(buffer, screen_rows);
        line = buffer->lines[buffer->position_y];
        buffer->position_x = line->char_count + 1;
        buffer_reset_offset_x(buffer, screen_cols);
    }
    buffer_move_position_left(buffer);
    buffer->needs_draw = 1;
    pthread_mutex_unlock(&editor_mutex);
}

void editor_move_cursor_down() {
    pthread_mutex_lock(&editor_mutex);
    if (buffer->position_y == buffer->line_count - 1) {
        pthread_mutex_unlock(&editor_mutex);
        return;
    }
    int visual_x = buffer_get_visual_position_x(buffer);
    buffer->position_y++;
    buffer_reset_offset_y(buffer, screen_rows);
    buffer_set_logical_position_x(buffer, visual_x);
    buffer_reset_offset_x(buffer, screen_cols);
    buffer->needs_draw = 1;
    pthread_mutex_unlock(&editor_mutex);
}

void editor_move_cursor_up() {
    pthread_mutex_lock(&editor_mutex);
    if (buffer->position_y == 0) {
        pthread_mutex_unlock(&editor_mutex);
        return;
    }
    int visual_x = buffer_get_visual_position_x(buffer);
    buffer->position_y--;
    buffer_reset_offset_y(buffer, screen_rows);
    buffer_set_logical_position_x(buffer, visual_x);
    buffer_reset_offset_x(buffer, screen_cols);
    buffer->needs_draw = 1;
    pthread_mutex_unlock(&editor_mutex);
}

void editor_write() {
    pthread_mutex_lock(&editor_mutex);
    if (buffer->file_name == NULL) {
        pthread_mutex_unlock(&editor_mutex);
        return;
    }
    FILE *fp = fopen(buffer->file_name, "w");
    if (fp == NULL) {
        log_error("editor.editor_write: failed to open file for writing");
        pthread_mutex_unlock(&editor_mutex);
        return;
    }
    for (int line_idx = 0; line_idx < buffer->line_count; line_idx++) {
        BufferLine *line = buffer->lines[line_idx];
        for (int char_idx = 0; char_idx < line->char_count; char_idx++) {
            fputs(line->chars[char_idx].value, fp);
        }
        if (line_idx < buffer->line_count - 1) {
            fputc('\n', fp);
        }
    }
    fclose(fp);
    buffer->dirty = 0;
    buffer->needs_draw = 1;
    pthread_mutex_unlock(&editor_mutex);
}

void editor_delete() {
    pthread_mutex_lock(&editor_mutex);
    BufferLine *line = buffer->lines[buffer->position_y];

    uint32_t start_byte = 0;
    for (int i = 0; i < buffer->position_y; i++) {
        for (int j = 0; j < buffer->lines[i]->char_count; j++) {
            start_byte += strlen(buffer->lines[i]->chars[j].value);
        }
        start_byte++; // for newline
    }
    int byte_pos_x = buffer_get_byte_position_x(buffer);
    start_byte += byte_pos_x;

    if (buffer->position_x == line->char_count) {
        if (buffer->position_y == buffer->line_count - 1) {
            pthread_mutex_unlock(&editor_mutex);
            return;
        }
        if (buffer->parser) {
            BufferLine *next_line = buffer->lines[buffer->position_y + 1];
            next_line->needs_highlight = 1;
        }
        BufferLine *next_line = buffer->lines[buffer->position_y + 1];
        int new_char_count = line->char_count + next_line->char_count;
        buffer_line_realloc_for_capacity(line, new_char_count);
        memcpy(&line->chars[line->char_count], next_line->chars, next_line->char_count * sizeof(Char));
        line->char_count = new_char_count;
        buffer_line_destroy(next_line);

        for (int i = buffer->position_y + 1; i < buffer->line_count - 1; i++) {
            buffer->lines[i] = buffer->lines[i + 1];
        }
        buffer->line_count--;
        buffer_set_line_num_width(buffer);
        if (buffer->parser) {
            ts_tree_edit(buffer->tree, &(TSInputEdit){
                .start_byte = start_byte,
                .old_end_byte = start_byte + 1,
                .new_end_byte = start_byte,
                .start_point = { (uint32_t)buffer->position_y, (uint32_t)byte_pos_x },
                .old_end_point = { (uint32_t)(buffer->position_y + 1), 0 },
                .new_end_point = { (uint32_t)buffer->position_y, (uint32_t)byte_pos_x }
            });
        }
    } else {
        Char deleted_char = line->chars[buffer->position_x];
        memmove(&line->chars[buffer->position_x],
                &line->chars[buffer->position_x + 1],
                (line->char_count - buffer->position_x - 1) * sizeof(Char));
        line->char_count--;
        if (buffer->parser) {
            line->needs_highlight = 1;
            ts_tree_edit(buffer->tree, &(TSInputEdit){
                .start_byte = start_byte,
                .old_end_byte = start_byte + strlen(deleted_char.value),
                .new_end_byte = start_byte,
                .start_point = { (uint32_t)buffer->position_y, (uint32_t)byte_pos_x },
                .old_end_point = { (uint32_t)buffer->position_y, (uint32_t)(byte_pos_x + strlen(deleted_char.value)) },
                .new_end_point = { (uint32_t)buffer->position_y, (uint32_t)byte_pos_x }
            });
        }
    }

    editor_did_change_buffer();
    pthread_mutex_unlock(&editor_mutex);
}

void editor_backspace() {
    pthread_mutex_lock(&editor_mutex);
    BufferLine *line = buffer->lines[buffer->position_y];

    uint32_t start_byte = 0;
    for (int i = 0; i < buffer->position_y; i++) {
        for (int j = 0; j < buffer->lines[i]->char_count; j++) {
            start_byte += strlen(buffer->lines[i]->chars[j].value);
        }
        start_byte++; // for newline
    }
    int byte_pos_x = buffer_get_byte_position_x(buffer);
    start_byte += byte_pos_x;

    if (buffer->position_x == 0) {
        if (buffer->position_y == 0) {
            pthread_mutex_unlock(&editor_mutex);
            return;
        }
        BufferLine *prev_line = buffer->lines[buffer->position_y - 1];
        int new_char_count = prev_line->char_count + line->char_count;
        buffer_line_realloc_for_capacity(prev_line, new_char_count);
        memcpy(&prev_line->chars[prev_line->char_count], line->chars, line->char_count * sizeof(Char));
        prev_line->char_count = new_char_count;
        if (buffer->parser) {
            prev_line->needs_highlight = 1;
        }

        for (int i = buffer->position_y; i < buffer->line_count - 1; i++) {
            buffer->lines[i] = buffer->lines[i + 1];
        }
        buffer->line_count--;
        buffer_set_line_num_width(buffer);
        if (buffer->parser) {
            int prev_line_byte_len = 0;
            for (int i = 0; i < prev_line->char_count; i++) {
                prev_line_byte_len += strlen(prev_line->chars[i].value);
            }
            ts_tree_edit(buffer->tree, &(TSInputEdit){
                .start_byte = start_byte - 1,
                .old_end_byte = start_byte,
                .new_end_byte = start_byte - 1,
                .start_point = { (uint32_t)buffer->position_y, 0 },
                .old_end_point = { (uint32_t)buffer->position_y, 0 },
                .new_end_point = { (uint32_t)(buffer->position_y - 1), (uint32_t)prev_line_byte_len }
            });
        }
        buffer->position_y--;
        buffer_reset_offset_y(buffer, screen_rows);
        buffer->position_x = buffer->lines[buffer->position_y]->char_count - line->char_count;
        buffer_line_destroy(line);

    } else {
        Char deleted_char = line->chars[buffer->position_x - 1];
        memmove(&line->chars[buffer->position_x - 1],
                &line->chars[buffer->position_x],
                (line->char_count - buffer->position_x) * sizeof(Char));
        line->char_count--;
        buffer->position_x--;
        if (buffer->parser) {
            line->needs_highlight = 1;
            ts_tree_edit(buffer->tree, &(TSInputEdit){
                .start_byte = start_byte - strlen(deleted_char.value),
                .old_end_byte = start_byte,
                .new_end_byte = start_byte - strlen(deleted_char.value),
                .start_point = { (uint32_t)buffer->position_y, (uint32_t)(byte_pos_x - strlen(deleted_char.value)) },
                .old_end_point = { (uint32_t)buffer->position_y, (uint32_t)byte_pos_x },
                .new_end_point = { (uint32_t)buffer->position_y, (uint32_t)(byte_pos_x - strlen(deleted_char.value)) }
             });
        }
    }

    editor_did_change_buffer();
    pthread_mutex_unlock(&editor_mutex);
}

void editor_move_n_lines_down(int n) {
    pthread_mutex_lock(&editor_mutex);
    int cnt;
    if (n <= 0) {
        cnt = screen_rows / 2;
    } else {
        cnt = n;
    }
    int remaining = buffer->line_count - 1 - buffer->position_y;
    if (remaining < cnt) cnt = remaining;
    int visual_x = buffer_get_visual_position_x(buffer);
    buffer->position_y += cnt;
    buffer->offset_y += cnt;
    buffer_reset_offset_y(buffer, screen_rows);
    buffer_set_logical_position_x(buffer, visual_x);
    buffer_reset_offset_x(buffer, screen_cols);
    buffer->needs_draw = 1;
    pthread_mutex_unlock(&editor_mutex);
}

void editor_move_n_lines_up(int n) {
    pthread_mutex_lock(&editor_mutex);
    int cnt;
    if (n <= 0) {
        cnt = screen_rows / 2;
    } else {
        cnt = n;
    }
    if (buffer->position_y < cnt) cnt = buffer->position_y;
    int visual_x = buffer_get_visual_position_x(buffer);
    buffer->position_y -= cnt;
    buffer->offset_y -= cnt;
    buffer_reset_offset_y(buffer, screen_rows);
    buffer_set_logical_position_x(buffer, visual_x);
    buffer_reset_offset_x(buffer, screen_cols);
    buffer->needs_draw = 1;
    pthread_mutex_unlock(&editor_mutex);
}

int is_word_char(char ch) {
    return (ch >= 'a' && ch <= 'z') || 
           (ch >= 'A' && ch <= 'Z') || 
           (ch >= '0' && ch <= '9') || 
           ch == '_';
}

int is_whitespace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n';
}

int range_expand_right(BufferLine **line, Range *range) {
    if (range->x_end >= (*line)->char_count - 1) {
        if (range->y_end == buffer->line_count - 1) {
            return 0;
        }
        range->y_end++;
        *line = buffer->lines[range->y_end];
        range->x_end = 0;
        return 1;
    }
    range->x_end++;
    return 0;
}

int range_expand_left(BufferLine **line, Range *range) {
    if (range->x_end == 0) {
        if (range->y_end == 0) {
            return 0;
        }
        range->y_end--;
        *line = buffer->lines[range->y_end];
        range->x_end = (*line)->char_count - 1;
        if (range->x_end < 0) range->x_end = 0;
        return 1;
    }
    range->x_end--;
    return 0;
}

int range_expand_down(BufferLine **line, Range *range) {
    if (range->y_end == buffer->line_count - 1) {
        return 0;
    }
    range->y_end++;
    *line = buffer->lines[range->y_end];
    range->x_end = (*line)->char_count - 1;
    return 1;
}


int range_expand_up(BufferLine **line, Range *range) {
    if (range->y_end == 0) {
        return 0;
    }
    range->y_end--;
    *line = buffer->lines[range->y_end];
    range->x_end = 0;
    return 1;
}

void range_expand_e(BufferLine *line, int count, Range *range) {
    while (count) {
        count--;
        int moved = 0;
        while (range_expand_right(&line, range)) {
            moved = 1;
            if (line->char_count) {
                break;
            }
        }
        if (!line->char_count) {
            continue;
        }
        while (is_whitespace(line->chars[range->x_end].value[0])) {
            range_expand_right(&line, range);
            moved = 1;
        }
        int touched = 0;
        if (!is_word_char(line->chars[range->x_end].value[0])) {
            if (moved) {
                continue;
            }
            while (range->x_end < line->char_count - 1 && !is_word_char(line->chars[range->x_end + 1].value[0]) && !is_whitespace(line->chars[range->x_end + 1].value[0])) {
                touched = 1;
                range_expand_right(&line, range);
            }
        }
        if (touched) {
            continue;
        }
        if (!is_word_char(line->chars[range->x_end].value[0])) {
            continue;
        }
        while (range->x_end < line->char_count - 1 && is_word_char(line->chars[range->x_end + 1].value[0])) {
            range_expand_right(&line, range);
        }
    }
}

void range_expand_E(BufferLine *line, int count, Range *range) {
    while (count) {
        count--;
        while (range_expand_right(&line, range)) {
            if (!line->char_count) {
                break;
            }
        }
        if (!line->char_count) {
            continue;
        }
        while (is_whitespace(line->chars[range->x_end].value[0])) {
            range_expand_right(&line, range);
        }
        while (range->x_end < line->char_count - 1 && !is_whitespace(line->chars[range->x_end + 1].value[0])) {
            range_expand_right(&line, range);
        }
    }
}

void range_expand_b(BufferLine *line, int count, Range *range) {
    while (count) {
        count--;
        while (range_expand_left(&line, range)) {
            if (!line->char_count) {
                break;
            }
        }
        if (!line->char_count) {
            continue;
        }
        while (is_whitespace(line->chars[range->x_end].value[0])) {
            range_expand_left(&line, range);
        }
        int touched = 0;
        if (!is_word_char(line->chars[range->x_end].value[0])) {
            while (range->x_end > 0 && !is_word_char(line->chars[range->x_end - 1].value[0]) && !is_whitespace(line->chars[range->x_end - 1].value[0])) {
                touched = 1;
                range_expand_left(&line, range);
            }
        }
        if (touched) {
            continue;
        }
        if (!is_word_char(line->chars[range->x_end].value[0])) {
            continue;
        }
        while (range->x_end > 0 && is_word_char(line->chars[range->x_end - 1].value[0])) {
            range_expand_left(&line, range);
        }
    }
}

void range_expand_B(BufferLine *line, int count, Range *range) {
    while (count) {
        count--;
        while (range_expand_left(&line, range) && !line->char_count) {
            if (!line->char_count) {
                break;
            }
        }
        while (is_whitespace(line->chars[range->x_end].value[0])) {
            range_expand_left(&line, range);
        }
        while (range->x_end > 0 && !is_whitespace(line->chars[range->x_end - 1].value[0])) {
            range_expand_left(&line, range);
        }
    }
}

static void get_target_range(EditorCommand *cmd, Range *range) {
    if (editor_handle_input == visual_handle_input && cmd->action) {
        range->x_start = buffer->selection_start_x;
        range->y_start = buffer->selection_start_y;
        range->x_end = buffer->position_x;
        range->y_end = buffer->position_y;
        if (range->y_start == range->y_end) {
            if (range->x_end >= range->x_start) {
                range->x_end++;
            } else {
                range->x_start++;
            }
        } else if (range->y_end > range->y_start) {
            if (buffer->lines[range->y_end]->char_count > 0) {
                range->x_end++;
            }
        } else {
            if (buffer->lines[range->y_start]->char_count > 0) {
                range->x_start++;
            }
        }
        return;
    }

    BufferLine *line = buffer->lines[buffer->position_y];
    int count = cmd->count ? cmd->count : 1;
    range->x_start = buffer->position_x;
    range->y_start = buffer->position_y;
    range->x_end = buffer->position_x;
    range->y_end = buffer->position_y;
    if (cmd->target && line->char_count && buffer->position_x == line->char_count) {
        // range->x_start--;
        // range->x_end--;
    }
    switch (cmd->target) {
        case 'n':
            if (strcmp(cmd->specifier, "p") == 0) {
                int y = buffer->position_y;
                int count = cmd->count ? cmd->count : 1;
                for (int i = 0; i < count; i++) {
                    // find next empty line
                    while (y < buffer->line_count - 1 && !is_line_empty(buffer->lines[y])) {
                        y++;
                    }
                    // find next non-empty line
                    while (y < buffer->line_count - 1 && is_line_empty(buffer->lines[y])) {
                        y++;
                    }
                }

                range->y_end = y;
                range->x_end = 0;
            }
            break;
        case 'p':
            if (cmd->action == 0) {
                if (strcmp(cmd->specifier, "p") == 0) {
                    int y = buffer->position_y;
                    int count = cmd->count ? cmd->count : 1;
                    for (int i = 0; i < count; i++) {
                        int start_of_current = y;
                        while (start_of_current > 0 && !is_line_empty(buffer->lines[start_of_current - 1])) {
                            start_of_current--;
                        }
                        if (y != start_of_current && i == 0) {
                            y = start_of_current;
                        } else {
                            if (y > 0) y--;
                            while (y > 0 && is_line_empty(buffer->lines[y])) {
                                y--;
                            }
                            while (y > 0 && !is_line_empty(buffer->lines[y-1])) {
                                y--;
                            }
                        }
                    }
                    range->y_end = y;
                    range->x_end = 0;
                }
            } else {
                int original_y = range->y_start;

                if (is_line_empty(buffer->lines[original_y])) {
                    range->y_start = original_y;
                    range->y_end = original_y;
                } else {
                    while (range->y_start > 0 && !is_line_empty(buffer->lines[range->y_start - 1])) {
                        range->y_start--;
                    }

                    while (range->y_end < buffer->line_count - 1 && !is_line_empty(buffer->lines[range->y_end + 1])) {
                        range->y_end++;
                    }
                }

                if (range->y_end < buffer->line_count - 1 && is_line_empty(buffer->lines[range->y_end + 1])) {
                    range->y_end++;
                }

                range->x_start = 0;
                range->x_end = buffer->lines[range->y_end]->char_count;
            }
            break;
        case 'w':
            if (cmd->action) {
                if (!line->char_count || is_whitespace(line->chars[range->x_end].value[0])) {
                    return;
                }
                if (range->x_start && is_word_char(line->chars[range->x_start - 1].value[0])) {
                    range_expand_b(line, count, range);
                    int tmp = range->x_end;
                    range->x_end = range->x_start;
                    range->x_start = tmp;
                }
                range_expand_e(line, count, range);
                range->x_end++;
                break;
            }
            while (count) {
                count--;
                if (range->x_end >= line->char_count - 1) {
                    range_expand_right(&line, range);
                    if (line->char_count) {
                        while (is_whitespace(line->chars[range->x_end].value[0])) {
                            range_expand_right(&line, range);
                        }
                    }
                    continue;
                }
                int touched = 0;
                while (!is_word_char(line->chars[range->x_end].value[0]) && !is_whitespace(line->chars[range->x_end].value[0])) {
                    touched = 1;
                    if (range_expand_right(&line, range)) break;
                }
                if (!touched) {
                    while (is_word_char(line->chars[range->x_end].value[0])) {
                        if (range_expand_right(&line, range)) break;
                    }
                }

                while (range->x_end < line->char_count && is_whitespace(line->chars[range->x_end].value[0])) {
                    if (range_expand_right(&line, range) && !line->char_count) break;
                }
            }
            break;
        case 'W':
            if (cmd->action) {
                if (!line->char_count || is_whitespace(line->chars[range->x_end].value[0])) {
                    return;
                }
                range_expand_B(line, count, range);
                int tmp = range->x_end;
                range->x_end = range->x_start;
                range->x_start = tmp;
                range_expand_E(line, count, range);
                range->x_end++;
                break;
            }
            while (count) {
                count--;
                if (range->x_end >= line->char_count - 1) {
                    range_expand_right(&line, range);
                    if (line->char_count) {
                        while (is_whitespace(line->chars[range->x_end].value[0])) {
                            range_expand_right(&line, range);
                        }
                    }
                    continue;
                }
                while (!is_whitespace(line->chars[range->x_end].value[0])) {
                    if (range_expand_right(&line, range)) break;
                }

                while (range->x_end < line->char_count && is_whitespace(line->chars[range->x_end].value[0])) {
                    if (range_expand_right(&line, range) && !line->char_count) break;
                }
            }
            break;
        case 'e':
            if (cmd->action == 'g') {
                line = buffer->lines[buffer->line_count - 1];
                range->x_end = line->char_count;
                range->y_end = buffer->line_count - 1;                
            } else {
                range_expand_e(line, count, range);
                if (cmd->action) {
                    range->x_end++;
                }
            }
            break;
        case 'E':
            range_expand_E(line, count, range);
            range->x_end++;
            break;
        case 'b':
            if (cmd->action == 'g') {
                line = buffer->lines[0];
                range->x_end = 0;
                range->y_end = 0;                
            } else {
                range_expand_b(line, count, range);
            }
            break;
        case 'B':
            range_expand_B(line, count, range);
            break;
        case 'l':
            if (range->x_end < line->char_count) {
                range->x_end = line->char_count;
            }
            break;
        case 'h':
            range->x_end = 0;
            break;
        case 'f':
            for (int i = 0; i < count; i++) {
                int found = 0;
                for (int j = range->x_end + 1; j < line->char_count; j++) {
                    if (strcmp(line->chars[j].value, cmd->specifier) == 0) {
                        range->x_end = j;
                        found = 1;
                        break;
                    }
                }
                if (!found) break;
            }
            break;
        case 'F':
            for (int i = 0; i < count; i++) {
                int found = 0;
                for (int j = range->x_end - 1; j >= 0; j--) {
                    if (strcmp(line->chars[j].value, cmd->specifier) == 0) {
                        range->x_end = j;
                        found = 1;
                        break;
                    }
                }
                if (!found) break;
            }
            break;
        case 't':
            for (int i = 0; i < count; i++) {
                int found = 0;
                for (int j = range->x_end + 2; j < line->char_count; j++) {
                    if (strcmp(line->chars[j].value, cmd->specifier) == 0) {
                        range->x_end = j - 1;
                        found = 1;
                        break;
                    }
                }
                if (!found) break;
            }
            break;
        case 'T':
            for (int i = 0; i < count; i++) {
                int found = 0;
                for (int j = range->x_end - 2; j >= 0; j--) {
                    if (strcmp(line->chars[j].value, cmd->specifier) == 0) {
                        range->x_end = j + 1;
                        found = 1;
                        break;
                    }
                }
                if (!found) break;
            }
            break;
    }
}

int range_get_left_boundary(Range *range) {
    if (range->y_start < range->y_end) {
        return range->x_start;
    } else if (range->y_end < range->y_start) {
        return range->x_end;
    }
    if (range->x_end < range->x_start) {
        return range->x_end;
    }
    return range->x_start;
}

int range_get_right_boundary(Range *range) {
    if (range->y_start < range->y_end) {
        return range->x_end;
    } else if (range->y_end < range->y_start) {
        return range->x_start;
    }
    if (range->x_end > range->x_start) {
        return range->x_end;
    }
    return range->x_start;
}

int range_get_top_boundary(Range *range) {
    if (range->y_end < range->y_start) {
        return range->y_end;
    }
    return range->y_start;
}

int range_get_bottom_boundary(Range *range) {
    if (range->y_end > range->y_start) {
        return range->y_end;
    }
    return range->y_start;
}

void range_delete(Buffer *b, Range *range, EditorCommand *cmd) {
    int left = range_get_left_boundary(range);
    int right = range_get_right_boundary(range);
    int top = range_get_top_boundary(range);
    int bottom = range_get_bottom_boundary(range);
    if (left == right && top == bottom) return;

    if (cmd->target == 'p' && top == bottom) {
        buffer_line_destroy(b->lines[top]);
        memmove(&b->lines[top], &b->lines[top + 1], (b->line_count - top - 1) * sizeof(BufferLine *));
        b->line_count--;
        return;
    }

    if (b->parser) {
        uint32_t start_byte = 0;
        for (int i = 0; i < top; i++) {
            for (int j = 0; j < b->lines[i]->char_count; j++) {
                start_byte += strlen(b->lines[i]->chars[j].value);
            }
            start_byte++; // newline
        }
        uint32_t start_byte_in_line = 0;
        for (int i = 0; i < left; i++) {
            start_byte_in_line += strlen(b->lines[top]->chars[i].value);
        }
        start_byte += start_byte_in_line;

        uint32_t deleted_bytes = 0;
        for (int i = top; i <= bottom; i++) {
            BufferLine *line = b->lines[i];
            int start_j = (i == top) ? left : 0;
            int end_j = (i == bottom) ? right : line->char_count;
            for (int j = start_j; j < end_j; j++) {
                deleted_bytes += strlen(line->chars[j].value);
            }
            if (i < bottom) {
                deleted_bytes++; // newline
            }
        }

        uint32_t end_byte_in_line = 0;
        if (top == bottom) {
            end_byte_in_line = start_byte_in_line + deleted_bytes;
        } else {
            for (int i = 0; i < right; i++) {
                end_byte_in_line += strlen(b->lines[bottom]->chars[i].value);
            }
        }

        ts_tree_edit(b->tree, &(TSInputEdit){
            .start_byte = start_byte,
            .old_end_byte = start_byte + deleted_bytes,
            .new_end_byte = start_byte,
            .start_point = { (uint32_t)top, start_byte_in_line },
            .old_end_point = { (uint32_t)bottom, end_byte_in_line },
            .new_end_point = { (uint32_t)top, start_byte_in_line }
        });
    }

    // trim top line
    int bottom_remaining_chars = b->lines[bottom]->char_count - right;
    int new_char_count = left + bottom_remaining_chars;
    buffer_line_realloc_for_capacity(b->lines[top], new_char_count);
    memmove(&b->lines[top]->chars[left], &b->lines[bottom]->chars[right], bottom_remaining_chars * sizeof(Char));
    b->lines[top]->char_count = new_char_count;

    // destroy all completely removed lines and shift
    if (bottom > top) {
        for (int i = top + 1; i <= bottom; i++) {
            buffer_line_destroy(b->lines[i]);
            free(b->lines[i]);
        }

        int lines_to_remove = bottom - top;
        if (b->line_count > bottom) {
            memmove(&b->lines[top + 1], &b->lines[bottom + 1], (b->line_count - bottom - 1) * sizeof(BufferLine *));
        }
        b->line_count -= lines_to_remove;
    }
}

void editor_search_next(int direction) {
    const char *last_term = search_get_last_term();
    if (last_term && last_term[0] != '\0') {
        int y = buffer->position_y;
        int x = buffer->position_x;
        int found = 0;
        if (direction == 1) {
            if (buffer_find_forward(buffer, last_term, &y, &x)) {
                found = 1;
            }
        } else {
            if (buffer_find_backward(buffer, last_term, &y, &x)) {
                found = 1;
            }
        }

        if (found) {
            buffer->position_y = y;
            buffer->position_x = x;
            editor_center_view();
            if (buffer->search_state.count == 0) {
                buffer_update_search_matches(buffer);
            }
            buffer_update_current_search_match(buffer);
        }
        editor_needs_draw();
    }
}

void editor_command_exec(EditorCommand *cmd) {
    pthread_mutex_lock(&editor_mutex);
    Range range;
    get_target_range(cmd, &range);

    if (editor_handle_input == visual_handle_input && !cmd->action) {
        buffer->position_x = range.x_end;
        buffer->position_y = range.y_end;
        editor_request_redraw();
        pthread_mutex_unlock(&editor_mutex);
        return;
    }

    log_info("get targ range: (%d, %d), (%d, %d)", range.y_start, range.x_start, range.y_end, range.x_end);
    if (cmd->action) {
        switch (cmd->target) {
            case 'f':
            case 't':
                if (range.x_end < buffer->lines[range.y_end]->char_count) {
                    range.x_end++;
                }
                break;
            case 'F':
            case 'T':
                if (range.x_start < buffer->lines[range.y_start]->char_count) {
                    range.x_start++;
                }
                break;
        }
    }
    if (!cmd->action) { // target only
        buffer->position_x = range.x_end;
        buffer->position_y = range.y_end;
        buffer_reset_offset_x(buffer, screen_cols);
        buffer_reset_offset_y(buffer, screen_rows);
        buffer->needs_draw = 1;
        editor_command_reset(cmd);
        pthread_mutex_unlock(&editor_mutex);
        return;
    }
    switch (cmd->action) {
        case 'g':
            buffer->position_x = range.x_end;
            buffer->position_y = range.y_end;
            buffer_reset_offset_x(buffer, screen_cols);
            buffer_reset_offset_y(buffer, screen_rows);
            buffer->needs_draw = 1;
            break;
        case 'c':
        case 'd':
            {
                int left = range_get_left_boundary(&range);
                int right = range_get_right_boundary(&range);
                int top = range_get_top_boundary(&range);
                int bottom = range_get_bottom_boundary(&range);
                buffer->position_x = left;
                buffer->position_y = range_get_top_boundary(&range);
                range_delete(buffer, &range, cmd);
                buffer_reset_offset_x(buffer, screen_cols);
                buffer_reset_offset_y(buffer, screen_rows);
                buffer->needs_draw = 1;
                if (left != right || top != bottom) {
                    editor_did_change_buffer();
                }
            }
            break;
    }
    if (cmd->action == 'c') {
        normal_insertion_registration_init();
        editor_handle_input = insert_handle_input;
    }
    if (editor_handle_input == visual_handle_input) {
        editor_handle_input = normal_handle_input;
    }
    editor_command_reset(cmd);
    pthread_mutex_unlock(&editor_mutex);
}

void editor_center_view(void) {
    buffer_reset_offset_y(buffer, screen_rows);
    buffer_reset_offset_x(buffer, screen_cols);
}

void editor_set_screen_size(int rows, int cols) {
    screen_rows = rows;
    screen_cols = cols;
}

Buffer *editor_get_active_buffer(void) {
    return buffer;
}
