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
#include <stdarg.h>
#include <sys/stat.h>
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
#include "utf8.h"

static pthread_mutex_t editor_mutex = PTHREAD_MUTEX_INITIALIZER;
Editor editor;
static int is_undo_redo_active = 0;

int (*editor_handle_input)(const char *);

#define buffer (editor.buffers[editor.active_buffer_idx])

static int is_in_search_match(int y, int x) {
    Buffer *buf = buffer;
    if (buf->search_state.count == 0) {
        return 0;
    }
    size_t term_len = utf8_strlen(buf->search_state.term);
    for (int i = 0; i < buf->search_state.count; i++) {
        if (buf->search_state.matches[i].y == y) {
            if (x >= buf->search_state.matches[i].x && x < buf->search_state.matches[i].x + (int)term_len) {
                return 1;
            }
        }
    }
    return 0;
}

void editor_command_reset(EditorCommand *cmd) {
    memset(cmd, 0, sizeof(EditorCommand));
}

void editor_set_style(const Style *style, int fg, int bg) {
    if (!style) return;
    printf("\x1b[");
    if (style->style & STYLE_BOLD) printf("1;"); else printf("22;");
    if (style->style & STYLE_ITALIC) printf("3;"); else printf("23;");
    if (style->style & STYLE_UNDERLINE) printf("4;"); else printf("24;");

    if (fg) {
        printf("38;2;%d;%d;%d", style->fg_r, style->fg_g, style->fg_b);
        if (bg) printf(";");
    }
    if (bg) {
        printf("48;2;%d;%d;%d", style->bg_r, style->bg_g, style->bg_b);
    }
    printf("m");
}

void handle_sigwinch(int arg __attribute__((unused))) {
    atomic_store(&editor.resize_requested, 1);
}

#ifndef TEST_BUILD
void init_terminal_size() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        log_error("editor.init_terminal_size: ioctl");
        exit(1);
    }
    editor.screen_rows = ws.ws_row;
    editor.screen_cols = ws.ws_col;

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
    if (atomic_exchange(&editor.resize_requested, 0)) {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
            if (ws.ws_row > 0 && ws.ws_col > 0) {
                editor.screen_rows = ws.ws_row;
                editor.screen_cols = ws.ws_col;
                buffer->needs_draw = 1;
            }
        }
    }
}

void check_for_redraw_request() {
    if (atomic_exchange(&editor.redraw_requested, 0)) {
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
    printf("\x1b[%d;1H", editor.screen_rows);

    const char *mode;
    if (editor_handle_input == insert_handle_input) {
        editor_set_style(&editor.current_theme.statusline_mode_insert, 1, 1);
        mode = " INSERT ";
    } else if (editor_handle_input == visual_handle_input) {
        editor_set_style(&editor.current_theme.statusline_mode_visual, 1, 1);
        mode = " VISUAL ";
    } else if (editor_handle_input == search_handle_input) {
        editor_set_style(&editor.current_theme.statusline_mode_command, 1, 1);
        mode = " COMMAND ";
    } else {
        editor_set_style(&editor.current_theme.statusline_mode_normal, 1, 1);
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
    editor_set_style(&editor.current_theme.statusline_text, 1, 1);

    if (editor_handle_input == search_handle_input) {
        const char *search_term = search_get_term();
        char prompt_char = search_get_prompt_char();
        int search_len = snprintf(NULL, 0, " %c%s", prompt_char, search_term);
        printf(" %c%s", prompt_char, search_term);

        char search_stats[32] = {0};
        int search_stats_len = 0;
        if (buffer->search_state.count > 0) {
            search_stats_len = snprintf(search_stats, sizeof(search_stats), "[%d/%d]", buffer->search_state.current + 1, buffer->search_state.count);
        } else if (search_term[0] != '\0') {
            search_stats_len = snprintf(search_stats, sizeof(search_stats), "[0/0]");
        }

        for (int i = 0; i < editor.screen_cols - mode_len - search_len - search_stats_len; i++) {
            putchar(' ');
        }
        if (search_stats_len > 0) {
            printf("%s", search_stats);
        }
    } else {
        int left_len = mode_len + branch_name_len;
        int right_len = position_len + line_count_len;
        char search_stats[32] = {0};
        int search_stats_len = 0;
        if (buffer->search_state.term && buffer->search_state.term[0] != '\0') {
            if (buffer->search_state.current != -1) {
                search_stats_len = snprintf(search_stats, sizeof(search_stats), "[%d/%d]", buffer->search_state.current + 1, buffer->search_state.count);
            } else {
                search_stats_len = snprintf(search_stats, sizeof(search_stats), "[-/%d]", buffer->search_state.count);
            }
            right_len += search_stats_len + 1;
        }

        int half_cols = editor.screen_cols / 2;
        int half_file_name_len = file_name_len / 2;
        int left_space = half_cols - left_len - half_file_name_len;
        int right_space = (editor.screen_cols - half_cols) - right_len - (file_name_len - half_file_name_len);

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

    if (buf->visual_mode == VISUAL_MODE_LINE) {
        if (start_y > end_y) {
            int tmp = start_y;
            start_y = end_y;
            end_y = tmp;
        }
        return y >= start_y && y <= end_y;
    }

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

typedef struct {
    uint32_t start_byte;
    uint32_t end_byte;
    const char *capture_name;
    int priority;
} HighlightCapture;

const char* find_best_capture_for_position(HighlightCapture *captures, uint32_t capture_count, uint32_t byte_pos) {
    const char *best_capture = NULL;
    int best_priority = -1;

    for (uint32_t i = 0; i < capture_count; i++) {
        HighlightCapture *cap = &captures[i];
        if (byte_pos >= cap->start_byte && byte_pos < cap->end_byte) {
            if (cap->priority > best_priority) {
                best_priority = cap->priority;
                best_capture = cap->capture_name;
            }
        }
        if (cap->start_byte > byte_pos) {
            break;
        }
    }

    return best_capture;
}

int get_capture_priority(const char* capture_name) {
    const CaptureInfo* info = theme_get_capture_info(capture_name);
    if (info) {
        return info->priority;
    }
    if (capture_name) {
        log_warning("buffer.get_capture_priority: unrecognized capture_name %s", capture_name);
    }
    return 0;
}

void draw_buffer(Diagnostic *diagnostics, int diagnostics_count, int update_diagnostics) {
    uint32_t start_byte = 0;
    for (int i = 0; i < buffer->offset_y; i++) {
        start_byte += buffer->lines[i]->byte_count + 1;
    }

    for (int row = buffer->offset_y; row < buffer->offset_y + editor.screen_rows - 1; row++) {
        int relative_y = row - buffer->offset_y;
        printf("\x1b[%d;1H", relative_y + 1);
        char line_num_str[16];
        int line_num_len = snprintf(line_num_str, sizeof(line_num_str), "%*d ", buffer->line_num_width - 1, row + 1);

        if (row >= buffer->line_count) {
            editor_set_style(&editor.current_theme.content_background, 0, 1);
            for (int i = 0; i < editor.screen_cols; i++) {
                putchar(' ');
            }
            continue;
        }

        BufferLine *line = buffer->lines[row];
        HighlightCapture *captures = NULL;
        uint32_t capture_count = 0;

        if (buffer->cursor) {
            ts_query_cursor_set_byte_range(buffer->cursor, start_byte, start_byte + line->byte_count);
            ts_query_cursor_exec(buffer->cursor, buffer->query, buffer->root);
            TSQueryMatch match;
            uint32_t capture_index;
            uint32_t capture_capacity = 0;
            while (ts_query_cursor_next_capture(buffer->cursor, &match, &capture_index)) {
                TSQueryCapture capture = match.captures[capture_index];
                if (capture_count >= capture_capacity) {
                    capture_capacity = capture_capacity ? capture_capacity * 2 : 16;
                    captures = realloc(captures, capture_capacity * sizeof(HighlightCapture));
                }
                uint32_t capture_name_length;
                const char *capture_name = ts_query_capture_name_for_id(buffer->query, capture.index, &capture_name_length);
                captures[capture_count] = (HighlightCapture){ts_node_start_byte(capture.node), ts_node_end_byte(capture.node), capture_name, get_capture_priority(capture_name)};
                capture_count++;
            }
        }

        if (buffer->offset_x) {
            editor_set_style(&editor.current_theme.content_line_number_sticky, 1, 1);
        } else if (row == buffer->position_y) {
            editor_set_style(&editor.current_theme.content_line_number_active, 1, 1);
        } else {
            editor_set_style(&editor.current_theme.content_line_number, 1, 1);
        }
        printf("%.*s", line_num_len, line_num_str);

        const Style *line_style = (row == buffer->position_y) ? &editor.current_theme.content_cursor_line : &editor.current_theme.content_background;
        editor_set_style(line_style, 0, 1);

        int cols_to_skip = buffer->offset_x;
        int chars_to_print = editor.screen_cols - buffer->line_num_width;
        char *ptr = line->chars;
        int current_byte_in_line = 0;

        for (int ch_idx = 0; ch_idx < line->char_count && chars_to_print > 0; ch_idx++) {
            const Style *style = line_style;
            if (editor_handle_input == visual_handle_input && is_in_selection(row, ch_idx)) {
                style = &editor.current_theme.content_selection;
            } else if (is_in_search_match(row, ch_idx)) {
                style = &editor.current_theme.search_match;
            }

            int char_len = utf8_char_len(ptr);
            char temp_char[5] = {0};
            strncpy(temp_char, ptr, char_len);

            if (*ptr == '\t') {
                int width = buffer->tab_width - (cols_to_skip % buffer->tab_width);
                cols_to_skip = 0;
                editor_set_style(style, 0, 1);
                for (int i = 0; i < width && chars_to_print > 0; i++) {
                    printf(" ");
                    chars_to_print--;
                }
            } else {
                if (cols_to_skip) {
                    cols_to_skip--;
                } else {
                    const char *capture_name = find_best_capture_for_position(captures, capture_count, start_byte + current_byte_in_line);
                    Style char_style = *theme_get_capture_style(capture_name, &editor.current_theme);
                    char_style.bg_r = style->bg_r;
                    char_style.bg_g = style->bg_g;
                    char_style.bg_b = style->bg_g;

                    if (update_diagnostics) {
                        for (int i = 0; i < diagnostics_count; i++) {
                            if (diagnostics[i].line == row && ch_idx >= diagnostics[i].col_start && ch_idx < diagnostics[i].col_end) {
                                char_style.style |= STYLE_UNDERLINE;
                                break;
                            }
                        }
                    }

                    editor_set_style(&char_style, 1, 1);
                    printf("%s", temp_char);
                    chars_to_print--;
                }
            }
            ptr += char_len;
            current_byte_in_line += char_len;
        }

        editor_set_style(line_style, 1, 1);
        while (chars_to_print > 0) {
            putchar(' ');
            chars_to_print--;
        }

        if (captures) {
            free(captures);
        }
        start_byte += line->byte_count + 1;
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
            ui_draw_popup(&editor.current_theme, d.severity, d.message, y, editor.screen_cols, editor.screen_rows);
            return;
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
    if (editor_handle_input == normal_handle_input) {
        draw_diagnostics(diagnostics, diagnostic_count);
    }
    draw_cursor();
    if (picker_is_open()) {
        picker_draw(editor.screen_cols, editor.screen_rows, &editor.current_theme);
    }
    if (diagnostics) {
        for (int i = 0; i < diagnostic_count; i++) {
            free(diagnostics[i].message);
        }
        free(diagnostics);
    }
    fflush(stdout);
    buffer->needs_draw = 0;
}

void editor_request_redraw(void) {
    atomic_store(&editor.redraw_requested, 1);
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

    if (buffer->file_name && lsp_is_running()) {
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

static void calculate_end_point(const char *text, int start_y, int start_x, int *end_y, int *end_x);

static void editor_add_insertion_to_history(const char* text) {
    if (is_undo_redo_active) return;

    History *h = buffer->history;
    if (h->is_coalescing) {
        Change *last_change = h->undo_stack.head;
        if (last_change && last_change->type == CHANGE_TYPE_INSERT) {
            int end_y, end_x;
            calculate_end_point(last_change->text, last_change->y, last_change->x, &end_y, &end_x);
            if (end_y == buffer->position_y && end_x == buffer->position_x) {
                // Coalesce
                size_t old_len = strlen(last_change->text);
                size_t new_text_len = strlen(text);
                char *new_full_text = realloc(last_change->text, old_len + new_text_len + 1);
                if (new_full_text) {
                    memcpy(new_full_text + old_len, text, new_text_len);
                    new_full_text[old_len + new_text_len] = '\0';
                    last_change->text = new_full_text;
                    history_clear_redo(h);
                    return; // Done
                }
            }
        }
    }
    // If not coalesced, add a new change.
    history_add_change(h, CHANGE_TYPE_INSERT, buffer->position_y, buffer->position_x, text);
}

void editor_insert_new_line() {
    pthread_mutex_lock(&editor_mutex);
    BufferLine *current_line = buffer->lines[buffer->position_y];

    uint32_t start_byte = 0;
    for (int i = 0; i < buffer->position_y; i++) {
        start_byte += buffer->lines[i]->byte_count + 1;
    }
    int byte_pos_x = buffer_get_byte_position_x(buffer);
    start_byte += byte_pos_x;

    buffer_realloc_lines_for_capacity(buffer);

    memmove(&buffer->lines[buffer->position_y + 2],
            &buffer->lines[buffer->position_y + 1],
            (buffer->line_count - buffer->position_y - 1) * sizeof(BufferLine *));

    buffer->lines[buffer->position_y + 1] = (BufferLine *)malloc(sizeof(BufferLine));
    if (buffer->lines[buffer->position_y + 1] == NULL) {
        log_error("editor.editor_insert_new_line: failed to allocate BufferLine");
        exit(1);
    }
    buffer_line_init(buffer->lines[buffer->position_y + 1]);

    int bytes_to_move = current_line->byte_count - byte_pos_x;
    if (bytes_to_move > 0) {
        BufferLine *new_line = buffer->lines[buffer->position_y + 1];
        buffer_line_realloc_for_capacity(new_line, bytes_to_move + 1);
        memcpy(new_line->chars, &current_line->chars[byte_pos_x], bytes_to_move);
        new_line->chars[bytes_to_move] = '\0';
        new_line->byte_count = bytes_to_move;
        new_line->char_count = utf8_strlen(new_line->chars);
        current_line->byte_count = byte_pos_x;
        current_line->chars[current_line->byte_count] = '\0';
        current_line->char_count = utf8_strlen(current_line->chars);
    }

    if (buffer->parser) {
        current_line->needs_highlight = 1;
        buffer->lines[buffer->position_y + 1]->needs_highlight = 1;
    }

    buffer->line_count++;
    if (buffer->parser && buffer->tree) {
        ts_tree_edit(buffer->tree, &(TSInputEdit){
            .start_byte = start_byte,
            .old_end_byte = start_byte,
            .new_end_byte = start_byte + 1,
            .start_point = { (uint32_t)buffer->position_y, (uint32_t)byte_pos_x },
            .old_end_point = { (uint32_t)buffer->position_y, (uint32_t)byte_pos_x },
            .new_end_point = { (uint32_t)(buffer->position_y + 1), 0 }
        });
    }
    editor_add_insertion_to_history("\n");
    buffer->position_y++;
    buffer_reset_offset_y(buffer, editor.screen_rows);
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

#include <stdbool.h>

static void editor_setup_lsp(const char *file_name) {
    if (!file_name) {
        return;
    }
    char absolute_path[PATH_MAX];
    if (realpath(file_name, absolute_path) != NULL) {
        if (!lsp_is_running()) {
            lsp_init(file_name);
        }
        if (lsp_is_running()) {
            char *content = buffer_get_content(buffer);
            if (content) {
                char file_uri[PATH_MAX + 7];
                snprintf(file_uri, sizeof(file_uri), "file://%s", absolute_path);
                const char *ext = strrchr(file_name, '.');
                const char *language_id = "c";
                if (ext && strcmp(ext, ".cpp") == 0) {
                    language_id = "cpp";
                }
                lsp_did_open(file_uri, language_id, content);
                free(content);
            }
        }
    }
}

void editor_init(char *file_name, bool benchmark_mode) {
    if (!benchmark_mode) {
        init_terminal_size();
        setup_terminal();
    }
    editor.buffer_capacity = 1;
    editor.buffers = malloc(sizeof(Buffer*) * editor.buffer_capacity);
    if (!editor.buffers) {
        log_error("editor.editor_init: alloc buffers failed");
        exit(1);
    }
    editor.buffer_count = 1;
    editor.active_buffer_idx = 0;
    editor.buffers[0] = (Buffer *)malloc(sizeof(Buffer));
    if (editor.buffers[0] == NULL) {
        log_error("editor.editor_init: alloc current buffer failed");
        exit(1);
    }
    theme_init();
    buffer_init(buffer, file_name);
    editor_handle_input = normal_handle_input;
    editor_set_cursor_shape(2);
    buffer_set_line_num_width(buffer);
    config_load(&editor.config);
    config_load_theme(editor.config.theme, &editor.current_theme);
    editor_setup_lsp(file_name);

    editor.last_search_term[0] = '\0';
    editor.last_search_direction = 1;
}

void editor_open(char *file_name) {
    pthread_mutex_lock(&editor_mutex);

    for (int i = 0; i < editor.buffer_count; i++) {
        if (editor.buffers[i]->file_name && strcmp(editor.buffers[i]->file_name, file_name) == 0) {
            editor.active_buffer_idx = i;
            pthread_mutex_unlock(&editor_mutex);
            return;
        }
    }

    if (editor.buffer_count == 1 && editor.buffers[0]->file_name == NULL) {
        buffer_destroy(editor.buffers[0]);
        buffer_init(editor.buffers[0], file_name);
        buffer_set_line_num_width(buffer);
        editor_handle_input = normal_handle_input;
        buffer_update_search_matches(buffer, editor.last_search_term);
        editor_setup_lsp(file_name);

        pthread_mutex_unlock(&editor_mutex);
        return;
    }

    if (editor.buffer_count == editor.buffer_capacity) {
        editor.buffer_capacity *= 2;
        editor.buffers = realloc(editor.buffers, sizeof(Buffer*) * editor.buffer_capacity);
        if (!editor.buffers) {
            log_error("editor.editor_open: realloc buffers failed");
            exit(1);
        }
    }
    editor.buffer_count++;
    editor.active_buffer_idx = editor.buffer_count - 1;
    editor.buffers[editor.active_buffer_idx] = (Buffer *)malloc(sizeof(Buffer));
    if (editor.buffers[editor.active_buffer_idx] == NULL) {
        log_error("editor.editor_open: alloc current buffer failed");
        exit(1);
    }
    buffer_init(buffer, file_name);
    buffer_set_line_num_width(buffer);
    editor_handle_input = normal_handle_input;
    buffer_update_search_matches(buffer, editor.last_search_term);
    editor_setup_lsp(file_name);

    pthread_mutex_unlock(&editor_mutex);
}

Buffer **editor_get_buffers(int *count) {
    *count = editor.buffer_count;
    return editor.buffers;
}

void editor_set_active_buffer(int index) {
    if (index >= 0 && index < editor.buffer_count) {
        editor.active_buffer_idx = index;
    }
    editor_handle_input = normal_handle_input;
    buffer_update_search_matches(buffer, editor.last_search_term);
    buffer->needs_draw = 1;
}

int editor_is_any_buffer_dirty(void) {
    for (int i = 0; i < editor.buffer_count; i++) {
        if (editor.buffers[i]->dirty) {
            return 1;
        }
    }
    return 0;
}

void editor_close_buffer(int buffer_index) {
    pthread_mutex_lock(&editor_mutex);
    if (buffer_index < 0 || buffer_index >= editor.buffer_count) {
        pthread_mutex_unlock(&editor_mutex);
        return;
    }

    buffer_destroy(editor.buffers[buffer_index]);
    free(editor.buffers[buffer_index]);

    for (int i = buffer_index; i < editor.buffer_count - 1; i++) {
        editor.buffers[i] = editor.buffers[i + 1];
    }
    editor.buffer_count--;

    if (editor.buffer_count == 0) {
        editor.buffer_capacity = 1;
        editor.buffers = realloc(editor.buffers, sizeof(Buffer*) * editor.buffer_capacity);
        editor.buffers[0] = (Buffer *)malloc(sizeof(Buffer));
        buffer_init(editor.buffers[0], NULL);
        editor.buffer_count = 1;
        editor.active_buffer_idx = 0;
        buffer->needs_draw = 1;
        pthread_mutex_unlock(&editor_mutex);
        picker_file_show();
        return;
    } else if (editor.active_buffer_idx >= buffer_index) {
        if (editor.active_buffer_idx > 0) {
            editor.active_buffer_idx--;
        }
    }
    buffer->needs_draw = 1;
    pthread_mutex_unlock(&editor_mutex);
}

int editor_get_active_buffer_idx(void) {
    return editor.active_buffer_idx;
}

void editor_start(char *file_name, int benchmark_mode) {
    editor_init(file_name, benchmark_mode);
    if (benchmark_mode) {
        // In benchmark mode, we just initialize, load the file, and exit.
        // The performance metrics will be captured by the calling process.
        return;
    }
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
    int ch_len = strlen(ch);
    buffer_line_realloc_for_capacity(line, line->byte_count + ch_len + 1);

    int byte_pos_x = buffer_get_byte_position_x(buffer);

    if (byte_pos_x < line->byte_count) {
        memmove(&line->chars[byte_pos_x + ch_len],
                &line->chars[byte_pos_x],
                line->byte_count - byte_pos_x);
    }

    memcpy(&line->chars[byte_pos_x], ch, ch_len);
    line->byte_count += ch_len;
    line->chars[line->byte_count] = '\0';
    line->char_count = utf8_strlen(line->chars);
    editor_add_insertion_to_history(ch);
    buffer->position_x++;
    buffer_reset_offset_x(buffer, editor.screen_cols);
    editor_did_change_buffer();

    if (buffer->parser && buffer->tree) {
        uint32_t start_byte = 0;
        for (int i = 0; i < buffer->position_y; i++) {
            start_byte += buffer->lines[i]->byte_count + 1;
        }
        start_byte += byte_pos_x;

        ts_tree_edit(buffer->tree, &(TSInputEdit){
            .start_byte = start_byte,
            .old_end_byte = start_byte,
            .new_end_byte = start_byte + ch_len,
            .start_point = {(uint32_t)buffer->position_y, (uint32_t)byte_pos_x},
            .old_end_point = {(uint32_t)buffer->position_y, (uint32_t)byte_pos_x},
            .new_end_point = {(uint32_t)buffer->position_y, (uint32_t)(byte_pos_x + ch_len)}
        });
        line->needs_highlight = 1;
        buffer->needs_parse = 1;
    }
    pthread_mutex_unlock(&editor_mutex);
}

void editor_move_cursor_right() {
    pthread_mutex_lock(&editor_mutex);
    BufferLine *line = buffer->lines[buffer->position_y];
    if (buffer->position_x < line->char_count) {
        buffer->position_x++;
    } else if (buffer->position_y < buffer->line_count - 1) {
        buffer->position_y++;
        buffer->position_x = 0;
    }
    buffer_reset_offset_y(buffer, editor.screen_rows);
    buffer_reset_offset_x(buffer, editor.screen_cols);
    buffer_update_current_search_match(buffer);
    buffer->needs_draw = 1;
    pthread_mutex_unlock(&editor_mutex);
}


void editor_move_cursor_left() {
    pthread_mutex_lock(&editor_mutex);
    if (buffer->position_x > 0) {
        buffer->position_x--;
    } else if (buffer->position_y > 0) {
        buffer->position_y--;
        buffer->position_x = buffer->lines[buffer->position_y]->char_count;
    }
    buffer_reset_offset_y(buffer, editor.screen_rows);
    buffer_reset_offset_x(buffer, editor.screen_cols);
    buffer_update_current_search_match(buffer);
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
    buffer_reset_offset_y(buffer, editor.screen_rows);
    buffer_set_logical_position_x(buffer, visual_x);
    buffer_reset_offset_x(buffer, editor.screen_cols);
    buffer_update_current_search_match(buffer);
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
    buffer_reset_offset_y(buffer, editor.screen_rows);
    buffer_set_logical_position_x(buffer, visual_x);
    buffer_reset_offset_x(buffer, editor.screen_cols);
    buffer_update_current_search_match(buffer);
    buffer->needs_draw = 1;
    pthread_mutex_unlock(&editor_mutex);
}

void editor_move_to_start_of_line(void) {
    pthread_mutex_lock(&editor_mutex);
    buffer->position_x = 0;
    buffer_reset_offset_x(buffer, editor.screen_cols);
    buffer_update_current_search_match(buffer);
    buffer->needs_draw = 1;
    pthread_mutex_unlock(&editor_mutex);
}

void editor_move_to_end_of_line(void) {
    pthread_mutex_lock(&editor_mutex);
    BufferLine *line = buffer->lines[buffer->position_y];
    buffer->position_x = line->char_count;
    buffer_reset_offset_x(buffer, editor.screen_cols);
    buffer_update_current_search_match(buffer);
    buffer->needs_draw = 1;
    pthread_mutex_unlock(&editor_mutex);
}

void editor_write_force() {
    pthread_mutex_lock(&editor_mutex);
    if (buffer->file_name == NULL) {
        pthread_mutex_unlock(&editor_mutex);
        return;
    }
    FILE *fp = fopen(buffer->file_name, "w");
    if (fp == NULL) {
        pthread_mutex_unlock(&editor_mutex);
        return;
    }
    for (int line_idx = 0; line_idx < buffer->line_count; line_idx++) {
        BufferLine *line = buffer->lines[line_idx];
        fwrite(line->chars, 1, line->byte_count, fp);
        if (line_idx < buffer->line_count - 1) {
            fputc('\n', fp);
        }
    }
    fclose(fp);
    buffer->dirty = 0;

    struct stat st;
    if (stat(buffer->file_name, &st) == 0) {
        buffer->mtime = st.st_mtime;
    }

    buffer->needs_draw = 1;
    pthread_mutex_unlock(&editor_mutex);
}

void editor_write() {
    if (buffer->file_name == NULL) {
        return;
    }
    struct stat st;
    if (stat(buffer->file_name, &st) == 0) {
        if (st.st_mtime > buffer->mtime) {
            return;
        }
    }
    editor_write_force();
}

void editor_delete() {
    pthread_mutex_lock(&editor_mutex);
    BufferLine *line = buffer->lines[buffer->position_y];

    uint32_t start_byte = 0;
    for (int i = 0; i < buffer->position_y; i++) {
        start_byte += buffer->lines[i]->byte_count + 1;
    }
    int byte_pos_x = buffer_get_byte_position_x(buffer);
    start_byte += byte_pos_x;

    if (buffer->position_x == line->char_count) {
        if (buffer->position_y == buffer->line_count - 1) {
            pthread_mutex_unlock(&editor_mutex);
            return;
        }
        if (!is_undo_redo_active) {
            history_add_change(buffer->history, CHANGE_TYPE_DELETE, buffer->position_y, buffer->position_x, "\n");
        }
        if (buffer->parser) {
            BufferLine *next_line = buffer->lines[buffer->position_y + 1];
            next_line->needs_highlight = 1;
        }
        BufferLine *next_line = buffer->lines[buffer->position_y + 1];
        int new_byte_count = line->byte_count + next_line->byte_count;
        buffer_line_realloc_for_capacity(line, new_byte_count + 1);
        memcpy(&line->chars[line->byte_count], next_line->chars, next_line->byte_count);
        line->byte_count = new_byte_count;
        line->chars[line->byte_count] = '\0';
        line->char_count = utf8_strlen(line->chars);
        buffer_line_destroy(next_line);

        memmove(&buffer->lines[buffer->position_y + 1],
                &buffer->lines[buffer->position_y + 2],
                (buffer->line_count - buffer->position_y - 2) * sizeof(BufferLine *));
        buffer->line_count--;
        buffer_set_line_num_width(buffer);
        if (buffer->parser && buffer->tree) {
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
        int char_len = utf8_char_len(&line->chars[byte_pos_x]);
        char deleted_char[5];
        strncpy(deleted_char, &line->chars[byte_pos_x], char_len);
        deleted_char[char_len] = '\0';
        if (!is_undo_redo_active) {
            history_add_change(buffer->history, CHANGE_TYPE_DELETE, buffer->position_y, buffer->position_x, deleted_char);
        }
        memmove(&line->chars[byte_pos_x],
                &line->chars[byte_pos_x + char_len],
                line->byte_count - byte_pos_x - char_len);
        line->byte_count -= char_len;
        line->chars[line->byte_count] = '\0';
        line->char_count = utf8_strlen(line->chars);
        if (buffer->parser && buffer->tree) {
            line->needs_highlight = 1;
            ts_tree_edit(buffer->tree, &(TSInputEdit){
                .start_byte = start_byte,
                .old_end_byte = start_byte + char_len,
                .new_end_byte = start_byte,
                .start_point = { (uint32_t)buffer->position_y, (uint32_t)byte_pos_x },
                .old_end_point = { (uint32_t)buffer->position_y, (uint32_t)(byte_pos_x + char_len) },
                .new_end_point = { (uint32_t)buffer->position_y, (uint32_t)byte_pos_x }
            });
        }
    }

    editor_did_change_buffer();
    pthread_mutex_unlock(&editor_mutex);
}

void editor_clear_line(void) {
    pthread_mutex_lock(&editor_mutex);
    Buffer *b = editor_get_active_buffer();
    EditorCommand cmd = {0};
    Range range = {
        .y_start = b->position_y,
        .y_end = b->position_y,
        .x_start = 0,
        .x_end = b->lines[b->position_y]->char_count,
    };
    if (range.x_start != range.x_end) {
        range_delete(b, &range, &cmd);
        editor_did_change_buffer();
    }
    b->position_x = 0;
    buffer_update_current_search_match(b);
    b->needs_draw = 1;
    pthread_mutex_unlock(&editor_mutex);
}

void editor_backspace() {
    pthread_mutex_lock(&editor_mutex);
    BufferLine *line = buffer->lines[buffer->position_y];

    uint32_t start_byte = 0;
    for (int i = 0; i < buffer->position_y; i++) {
        start_byte += buffer->lines[i]->byte_count + 1;
    }
    int byte_pos_x = buffer_get_byte_position_x(buffer);
    start_byte += byte_pos_x;

    if (buffer->position_x == 0) {
        if (buffer->position_y == 0) {
            pthread_mutex_unlock(&editor_mutex);
            return;
        }
        BufferLine *prev_line = buffer->lines[buffer->position_y - 1];
        if (!is_undo_redo_active) {
            history_add_change(buffer->history, CHANGE_TYPE_DELETE, buffer->position_y - 1, prev_line->char_count, "\n");
        }
        int new_byte_count = prev_line->byte_count + line->byte_count;
        buffer_line_realloc_for_capacity(prev_line, new_byte_count + 1);
        memcpy(&prev_line->chars[prev_line->byte_count], line->chars, line->byte_count);
        prev_line->byte_count = new_byte_count;
        prev_line->chars[prev_line->byte_count] = '\0';
        prev_line->char_count = utf8_strlen(prev_line->chars);
        if (buffer->parser) {
            prev_line->needs_highlight = 1;
        }

        memmove(&buffer->lines[buffer->position_y],
                &buffer->lines[buffer->position_y + 1],
                (buffer->line_count - buffer->position_y - 1) * sizeof(BufferLine *));
        buffer->line_count--;
        buffer_set_line_num_width(buffer);
        if (buffer->parser && buffer->tree) {
            ts_tree_edit(buffer->tree, &(TSInputEdit){
                .start_byte = start_byte - 1,
                .old_end_byte = start_byte,
                .new_end_byte = start_byte - 1,
                .start_point = { (uint32_t)buffer->position_y, 0 },
                .old_end_point = { (uint32_t)buffer->position_y, 0 },
                .new_end_point = { (uint32_t)(buffer->position_y - 1), (uint32_t)prev_line->byte_count }
            });
        }
        buffer->position_y--;
        buffer_reset_offset_y(buffer, editor.screen_rows);
        buffer->position_x = prev_line->char_count;
        buffer_line_destroy(line);

    } else {
        int prev_byte_pos_x = utf8_char_to_byte_index(line->chars, buffer->position_x - 1);
        int char_len = byte_pos_x - prev_byte_pos_x;
        char deleted_char[5];
        strncpy(deleted_char, &line->chars[prev_byte_pos_x], char_len);
        deleted_char[char_len] = '\0';
        if (!is_undo_redo_active) {
            history_add_change(buffer->history, CHANGE_TYPE_DELETE, buffer->position_y, buffer->position_x - 1, deleted_char);
        }
        memmove(&line->chars[prev_byte_pos_x],
                &line->chars[byte_pos_x],
                line->byte_count - byte_pos_x);
        line->byte_count -= char_len;
        line->chars[line->byte_count] = '\0';
        line->char_count = utf8_strlen(line->chars);
        buffer->position_x--;
        if (buffer->parser && buffer->tree) {
            line->needs_highlight = 1;
            ts_tree_edit(buffer->tree, &(TSInputEdit){
                .start_byte = start_byte - char_len,
                .old_end_byte = start_byte,
                .new_end_byte = start_byte - char_len,
                .start_point = { (uint32_t)buffer->position_y, (uint32_t)(byte_pos_x - char_len) },
                .old_end_point = { (uint32_t)buffer->position_y, (uint32_t)byte_pos_x },
                .new_end_point = { (uint32_t)buffer->position_y, (uint32_t)(byte_pos_x - char_len) }
             });
        }
    }

    buffer_reset_offset_x(buffer, editor.screen_cols);
    buffer_reset_offset_y(buffer, editor.screen_rows);
    editor_did_change_buffer();
    pthread_mutex_unlock(&editor_mutex);
}

void editor_move_n_lines_down(int n) {
    pthread_mutex_lock(&editor_mutex);
    int cnt;
    if (n <= 0) {
        cnt = editor.screen_rows / 2;
    } else {
        cnt = n;
    }
    int remaining = buffer->line_count - 1 - buffer->position_y;
    if (remaining < cnt) cnt = remaining;
    int visual_x = buffer_get_visual_position_x(buffer);
    buffer->position_y += cnt;
    buffer->offset_y += cnt;
    buffer_reset_offset_y(buffer, editor.screen_rows);
    buffer_set_logical_position_x(buffer, visual_x);
    buffer_reset_offset_x(buffer, editor.screen_cols);
    buffer->needs_draw = 1;
    pthread_mutex_unlock(&editor_mutex);
}

void editor_move_n_lines_up(int n) {
    pthread_mutex_lock(&editor_mutex);
    int cnt;
    if (n <= 0) {
        cnt = editor.screen_rows / 2;
    } else {
        cnt = n;
    }
    if (buffer->position_y < cnt) cnt = buffer->position_y;
    int visual_x = buffer_get_visual_position_x(buffer);
    buffer->position_y -= cnt;
    buffer->offset_y -= cnt;
    buffer_reset_offset_y(buffer, editor.screen_rows);
    buffer_set_logical_position_x(buffer, visual_x);
    buffer_reset_offset_x(buffer, editor.screen_cols);
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
        int byte_pos = buffer_get_byte_position_x(buffer);
        while (is_whitespace(line->chars[byte_pos])) {
            range_expand_right(&line, range);
            moved = 1;
            byte_pos = buffer_get_byte_position_x(buffer);
        }
        int touched = 0;
        if (!is_word_char(line->chars[byte_pos])) {
            if (moved) {
                continue;
            }
            while (range->x_end < line->char_count - 1 && !is_word_char(line->chars[byte_pos + 1]) && !is_whitespace(line->chars[byte_pos + 1])) {
                touched = 1;
                range_expand_right(&line, range);
                byte_pos = buffer_get_byte_position_x(buffer);
            }
        }
        if (touched) {
            continue;
        }
        if (!is_word_char(line->chars[byte_pos])) {
            continue;
        }
        while (range->x_end < line->char_count - 1 && is_word_char(line->chars[byte_pos + 1])) {
            range_expand_right(&line, range);
            byte_pos = buffer_get_byte_position_x(buffer);
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
        int byte_pos = buffer_get_byte_position_x(buffer);
        while (is_whitespace(line->chars[byte_pos])) {
            range_expand_right(&line, range);
            byte_pos = buffer_get_byte_position_x(buffer);
        }
        while (range->x_end < line->char_count - 1 && !is_whitespace(line->chars[byte_pos + 1])) {
            range_expand_right(&line, range);
            byte_pos = buffer_get_byte_position_x(buffer);
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
        int byte_pos = buffer_get_byte_position_x(buffer);
        while (is_whitespace(line->chars[byte_pos])) {
            range_expand_left(&line, range);
            byte_pos = buffer_get_byte_position_x(buffer);
        }
        int touched = 0;
        if (!is_word_char(line->chars[byte_pos])) {
            int prev_byte_pos = utf8_char_to_byte_index(line->chars, range->x_end - 1);
            while (range->x_end > 0 && !is_word_char(line->chars[prev_byte_pos]) && !is_whitespace(line->chars[prev_byte_pos])) {
                touched = 1;
                range_expand_left(&line, range);
                prev_byte_pos = utf8_char_to_byte_index(line->chars, range->x_end - 1);
            }
        }
        if (touched) {
            continue;
        }
        if (!is_word_char(line->chars[byte_pos])) {
            continue;
        }
        int prev_byte_pos = utf8_char_to_byte_index(line->chars, range->x_end - 1);
        while (range->x_end > 0 && is_word_char(line->chars[prev_byte_pos])) {
            range_expand_left(&line, range);
            prev_byte_pos = utf8_char_to_byte_index(line->chars, range->x_end - 1);
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
        int byte_pos = buffer_get_byte_position_x(buffer);
        while (is_whitespace(line->chars[byte_pos])) {
            range_expand_left(&line, range);
            byte_pos = buffer_get_byte_position_x(buffer);
        }
        int prev_byte_pos = utf8_char_to_byte_index(line->chars, range->x_end - 1);
        while (range->x_end > 0 && !is_whitespace(line->chars[prev_byte_pos])) {
            range_expand_left(&line, range);
            prev_byte_pos = utf8_char_to_byte_index(line->chars, range->x_end - 1);
        }
    }
}

static void get_target_range(EditorCommand *cmd, Range *range) {
    if (editor_handle_input == visual_handle_input && cmd->action && cmd->action != 'g') {
        range->y_start = buffer->selection_start_y;
        range->y_end = buffer->position_y;

        if (buffer->visual_mode == VISUAL_MODE_LINE) {
            if (range->y_start > range->y_end) {
                int tmp = range->y_start;
                range->y_start = range->y_end;
                range->y_end = tmp;
            }
            range->x_start = 0;
            range->x_end = buffer->lines[range->y_end]->char_count;
        } else {
            range->x_start = buffer->selection_start_x;
            range->x_end = buffer->position_x;
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
        }
        return;
    }

    BufferLine *line = buffer->lines[buffer->position_y];
    int count = cmd->count ? cmd->count : 1;
    range->x_start = buffer->position_x;
    range->y_start = buffer->position_y;
    range->x_end = buffer->position_x;
    range->y_end = buffer->position_y;
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
                if (cmd->specifier[0] == '\0') {
                    range->x_end = range->x_start;
                    range->y_end = range->y_start;
                    break;
                }

                int original_y = range->y_start;
                if (is_line_empty(buffer->lines[original_y])) {
                    if (cmd->specifier[0] == 'a') {
                        // dap on an empty line should delete that line.
                    } else {
                        // dip on an empty line is a no-op
                        range->x_end = range->x_start;
                        range->y_end = range->y_start;
                    }
                } else {
                    while (range->y_start > 0 && !is_line_empty(buffer->lines[range->y_start - 1])) {
                        range->y_start--;
                    }
                    while (range->y_end < buffer->line_count - 1 && !is_line_empty(buffer->lines[range->y_end + 1])) {
                        range->y_end++;
                    }
                }

                if (cmd->specifier[0] == 'a') {
                    if (range->y_end < buffer->line_count - 1 && is_line_empty(buffer->lines[range->y_end + 1])) {
                        range->y_end++;
                    } else if (range->y_start > 0 && is_line_empty(buffer->lines[range->y_start - 1])) {
                        range->y_start--;
                    }
                }

                range->x_start = 0;
                range->x_end = buffer->lines[range->y_end]->char_count;
            }
            break;
        case 'w':
            if (cmd->action) {
                int byte_pos_x = buffer_get_byte_position_x(buffer);
                if (cmd->specifier[0] == 'i') {
                    if (is_word_char(line->chars[byte_pos_x])) {
                        int start_byte_pos = byte_pos_x;
                        while (start_byte_pos > 0 && is_word_char(line->chars[start_byte_pos - 1])) start_byte_pos--;
                        range->x_start = utf8_char_count(line->chars, start_byte_pos);

                        int end_byte_pos = byte_pos_x;
                        while (end_byte_pos < line->byte_count - 1 && is_word_char(line->chars[end_byte_pos + 1])) end_byte_pos++;
                        range->x_end = utf8_char_count(line->chars, end_byte_pos) + 1;
                    }
                } else if (cmd->specifier[0] == 'a') {
                    if (is_word_char(line->chars[byte_pos_x])) {
                        int start_byte_pos = byte_pos_x;
                        while (start_byte_pos > 0 && is_word_char(line->chars[start_byte_pos - 1])) start_byte_pos--;
                        range->x_start = utf8_char_count(line->chars, start_byte_pos);

                        int end_byte_pos = byte_pos_x;
                        while (end_byte_pos < line->byte_count - 1 && is_word_char(line->chars[end_byte_pos + 1])) end_byte_pos++;

                        int x_end_byte = end_byte_pos;
                        while (end_byte_pos < line->byte_count - 1 && is_whitespace(line->chars[end_byte_pos + 1])) end_byte_pos++;
                        range->x_end = utf8_char_count(line->chars, end_byte_pos) + 1;

                        if (x_end_byte + 1 == end_byte_pos) {
                            while (start_byte_pos > 0 && is_whitespace(line->chars[start_byte_pos - 1])) start_byte_pos--;
                            range->x_start = utf8_char_count(line->chars, start_byte_pos);
                        }
                    } else {
                        int end_of_space = byte_pos_x;
                        while(end_of_space < line->byte_count && is_whitespace(line->chars[end_of_space])) end_of_space++;
                        if (end_of_space < line->byte_count) {
                            int start_byte_pos = byte_pos_x;
                            while(start_byte_pos > 0 && is_whitespace(line->chars[start_byte_pos - 1])) start_byte_pos--;
                            range->x_start = utf8_char_count(line->chars, start_byte_pos);

                            int end_byte_pos = end_of_space;
                            while(end_byte_pos < line->byte_count - 1 && is_word_char(line->chars[end_byte_pos + 1])) end_byte_pos++;
                            range->x_end = utf8_char_count(line->chars, end_byte_pos) + 1;
                        }
                    }
                }
                break;
            }
            while (count) {
                count--;
                if (range->x_end >= line->char_count - 1) {
                    range_expand_right(&line, range);
                    if (line->char_count) {
                        int byte_pos = buffer_get_byte_position_x(buffer);
                        while (is_whitespace(line->chars[byte_pos])) {
                            range_expand_right(&line, range);
                            byte_pos = buffer_get_byte_position_x(buffer);
                        }
                    }
                    continue;
                }
                int touched = 0;
                int byte_pos = buffer_get_byte_position_x(buffer);
                while (!is_word_char(line->chars[byte_pos]) && !is_whitespace(line->chars[byte_pos])) {
                    touched = 1;
                    if (range_expand_right(&line, range)) break;
                    byte_pos = buffer_get_byte_position_x(buffer);
                }
                if (!touched) {
                    while (is_word_char(line->chars[byte_pos])) {
                        if (range_expand_right(&line, range)) break;
                        byte_pos = buffer_get_byte_position_x(buffer);
                    }
                }

                while (range->x_end < line->char_count && is_whitespace(line->chars[byte_pos])) {
                    if (range_expand_right(&line, range) && !line->char_count) break;
                    byte_pos = buffer_get_byte_position_x(buffer);
                }
            }
            break;
        case 'W':
            if (cmd->action) {
                int byte_pos_x = buffer_get_byte_position_x(buffer);
                if (cmd->specifier[0] == 'i') {
                    if (!is_whitespace(line->chars[byte_pos_x])) {
                        int start_byte_pos = byte_pos_x;
                        while (start_byte_pos > 0 && !is_whitespace(line->chars[start_byte_pos - 1])) start_byte_pos--;
                        range->x_start = utf8_char_count(line->chars, start_byte_pos);
                        int end_byte_pos = byte_pos_x;
                        while (end_byte_pos < line->byte_count - 1 && !is_whitespace(line->chars[end_byte_pos + 1])) end_byte_pos++;
                        range->x_end = utf8_char_count(line->chars, end_byte_pos) + 1;
                    }
                } else if (cmd->specifier[0] == 'a') {
                    if (!is_whitespace(line->chars[byte_pos_x])) {
                        int start_byte_pos = byte_pos_x;
                        while (start_byte_pos > 0 && !is_whitespace(line->chars[start_byte_pos - 1])) start_byte_pos--;
                        range->x_start = utf8_char_count(line->chars, start_byte_pos);
                        int end_byte_pos = byte_pos_x;
                        while (end_byte_pos < line->byte_count - 1 && !is_whitespace(line->chars[end_byte_pos + 1])) end_byte_pos++;

                        int x_end_byte = end_byte_pos;
                        while (end_byte_pos < line->byte_count - 1 && is_whitespace(line->chars[end_byte_pos + 1])) end_byte_pos++;
                        range->x_end = utf8_char_count(line->chars, end_byte_pos) + 1;

                        if (x_end_byte + 1 == end_byte_pos) {
                            while (start_byte_pos > 0 && is_whitespace(line->chars[start_byte_pos - 1])) start_byte_pos--;
                            range->x_start = utf8_char_count(line->chars, start_byte_pos);
                        }
                    } else {
                        int end_of_space = byte_pos_x;
                        while(end_of_space < line->byte_count && is_whitespace(line->chars[end_of_space])) end_of_space++;
                        if (end_of_space < line->byte_count) {
                            int start_byte_pos = byte_pos_x;
                            while(start_byte_pos > 0 && is_whitespace(line->chars[start_byte_pos - 1])) start_byte_pos--;
                            range->x_start = utf8_char_count(line->chars, start_byte_pos);
                            int end_byte_pos = end_of_space;
                            while(end_byte_pos < line->byte_count - 1 && !is_whitespace(line->chars[end_byte_pos + 1])) end_byte_pos++;
                            range->x_end = utf8_char_count(line->chars, end_byte_pos) + 1;
                        }
                    }
                }
                break;
            }
            while (count) {
                count--;
                if (range->x_end >= line->char_count - 1) {
                    range_expand_right(&line, range);
                    if (line->char_count) {
                        int byte_pos = buffer_get_byte_position_x(buffer);
                        while (is_whitespace(line->chars[byte_pos])) {
                            range_expand_right(&line, range);
                            byte_pos = buffer_get_byte_position_x(buffer);
                        }
                    }
                    continue;
                }
                int byte_pos = buffer_get_byte_position_x(buffer);
                while (!is_whitespace(line->chars[byte_pos])) {
                    if (range_expand_right(&line, range)) break;
                    byte_pos = buffer_get_byte_position_x(buffer);
                }

                while (range->x_end < line->char_count && is_whitespace(line->chars[byte_pos])) {
                    if (range_expand_right(&line, range) && !line->char_count) break;
                    byte_pos = buffer_get_byte_position_x(buffer);
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
                int start_byte_pos = buffer_get_byte_position_x(buffer);
                char *p = line->chars + start_byte_pos;
                for (int j = range->x_end + 1; j < line->char_count; j++) {
                    p += utf8_char_len(p);
                    if (strncmp(p, cmd->specifier, strlen(cmd->specifier)) == 0) {
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
                    int byte_pos = utf8_char_to_byte_index(line->chars, j);
                    if (strncmp(line->chars + byte_pos, cmd->specifier, strlen(cmd->specifier)) == 0) {
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
                int start_byte_pos = buffer_get_byte_position_x(buffer);
                char *p = line->chars + start_byte_pos;
                for (int j = range->x_end + 2; j < line->char_count; j++) {
                    p += utf8_char_len(p);
                    if (strncmp(p, cmd->specifier, strlen(cmd->specifier)) == 0) {
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
                    int byte_pos = utf8_char_to_byte_index(line->chars, j);
                    if (strncmp(line->chars + byte_pos, cmd->specifier, strlen(cmd->specifier)) == 0) {
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

static char* buffer_get_text_in_range(Buffer *b, int top, int left, int bottom, int right) {
    size_t len = 0;
    for (int y = top; y <= bottom; y++) {
        BufferLine *line = b->lines[y];
        int start_x = (y == top) ? left : 0;
        int end_x = (y == bottom) ? right : line->char_count;
        int start_byte = utf8_char_to_byte_index(line->chars, start_x);
        int end_byte = utf8_char_to_byte_index(line->chars, end_x);
        len += end_byte - start_byte;
        if (y < bottom) {
            len++; // for '\n'
        }
    }

    if (len == 0) return NULL;

    char *text = malloc(len + 1);
    if (!text) return NULL;

    char *p = text;
    for (int y = top; y <= bottom; y++) {
        BufferLine *line = b->lines[y];
        int start_x = (y == top) ? left : 0;
        int end_x = (y == bottom) ? right : line->char_count;
        int start_byte = utf8_char_to_byte_index(line->chars, start_x);
        int end_byte = utf8_char_to_byte_index(line->chars, end_x);
        int bytes_to_copy = end_byte - start_byte;
        memcpy(p, line->chars + start_byte, bytes_to_copy);
        p += bytes_to_copy;
        if (y < bottom) {
            *p++ = '\n';
        }
    }
    *p = '\0';
    return text;
}

void range_delete(Buffer *b, Range *range, EditorCommand *cmd) {
    int left = range_get_left_boundary(range);
    int right = range_get_right_boundary(range);
    int top = range_get_top_boundary(range);
    int bottom = range_get_bottom_boundary(range);
    if (left == right && top == bottom) return;

    if (cmd->target == 'p' && top == bottom) {
        if (!is_undo_redo_active) {
            char *line_text = buffer_get_text_in_range(b, top, 0, top, b->lines[top]->char_count);
            if (line_text) {
                size_t len = strlen(line_text);
                char *deleted_text = malloc(len + 2);
                if (deleted_text) {
                    sprintf(deleted_text, "%s\n", line_text);
                    history_add_change(b->history, CHANGE_TYPE_DELETE, top, 0, deleted_text);
                    free(deleted_text);
                }
                free(line_text);
            } else { // empty line
                history_add_change(b->history, CHANGE_TYPE_DELETE, top, 0, "\n");
            }
        }

        buffer_line_destroy(b->lines[top]);
        free(b->lines[top]);
        memmove(&b->lines[top], &b->lines[top + 1], (b->line_count - top - 1) * sizeof(BufferLine *));
        b->line_count--;
        return;
    }

    if (!is_undo_redo_active) {
        char *deleted_text = buffer_get_text_in_range(b, top, left, bottom, right);
        if (deleted_text) {
            history_add_change(b->history, CHANGE_TYPE_DELETE, top, left, deleted_text);
            free(deleted_text);
        }
    }

    if (b->parser && b->tree) {
        uint32_t start_byte = 0;
        for (int i = 0; i < top; i++) {
            start_byte += b->lines[i]->byte_count + 1;
        }
        uint32_t start_byte_in_line = utf8_char_to_byte_index(b->lines[top]->chars, left);
        start_byte += start_byte_in_line;

        uint32_t deleted_bytes = 0;
        for (int i = top; i <= bottom; i++) {
            BufferLine *line = b->lines[i];
            int start_j = (i == top) ? left : 0;
            int end_j = (i == bottom) ? right : line->char_count;
            deleted_bytes += utf8_char_to_byte_index(line->chars, end_j) - utf8_char_to_byte_index(line->chars, start_j);
            if (i < bottom) {
                deleted_bytes++; // newline
            }
        }

        uint32_t end_byte_in_line = utf8_char_to_byte_index(b->lines[bottom]->chars, right);

        ts_tree_edit(b->tree, &(TSInputEdit){
            .start_byte = start_byte,
            .old_end_byte = start_byte + deleted_bytes,
            .new_end_byte = start_byte,
            .start_point = { (uint32_t)top, start_byte_in_line },
            .old_end_point = { (uint32_t)bottom, end_byte_in_line },
            .new_end_point = { (uint32_t)top, start_byte_in_line }
        });
    }

    int left_byte = utf8_char_to_byte_index(b->lines[top]->chars, left);
    int right_byte = utf8_char_to_byte_index(b->lines[bottom]->chars, right);
    int bottom_remaining_bytes = b->lines[bottom]->byte_count - right_byte;
    int new_byte_count = left_byte + bottom_remaining_bytes;
    buffer_line_realloc_for_capacity(b->lines[top], new_byte_count + 1);
    memmove(&b->lines[top]->chars[left_byte], &b->lines[bottom]->chars[right_byte], bottom_remaining_bytes);
    b->lines[top]->byte_count = new_byte_count;
    b->lines[top]->chars[new_byte_count] = '\0';
    b->lines[top]->char_count = utf8_strlen(b->lines[top]->chars);

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
                buffer_update_search_matches(buffer, last_term);
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

    if (editor_handle_input == visual_handle_input && (!cmd->action || cmd->action == 'g')) {
        buffer->position_x = range.x_end;
        buffer->position_y = range.y_end;
        buffer_reset_offset_x(buffer, editor.screen_cols);
        buffer_reset_offset_y(buffer, editor.screen_rows);
        editor_command_reset(cmd);
        editor_request_redraw();
        pthread_mutex_unlock(&editor_mutex);
        return;
    }

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
        buffer_reset_offset_x(buffer, editor.screen_cols);
        buffer_reset_offset_y(buffer, editor.screen_rows);
        buffer_update_current_search_match(buffer);
        buffer->needs_draw = 1;
        editor_command_reset(cmd);
        pthread_mutex_unlock(&editor_mutex);
        return;
    }
    switch (cmd->action) {
        case 'g':
            buffer->position_x = range.x_end;
            buffer->position_y = range.y_end;
            buffer_reset_offset_x(buffer, editor.screen_cols);
            buffer_reset_offset_y(buffer, editor.screen_rows);
            buffer_update_current_search_match(buffer);
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
                buffer->position_y = top;
                if (cmd->target == 'p') {
                    int lines_to_remove = bottom - top + 1;
                    if (lines_to_remove <= 0 || (top == bottom && left == right)) break;

                    for (int i = top; i <= bottom; i++) {
                        buffer_line_destroy(buffer->lines[i]);
                        free(buffer->lines[i]);
                    }
                    if (buffer->line_count > bottom + 1) {
                        memmove(&buffer->lines[top], &buffer->lines[bottom + 1], (buffer->line_count - bottom - 1) * sizeof(BufferLine *));
                    }
                    buffer->line_count -= lines_to_remove;
                    if (buffer->line_count == 0) {
                        buffer->lines[0] = (BufferLine *)malloc(sizeof(BufferLine));
                        buffer_line_init(buffer->lines[0]);
                        buffer->line_count = 1;
                    }
                    if (top < buffer->line_count) {
                        buffer->position_y = top;
                    } else {
                        buffer->position_y = buffer->line_count - 1;
                    }
                } else {
                    range_delete(buffer, &range, cmd);
                }
                buffer_reset_offset_x(buffer, editor.screen_cols);
                buffer_reset_offset_y(buffer, editor.screen_rows);
                buffer_update_current_search_match(buffer);
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
    pthread_mutex_lock(&editor_mutex);
    buffer->offset_y = buffer->position_y - editor.screen_rows / 2;
    if (buffer->offset_y < 0) {
        buffer->offset_y = 0;
    }
    buffer->needs_draw = 1;
    pthread_mutex_unlock(&editor_mutex);
}

void editor_scroll_to_top(void) {
    pthread_mutex_lock(&editor_mutex);
    buffer->offset_y = buffer->position_y - 5;
    if (buffer->offset_y < 0) {
        buffer->offset_y = 0;
    }
    buffer->needs_draw = 1;
    pthread_mutex_unlock(&editor_mutex);
}

void editor_scroll_to_bottom(void) {
    pthread_mutex_lock(&editor_mutex);
    buffer->offset_y = buffer->position_y - editor.screen_rows + 2 + 5;
    if (buffer->offset_y < 0) {
        buffer->offset_y = 0;
    }
    buffer->needs_draw = 1;
    pthread_mutex_unlock(&editor_mutex);
}

void editor_set_screen_size(int rows, int cols) {
    editor.screen_rows = rows;
    editor.screen_cols = cols;
}

static int utf8_char_len_from_string(const char *s) {
    unsigned char c = *s;
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1; // fallback
}

static void editor_insert_string_at(const char *text, int y, int x) {
    buffer->position_y = y;
    buffer->position_x = x;
    const char *p = text;
    while (*p) {
        if (*p == '\n') {
            editor_insert_new_line();
            p++;
        } else {
            char utf8_buf[8];
            size_t len = utf8_char_len_from_string(p);
            strncpy(utf8_buf, p, len);
            utf8_buf[len] = '\0';
            editor_insert_char(utf8_buf);
            p += len;
        }
    }
}

static void calculate_end_point(const char *text, int start_y, int start_x, int *end_y, int *end_x) {
    *end_y = start_y;
    *end_x = start_x;
    const char *p = text;
    while (*p) {
        if (*p == '\n') {
            (*end_y)++;
            *end_x = 0;
            p++;
        } else {
            (*end_x)++;
            p += utf8_char_len_from_string(p);
        }
    }
}

void editor_undo(void) {
    pthread_mutex_lock(&editor_mutex);
    is_undo_redo_active = 1;

    Change *change = history_pop_undo(buffer->history);
    if (change) {
        if (change->type == CHANGE_TYPE_INSERT) {
            int end_y, end_x;
            calculate_end_point(change->text, change->y, change->x, &end_y, &end_x);

            Range range = { .y_start = change->y, .x_start = change->x, .y_end = end_y, .x_end = end_x };
            EditorCommand cmd = {0}; // dummy cmd
            range_delete(buffer, &range, &cmd);
            buffer->position_y = change->y;
            buffer->position_x = change->x;
        } else { // CHANGE_TYPE_DELETE
            pthread_mutex_unlock(&editor_mutex);
            editor_insert_string_at(change->text, change->y, change->x);
            pthread_mutex_lock(&editor_mutex);
            buffer->position_y = change->y;
            buffer->position_x = change->x;
        }
        history_push_redo(buffer->history, change);
        editor_did_change_buffer();
    }

    is_undo_redo_active = 0;
    pthread_mutex_unlock(&editor_mutex);
}

void editor_redo(void) {
    pthread_mutex_lock(&editor_mutex);
    is_undo_redo_active = 1;

    Change *change = history_pop_redo(buffer->history);
    if (change) {
        if (change->type == CHANGE_TYPE_INSERT) {
            pthread_mutex_unlock(&editor_mutex);
            editor_insert_string_at(change->text, change->y, change->x);
            pthread_mutex_lock(&editor_mutex);
            buffer->position_y = change->y;
            buffer->position_x = change->x;
        } else { // CHANGE_TYPE_DELETE
            int end_y, end_x;
            calculate_end_point(change->text, change->y, change->x, &end_y, &end_x);

            Range range = { .y_start = change->y, .x_start = change->x, .y_end = end_y, .x_end = end_x };
            EditorCommand cmd = {0}; // dummy cmd
            range_delete(buffer, &range, &cmd);
            buffer->position_y = change->y;
            buffer->position_x = change->x;
        }
        history_push_undo(buffer->history, change);
        editor_did_change_buffer();
    }

    is_undo_redo_active = 0;
    pthread_mutex_unlock(&editor_mutex);
}

Buffer *editor_get_active_buffer(void) {
    return buffer;
}
