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
#include <math.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
#include <limits.h>
#include <libgen.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/inotify.h>
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
#include "str.h"

static pthread_mutex_t editor_mutex = PTHREAD_MUTEX_INITIALIZER;
Editor editor;
static int is_undo_redo_active = 0;

int (*editor_handle_input)(const char *);

#define buffer (editor.buffers[editor.active_buffer_idx])

static char get_char_at(BufferLine *line, int char_x) {
    if (char_x >= line->char_count) {
        return '\0';
    }
    char *p = line->text;
    int i = 0;
    while (*p && i < char_x) {
        p += utf8_char_len(p);
        i++;
    }
    return *p;
}

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

void editor_set_style(Style *style, int fg, int bg) {
    char escape_seq[128];
    char *p = escape_seq;
    p += sprintf(p, "\x1b[");


    if (fg) {
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

void check_for_config_reload() {
    if (atomic_exchange(&editor.config_reloaded_requested, 0)) {
        config_destroy(&editor.config);
        config_init(&editor.config);
        config_load(&editor.config);
        log_info("editor.check_for_config_reload: reloading config");
        config_load_theme(editor.config.theme, &editor.current_theme);
        buffer->needs_draw = 1;
    }
}

void editor_clear_screen() {
    printf("\033[2J\033[H");
}

void editor_set_cursor_shape(int shape_code) {
    printf("\x1b[%d q", shape_code);
}

typedef struct {
    int len;
    int count;
    char str[8];
    Style style;
} DiagnosticCell;

static void diagnostic_cell_init(DiagnosticCell *cell, Style *style) {
    cell->len = 0;
    cell->count = 0;
    cell->style.fg_r = style->fg_r;
    cell->style.fg_g = style->fg_g;
    cell->style.fg_b = style->fg_b;
}

static void diagnostic_cell_add(DiagnosticCell *cell) {
    cell->count++;
    cell->len = (int)floor(log10(cell->count)) + 4;
}

static void draw_diagnostic_cell(DiagnosticCell *cell, Style *statusline_text) {
    if (!cell->count) return;
    editor_set_style(&cell->style, 1, 0);    
    printf(" ● ");
    editor_set_style(statusline_text, 1, 1);    
    // log_info("draw diag cell, %d %d", cell->count, cell->len);
    printf("%d", cell->count);
}

void draw_statusline(Diagnostic *file_diagnostics, int file_diagnostic_count, Diagnostic *workspace_diagnostics, int workspace_diagnostic_count) {
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

    char line_count[16];
    int line_count_len = snprintf(line_count, sizeof(line_count), " %d ", buffer->line_count);

    int mode_len = strlen(mode);
    int file_name_len = 0;
    if (buffer->file_name) {
        file_name_len = strlen(buffer->file_name);
    }
    if (buffer->dirty) {
        file_name_len += buffer->file_name ? 4 : 3;
    }

    char branch_name[256];
    git_current_branch(branch_name, sizeof(branch_name));
    int branch_name_len = strlen(branch_name);
    if (branch_name_len) {
        branch_name_len += 1;
    }

    DiagnosticCell ws_errors;
    DiagnosticCell ws_warnings;
    DiagnosticCell ws_infos;
    DiagnosticCell ws_hints;
    diagnostic_cell_init(&ws_errors, &editor.current_theme.diagnostics_error);
    diagnostic_cell_init(&ws_warnings, &editor.current_theme.diagnostics_warning);
    diagnostic_cell_init(&ws_infos, &editor.current_theme.diagnostics_info);
    diagnostic_cell_init(&ws_hints, &editor.current_theme.diagnostics_hint);
    for (int i = 0; i < workspace_diagnostic_count; i++) {
        switch (workspace_diagnostics[i].severity) {
            case LSP_DIAGNOSTIC_SEVERITY_ERROR:
                diagnostic_cell_add(&ws_errors);
                break;
            case LSP_DIAGNOSTIC_SEVERITY_WARNING:
                diagnostic_cell_add(&ws_warnings);
                break;
            case LSP_DIAGNOSTIC_SEVERITY_INFO:
                diagnostic_cell_add(&ws_infos);
                break;
            case LSP_DIAGNOSTIC_SEVERITY_HINT:
                diagnostic_cell_add(&ws_hints);
                break;
        }
    }
    DiagnosticCell file_errors;
    DiagnosticCell file_warnings;
    DiagnosticCell file_infos;
    DiagnosticCell file_hints;
    diagnostic_cell_init(&file_errors, &editor.current_theme.diagnostics_error);
    diagnostic_cell_init(&file_warnings, &editor.current_theme.diagnostics_warning);
    diagnostic_cell_init(&file_infos, &editor.current_theme.diagnostics_info);
    diagnostic_cell_init(&file_hints, &editor.current_theme.diagnostics_hint);
    for (int i = 0; i < file_diagnostic_count; i++) {
        switch (file_diagnostics[i].severity) {
            case LSP_DIAGNOSTIC_SEVERITY_ERROR:
                diagnostic_cell_add(&file_errors);
                break;
            case LSP_DIAGNOSTIC_SEVERITY_WARNING:
                diagnostic_cell_add(&file_warnings);
                break;
            case LSP_DIAGNOSTIC_SEVERITY_INFO:
                diagnostic_cell_add(&file_infos);
                break;
            case LSP_DIAGNOSTIC_SEVERITY_HINT:
                diagnostic_cell_add(&file_hints);
                break;
        }
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
        int workspace_diagnostic_len = ws_errors.len + ws_warnings.len + ws_infos.len + ws_hints.len;
        int file_diagnostic_len = file_errors.len + file_warnings.len + file_infos.len + file_hints.len;
        int left_len = mode_len + branch_name_len + workspace_diagnostic_len;
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
        int half_file_diagnostics_len = file_diagnostic_len / 2;
        int left_space = half_cols - left_len - half_file_name_len - half_file_diagnostics_len;
        int right_space = (editor.screen_cols - half_cols) - right_len - (file_name_len - half_file_name_len) - (file_diagnostic_len - half_file_diagnostics_len);

        if (branch_name_len) {
            printf(" %s", branch_name);
        }

        draw_diagnostic_cell(&ws_errors, &editor.current_theme.statusline_text);
        draw_diagnostic_cell(&ws_warnings, &editor.current_theme.statusline_text);
        draw_diagnostic_cell(&ws_infos, &editor.current_theme.statusline_text);
        draw_diagnostic_cell(&ws_hints, &editor.current_theme.statusline_text);
        for (int i = 0; i < left_space; i++) putchar(' ');
        if (buffer->file_name) {
            printf("%s", buffer->file_name);
        }

        if (buffer->dirty) {
            if (buffer->file_name) {
                printf(" [+]");
            } else {
                printf("[+]");
            }
        }
        draw_diagnostic_cell(&file_errors, &editor.current_theme.statusline_text);
        draw_diagnostic_cell(&file_warnings, &editor.current_theme.statusline_text);
        draw_diagnostic_cell(&file_infos, &editor.current_theme.statusline_text);
        draw_diagnostic_cell(&file_hints, &editor.current_theme.statusline_text);

        for (int i = 0; i < right_space; i++) putchar(' ');

        if (search_stats_len > 0) {
            printf("%s ", search_stats);
        }
        printf("%s%s", position, line_count);
    }
    printf("\x1b[0m");
}

static int is_in_selection(int y, int x) {
    int start_y = buffer->selection_start_y;
    int start_x = buffer->selection_start_x;
    int end_y = buffer->position_y;
    int end_x = buffer->position_x;

    if (buffer->visual_mode == VISUAL_MODE_LINE) {
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

void draw_buffer(Diagnostic *diagnostics, int diagnostics_count) {
    uint32_t start_byte = 0;
    for (int i = 0; i < buffer->offset_y; i++) {
        start_byte += buffer->lines[i]->text_len + 1; // +1 for newline
    }

    char utf8_buf[8];
    char line_num_str[16];

    for (int row = buffer->offset_y; row < buffer->offset_y + editor.screen_rows - 1; row++) {
        int relative_y = row - buffer->offset_y;
        printf("\x1b[%d;1H", relative_y + 1);
        int line_num_len = snprintf(line_num_str, sizeof(line_num_str), "%*d ", buffer->line_num_width - 1, row + 1);

        if (row >= buffer->line_count) {
            int chars_to_print = editor.screen_cols;
            if (buffer->offset_x) {
                editor_set_style(&editor.current_theme.content_line_number_sticky, 0, 1);
                for (int i = 0; i < line_num_len; i++) {
                    putchar(' ');
                    chars_to_print--;
                }
            }
            editor_set_style(&editor.current_theme.content_background, 0, 1);
            while (chars_to_print > 0) {
                putchar(' ');
                chars_to_print--;
            }
            continue;
        }

        BufferLine *line = buffer->lines[row];

        if (line->needs_highlight) {
            buffer_line_apply_syntax_highlighting(buffer, line, start_byte, &editor.current_theme);
        }

        Style *char_styles = NULL;
        if (line->char_count) {
            char_styles = malloc(sizeof(Style) * line->char_count);
            if (char_styles == NULL) continue;
        }
        if (line->char_count > 0) {
            int run_idx = 0;
            int char_in_run = 0;
            for (int i = 0; i < line->char_count; i++) {
                if (run_idx < line->highlight_runs_count) {
                    char_styles[i] = line->highlight_runs[run_idx].style;
                    char_in_run++;
                    if (char_in_run >= line->highlight_runs[run_idx].count) {
                        run_idx++;
                        char_in_run = 0;
                    }
                } else {
                    char_styles[i] = editor.current_theme.syntax_variable;
                }
            }
        }

        for (int i = 0; i < diagnostics_count; i++) {
            if (diagnostics[i].line == row) {
                for (int j = diagnostics[i].col_start; j < diagnostics[i].col_end && j < line->char_count; j++) {
                    char_styles[j].style |= STYLE_UNDERLINE;
                }
            }
        }

        if (buffer->offset_x) {
            editor_set_style(&editor.current_theme.content_line_number_sticky, 1, 1);
        } else if (row == buffer->position_y) {
            editor_set_style(&editor.current_theme.content_line_number_active, 1, 1);
        } else {
            editor_set_style(&editor.current_theme.content_line_number, 1, 1);
        }

        DiagnosticSeverity highest_severity = 0;
        for (int i = 0; i < diagnostics_count; i++) {
            if (diagnostics[i].line == row) {
                if (highest_severity == 0 || diagnostics[i].severity < highest_severity) {
                    highest_severity = diagnostics[i].severity;
                }
            }
        }

        printf(" ");
        if (highest_severity) {
            Style *diag_style;
            switch (highest_severity) {
                case LSP_DIAGNOSTIC_SEVERITY_ERROR:
                    diag_style = &editor.current_theme.diagnostics_error;
                    break;
                case LSP_DIAGNOSTIC_SEVERITY_WARNING:
                    diag_style = &editor.current_theme.diagnostics_warning;
                    break;
                case LSP_DIAGNOSTIC_SEVERITY_INFO:
                    diag_style = &editor.current_theme.diagnostics_info;
                    break;
                case LSP_DIAGNOSTIC_SEVERITY_HINT:
                    diag_style = &editor.current_theme.diagnostics_hint;
                    break;
                default:
                    diag_style = &editor.current_theme.content_line_number;
                    break;
            }
            editor_set_style(diag_style, 1, 0);
            printf("●");
            if (buffer->offset_x) {
                editor_set_style(&editor.current_theme.content_line_number_sticky, 1, 1);
            } else if (row == buffer->position_y) {
                editor_set_style(&editor.current_theme.content_line_number_active, 1, 1);
            } else {
                editor_set_style(&editor.current_theme.content_line_number, 1, 1);
            }
        } else {
            printf(" ");
        }

        int deleted_lines = 0;
        GitLineStatus git_status = git_get_line_status(buffer, row, &deleted_lines);

        Style* git_style = NULL;
        const char* git_char = " ";
        switch (git_status) {
            case GIT_LINE_ADDED:
                git_style = &editor.current_theme.git_added;
                git_char = "▐";
                break;
            case GIT_LINE_MODIFIED:
                git_style = &editor.current_theme.git_modified;
                git_char = "▐";
                break;
            default:
                if (deleted_lines > 0) {
                    git_style = &editor.current_theme.git_deleted;
                    git_char = "▔";
                }
                break;
        }

        if (git_style) {
            editor_set_style(git_style, 1, 0);
            printf("%s", git_char);
            if (buffer->offset_x) {
                editor_set_style(&editor.current_theme.content_line_number_sticky, 1, 1);
            } else if (row == buffer->position_y) {
                editor_set_style(&editor.current_theme.content_line_number_active, 1, 1);
            } else {
                editor_set_style(&editor.current_theme.content_line_number, 1, 1);
            }
        } else {
            printf(" ");
        }

        printf("%.*s", line_num_len, line_num_str);

        int is_visual_mode = editor_handle_input == visual_handle_input;
        Style *line_style = (row == buffer->position_y) ? &editor.current_theme.content_cursor_line : &editor.current_theme.content_background;

        int cols_to_skip = buffer->offset_x;
        int chars_to_print = editor.screen_cols - buffer->line_num_width - 2;
        int visual_x = 0;

        char *p = line->text;
        for (int ch_idx = 0; ch_idx < line->char_count && chars_to_print > 0; ch_idx++) {
            int char_len = utf8_char_len(p);
            strncpy(utf8_buf, p, char_len);
            utf8_buf[char_len] = '\0';

            int current_char_width;
            if (utf8_buf[0] == '\t') {
                current_char_width = buffer->tab_width - (visual_x % buffer->tab_width);
            } else {
                current_char_width = utf8_char_width(utf8_buf);
            }
            if (cols_to_skip > 0) {
                if (cols_to_skip >= current_char_width) {
                    cols_to_skip -= current_char_width;
                    visual_x += current_char_width;
                    p += char_len;
                    continue;
                }
            }

            Style final_style = char_styles[ch_idx];
            Style* base_style = line_style;
            int in_selection = is_visual_mode && is_in_selection(row, ch_idx);
            int in_search = is_in_search_match(row, ch_idx);

            if (in_search) {
                base_style = &editor.current_theme.search_match;
            } else if (in_selection) {
                base_style = &editor.current_theme.content_selection;
            }
            final_style.bg_r = base_style->bg_r;
            final_style.bg_g = base_style->bg_g;
            final_style.bg_b = base_style->bg_b;

            if (utf8_buf[0] == '\t' || utf8_buf[0] == ' ') {
                int is_trailing = 1;
                char *end_p = p + char_len;
                while (*end_p) {
                    if (*end_p != ' ' && *end_p != '\t') {
                        is_trailing = 0;
                        break;
                    }
                    end_p++;
                }

                if ((utf8_buf[0] == '\t' && (editor.config.whitespace.tab == WHITESPACE_RENDER_ALL || (editor.config.whitespace.tab == WHITESPACE_RENDER_TRAILING && is_trailing))) ||
                    (utf8_buf[0] == ' ' && (editor.config.whitespace.space == WHITESPACE_RENDER_ALL || (editor.config.whitespace.space == WHITESPACE_RENDER_TRAILING && is_trailing)))) {

                    Style whitespace_style = editor.current_theme.content_whitespace;
                    whitespace_style.bg_r = final_style.bg_r;
                    whitespace_style.bg_g = final_style.bg_g;
                    whitespace_style.bg_b = final_style.bg_b;
                    editor_set_style(&whitespace_style, 1, 1);

                    const char* symbol = (utf8_buf[0] == '\t') ? editor.config.whitespace.tab_char : editor.config.whitespace.space_char;
                    printf("%s", symbol);

                    if (utf8_buf[0] == '\t') {
                        editor_set_style(base_style, 0, 1);
                        for (int i = 0; i < current_char_width - 1 && chars_to_print > 0; i++) {
                            printf(" ");
                        }
                    }
                } else {
                    editor_set_style(base_style, 1, 1);
                    for (int i = 0; i < current_char_width; i++) {
                        printf(" ");
                    }
                }
            } else {
                editor_set_style(&final_style, 1, 1);
                printf("%s", utf8_buf);
            }

            p += char_len;
            visual_x += current_char_width;
            chars_to_print -= current_char_width;
        }

        editor_set_style(line_style, 1, 1);
        while (chars_to_print > 0) {
            putchar(' ');
            chars_to_print--;
        }
        start_byte += line->text_len + 1;
        if (char_styles) {
            free(char_styles);
        }
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
        buffer_update_git_diff(buffer);
    }
    Diagnostic *diagnostics = NULL;
    int diagnostic_count = 0;
    if (buffer->file_name) {
        buffer->diagnostics_version = lsp_get_diagnostics(buffer->file_name, &diagnostics, &diagnostic_count);
    }

    Diagnostic *workspace_diagnostics = NULL;
    int workspace_diagnostic_count = lsp_get_all_diagnostics(&workspace_diagnostics);

    editor_clear_screen();
    draw_buffer(diagnostics, diagnostic_count);
    draw_statusline(diagnostics, diagnostic_count, workspace_diagnostics, workspace_diagnostic_count);
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
    if (workspace_diagnostics) {
        for (int i = 0; i < workspace_diagnostic_count; i++) {
            free(workspace_diagnostics[i].message);
            free(workspace_diagnostics[i].uri);
        }
        free(workspace_diagnostics);
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

    if (buffer->file_name) {
        const char *lang_name = str_get_lang_name_from_file_name(buffer->file_name);
        if (lsp_is_running(lang_name)) {
            char *content = buffer_get_content(buffer);
            if (content) {
                char absolute_path[PATH_MAX];
                if (realpath(buffer->file_name, absolute_path) != NULL) {
                    lsp_did_change(absolute_path, content, buffer->version);
                }
                free(content);
            }
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
        check_for_config_reload();
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
    int byte_pos_x = buffer_get_byte_position_x(buffer);

    uint32_t start_byte = 0;
    for (int i = 0; i < buffer->position_y; i++) {
        start_byte += buffer->lines[i]->text_len + 1;
    }
    start_byte += byte_pos_x;

    buffer_realloc_lines_for_capacity(buffer);

    memmove(&buffer->lines[buffer->position_y + 2],
            &buffer->lines[buffer->position_y + 1],
            (buffer->line_count - buffer->position_y - 1) * sizeof(BufferLine *));

    BufferLine *new_line = (BufferLine *)malloc(sizeof(BufferLine));
    if (new_line == NULL) {
        log_error("editor.editor_insert_new_line: failed to allocate BufferLine");
        exit(1);
    }
    buffer_line_init(new_line);
    buffer->lines[buffer->position_y + 1] = new_line;

    int bytes_to_move = current_line->text_len - byte_pos_x;
    if (bytes_to_move > 0) {
        buffer_line_realloc_for_capacity(new_line, bytes_to_move + 1);
        memcpy(new_line->text, current_line->text + byte_pos_x, bytes_to_move);
        new_line->text[bytes_to_move] = '\0';
        new_line->text_len = bytes_to_move;
        new_line->char_count = current_line->char_count - buffer->position_x;

        current_line->text[byte_pos_x] = '\0';
        current_line->text_len = byte_pos_x;
        current_line->char_count = buffer->position_x;
    }

    if (buffer->parser) {
        current_line->needs_highlight = 1;
        new_line->needs_highlight = 1;
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

void editor_open_and_jump_to_line(const char *file_path, int line, int col) {
    editor_open((char *)file_path);
    Buffer *buf = editor_get_active_buffer();
    if (line > 0 && line <= buf->line_count) {
        buf->position_y = line - 1;
    }
    if (col > 0 && col <= buf->lines[buf->position_y]->char_count + 1) {
        buf->position_x = col - 1;
    } else {
        buf->position_x = 0;
    }
    editor_center_view();
    editor_request_redraw();
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

static void editor_setup_lsp(const char *file_name) {
    if (!file_name) {
        return;
    }
    const char *lang_name = str_get_lang_name_from_file_name(file_name);
    if (!lang_name) {
        return;
    }

    char absolute_path[PATH_MAX];
    if (realpath(file_name, absolute_path) != NULL) {
        lsp_init(&editor.config, file_name);
        if (lsp_is_running(lang_name)) {
            char *content = buffer_get_content(buffer);
            if (content) {
                lsp_did_open(absolute_path, lang_name, content);
                free(content);
            }
        }
    }
}

void *watch_config_file(void *arg __attribute__((unused))) {
    char *path = config_get_path();
    if (!path) {
        log_error("watch_config_file: config_get_path failed");
        return NULL;
    }

    char *dir_path = dirname(path);
    if (!dir_path) {
        log_error("watch_config_file: dirname failed");
        free(path);
        return NULL;
    }

    int fd = inotify_init();
    if (fd < 0) {
        log_error("watch_config_file: inotify_init failed");
        free(path);
        return NULL;
    }

    int wd = inotify_add_watch(fd, dir_path, IN_MODIFY | IN_CREATE | IN_MOVED_TO | IN_CLOSE_WRITE);
    if (wd < 0) {
        log_error("watch_config_file: inotify_add_watch failed for %s", dir_path);
        free(path);
        close(fd);
        return NULL;
    }

    char read_buffer[4096];
    while (1) {
        ssize_t len = read(fd, read_buffer, sizeof(read_buffer));
        if (len < 0) {
            log_error("watch_config_file: read failed");
            break;
        }

        for (char *p = read_buffer; p < read_buffer + len; ) {
            struct inotify_event *event = (struct inotify_event *)p;
            if (event->len) {
                if (strcmp(event->name, "config.toml") == 0) {
                    atomic_store(&editor.config_reloaded_requested, 1);
                }
            }
            p += sizeof(struct inotify_event) + event->len;
        }
    }

    inotify_rm_watch(fd, wd);
    close(fd);
    free(path);
    return NULL;
}

void editor_init(char *file_name, bool benchmark_mode) {
    if (!benchmark_mode) {
        init_terminal_size();
        setup_terminal();
    }
    atomic_store(&editor.config_reloaded_requested, 0);
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
    pthread_t config_watch_thread_id;
    editor_mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    if (pthread_create(&render_thread_id, NULL, render_loop, NULL) != 0) {
        log_error("editor.editor_start: unable to create render thread");
        exit(1);
    }
    if (pthread_create(&config_watch_thread_id, NULL, watch_config_file, NULL) != 0) {
        log_error("editor.editor_start: unable to create config watch thread");
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
    int byte_pos_x = buffer_get_byte_position_x(buffer);

    buffer_line_realloc_for_capacity(line, line->text_len + ch_len + 1);

    if (byte_pos_x < line->text_len) {
        memmove(line->text + byte_pos_x + ch_len,
                line->text + byte_pos_x,
                line->text_len - byte_pos_x);
    }

    memcpy(line->text + byte_pos_x, ch, ch_len);

    line->text_len += ch_len;
    line->text[line->text_len] = '\0';
    line->char_count++;

    editor_add_insertion_to_history(ch);
    buffer->position_x++;
    buffer_reset_offset_x(buffer, editor.screen_cols);
    editor_did_change_buffer();

    if (buffer->parser && buffer->tree) {
        uint32_t start_byte = 0;
        for (int i = 0; i < buffer->position_y; i++) {
            start_byte += buffer->lines[i]->text_len + 1;
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
        fputs(line->text, fp);
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
    buffer_update_git_diff(buffer);

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
    int byte_pos_x = buffer_get_byte_position_x(buffer);

    uint32_t start_byte = 0;
    for (int i = 0; i < buffer->position_y; i++) {
        start_byte += buffer->lines[i]->text_len + 1;
    }
    start_byte += byte_pos_x;

    if (buffer->position_x == line->char_count) {
        if (buffer->position_y == buffer->line_count - 1) {
            pthread_mutex_unlock(&editor_mutex);
            return;
        }
        if (!is_undo_redo_active) {
            history_add_change(buffer->history, CHANGE_TYPE_DELETE, buffer->position_y, buffer->position_x, "\n");
        }

        BufferLine *next_line = buffer->lines[buffer->position_y + 1];
        if (buffer->parser) {
            next_line->needs_highlight = 1;
        }

        int new_text_len = line->text_len + next_line->text_len;
        buffer_line_realloc_for_capacity(line, new_text_len + 1);
        memcpy(line->text + line->text_len, next_line->text, next_line->text_len);
        line->text[new_text_len] = '\0';
        line->text_len = new_text_len;
        line->char_count += next_line->char_count;

        buffer_line_destroy(next_line);
        free(next_line);

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
        int char_len = utf8_char_len(line->text + byte_pos_x);
        if (!is_undo_redo_active) {
            char deleted_char_str[8];
            strncpy(deleted_char_str, line->text + byte_pos_x, char_len);
            deleted_char_str[char_len] = '\0';
            history_add_change(buffer->history, CHANGE_TYPE_DELETE, buffer->position_y, buffer->position_x, deleted_char_str);
        }

        memmove(line->text + byte_pos_x,
                line->text + byte_pos_x + char_len,
                line->text_len - byte_pos_x - char_len);
        line->text_len -= char_len;
        line->text[line->text_len] = '\0';
        line->char_count--;

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
    int byte_pos_x = buffer_get_byte_position_x(buffer);

    uint32_t start_byte = 0;
    for (int i = 0; i < buffer->position_y; i++) {
        start_byte += buffer->lines[i]->text_len + 1;
    }
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

        int prev_line_char_count = prev_line->char_count;
        int new_text_len = prev_line->text_len + line->text_len;
        buffer_line_realloc_for_capacity(prev_line, new_text_len + 1);
        memcpy(prev_line->text + prev_line->text_len, line->text, line->text_len);
        prev_line->text[new_text_len] = '\0';
        prev_line->text_len = new_text_len;
        prev_line->char_count += line->char_count;

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
                .start_point = { (uint32_t)buffer->position_y -1, (uint32_t)prev_line->text_len },
                .old_end_point = { (uint32_t)buffer->position_y, 0 },
                .new_end_point = { (uint32_t)buffer->position_y - 1, (uint32_t)prev_line->text_len }
            });
        }
        buffer->position_y--;
        buffer_reset_offset_y(buffer, editor.screen_rows);
        buffer->position_x = prev_line_char_count;
        buffer_line_destroy(line);
        free(line);

    } else {
        buffer->position_x--;
        byte_pos_x = buffer_get_byte_position_x(buffer);
        int char_len = utf8_char_len(line->text + byte_pos_x);

        if (!is_undo_redo_active) {
            char deleted_char_str[8];
            strncpy(deleted_char_str, line->text + byte_pos_x, char_len);
            deleted_char_str[char_len] = '\0';
            history_add_change(buffer->history, CHANGE_TYPE_DELETE, buffer->position_y, buffer->position_x, deleted_char_str);
        }

        memmove(line->text + byte_pos_x,
                line->text + byte_pos_x + char_len,
                line->text_len - byte_pos_x - char_len);
        line->text_len -= char_len;
        line->text[line->text_len] = '\0';
        line->char_count--;

        if (buffer->parser && buffer->tree) {
            line->needs_highlight = 1;
            ts_tree_edit(buffer->tree, &(TSInputEdit){
                .start_byte = start_byte - char_len,
                .old_end_byte = start_byte,
                .new_end_byte = start_byte - char_len,
                .start_point = { (uint32_t)buffer->position_y, (uint32_t)(byte_pos_x) },
                .old_end_point = { (uint32_t)buffer->position_y, (uint32_t)(byte_pos_x + char_len) },
                .new_end_point = { (uint32_t)buffer->position_y, (uint32_t)(byte_pos_x) }
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
        while (is_whitespace(get_char_at(line, range->x_end))) {
            range_expand_right(&line, range);
            moved = 1;
        }
        int touched = 0;
        if (!is_word_char(get_char_at(line, range->x_end))) {
            if (moved) {
                continue;
            }
            while (range->x_end < line->char_count - 1 && !is_word_char(get_char_at(line, range->x_end + 1)) && !is_whitespace(get_char_at(line, range->x_end + 1))) {
                touched = 1;
                range_expand_right(&line, range);
            }
        }
        if (touched) {
            continue;
        }
        if (!is_word_char(get_char_at(line, range->x_end))) {
            continue;
        }
        while (range->x_end < line->char_count - 1 && is_word_char(get_char_at(line, range->x_end + 1))) {
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
        while (is_whitespace(get_char_at(line, range->x_end))) {
            range_expand_right(&line, range);
        }
        while (range->x_end < line->char_count - 1 && !is_whitespace(get_char_at(line, range->x_end + 1))) {
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
        while (is_whitespace(get_char_at(line, range->x_end))) {
            range_expand_left(&line, range);
        }
        int touched = 0;
        if (!is_word_char(get_char_at(line, range->x_end))) {
            while (range->x_end > 0 && !is_word_char(get_char_at(line, range->x_end - 1)) && !is_whitespace(get_char_at(line, range->x_end - 1))) {
                touched = 1;
                range_expand_left(&line, range);
            }
        }
        if (touched) {
            continue;
        }
        if (!is_word_char(get_char_at(line, range->x_end))) {
            continue;
        }
        while (range->x_end > 0 && is_word_char(get_char_at(line, range->x_end - 1))) {
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
        while (is_whitespace(get_char_at(line, range->x_end))) {
            range_expand_left(&line, range);
        }
        while (range->x_end > 0 && !is_whitespace(get_char_at(line, range->x_end - 1))) {
            range_expand_left(&line, range);
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
                int original_y = y;
                int count = cmd->count ? cmd->count : 1;
                for (int i = 0; i < count; i++) {
                    int last_y = y;
                    // find next empty line
                    while (y < buffer->line_count - 1 && !is_line_empty(buffer->lines[y])) {
                        y++;
                    }
                    // find next non-empty line
                    while (y < buffer->line_count - 1 && is_line_empty(buffer->lines[y])) {
                        y++;
                    }
                    if (y == last_y) { // stuck at the end
                        break;
                    }
                }

                if (y == original_y) { // Didn't move at all, wrap around
                    y = 0;
                    while (y < buffer->line_count - 1 && is_line_empty(buffer->lines[y])) {
                        y++;
                    }
                }

                range->y_end = y;
                range->x_end = 0;
            }
            if (strcmp(cmd->specifier, "d") == 0) {
                Diagnostic *diagnostics = NULL;
                int diagnostic_count = 0;
                if (buffer->file_name) {
                    lsp_get_diagnostics(buffer->file_name, &diagnostics, &diagnostic_count);
                }
                if (diagnostic_count > 0) {
                    int next_diag_y = -1;
                    int next_diag_x = -1;

                    for (int i = 0; i < diagnostic_count; i++) {
                        if (diagnostics[i].line > buffer->position_y || (diagnostics[i].line == buffer->position_y && diagnostics[i].col_start > buffer->position_x)) {
                            if (next_diag_y == -1 || diagnostics[i].line < next_diag_y || (diagnostics[i].line == next_diag_y && diagnostics[i].col_start < next_diag_x)) {
                                next_diag_y = diagnostics[i].line;
                                next_diag_x = diagnostics[i].col_start;
                            }
                        }
                    }

                    if (next_diag_y == -1) { // Wrap around
                        for (int i = 0; i < diagnostic_count; i++) {
                            if (next_diag_y == -1 || diagnostics[i].line < next_diag_y || (diagnostics[i].line == next_diag_y && diagnostics[i].col_start < next_diag_x)) {
                                next_diag_y = diagnostics[i].line;
                                next_diag_x = diagnostics[i].col_start;
                            }
                        }
                    }

                    if (next_diag_y != -1) {
                        range->y_end = next_diag_y;
                        range->x_end = next_diag_x;
                    }
                }
                if (diagnostics) free(diagnostics);
            }
            if (strcmp(cmd->specifier, "g") == 0) {
                if (buffer->hunk_count > 0) {
                    int next_hunk_y = -1;

                    for (int i = 0; i < buffer->hunk_count; i++) {
                        if (buffer->hunks[i].new_start - 1 > buffer->position_y) {
                            next_hunk_y = buffer->hunks[i].new_start - 1;
                            break;
                        }
                    }

                    if (next_hunk_y == -1) {
                        next_hunk_y = buffer->hunks[0].new_start - 1;
                    }

                    if (next_hunk_y != -1) {
                        range->y_end = next_hunk_y;
                        range->x_end = 0;
                    }
                }
            }
            break;
        case 'p':
            if (cmd->action == 0) {
                if (strcmp(cmd->specifier, "p") == 0) {
                    int y = buffer->position_y;
                    int original_y = y;
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

                    if (y == original_y && y == 0) { // stuck at the beginning
                        y = buffer->line_count - 1;
                        while (y > 0 && is_line_empty(buffer->lines[y])) {
                            y--;
                        }
                        while (y > 0 && !is_line_empty(buffer->lines[y-1])) {
                            y--;
                        }
                    }

                    range->y_end = y;
                    range->x_end = 0;
                }
                if (strcmp(cmd->specifier, "d") == 0) {
                    Diagnostic *diagnostics = NULL;
                    int diagnostic_count = 0;
                    if (buffer->file_name) {
                        lsp_get_diagnostics(buffer->file_name, &diagnostics, &diagnostic_count);
                    }
                    if (diagnostic_count > 0) {
                        int prev_diag_y = -1;
                        int prev_diag_x = -1;

                        for (int i = 0; i < diagnostic_count; i++) {
                            if (diagnostics[i].line < buffer->position_y || (diagnostics[i].line == buffer->position_y && diagnostics[i].col_start < buffer->position_x)) {
                                if (prev_diag_y == -1 || diagnostics[i].line > prev_diag_y || (diagnostics[i].line == prev_diag_y && diagnostics[i].col_start > prev_diag_x)) {
                                    prev_diag_y = diagnostics[i].line;
                                    prev_diag_x = diagnostics[i].col_start;
                                }
                            }
                        }

                        if (prev_diag_y == -1) { // Wrap around
                            for (int i = 0; i < diagnostic_count; i++) {
                                if (prev_diag_y == -1 || diagnostics[i].line > prev_diag_y || (diagnostics[i].line == prev_diag_y && diagnostics[i].col_start > prev_diag_x)) {
                                    prev_diag_y = diagnostics[i].line;
                                    prev_diag_x = diagnostics[i].col_start;
                                }
                            }
                        }

                        if (prev_diag_y != -1) {
                            range->y_end = prev_diag_y;
                            range->x_end = prev_diag_x;
                        }
                    }
                    if (diagnostics) free(diagnostics);
                }
                if (strcmp(cmd->specifier, "g") == 0) {
                    if (buffer->hunk_count > 0) {
                        int prev_hunk_y = -1;

                        for (int i = buffer->hunk_count - 1; i >= 0; i--) {
                            if (buffer->hunks[i].new_start - 1 < buffer->position_y) {
                                prev_hunk_y = buffer->hunks[i].new_start - 1;
                                break;
                            }
                        }

                        if (prev_hunk_y == -1) {
                            prev_hunk_y = buffer->hunks[buffer->hunk_count - 1].new_start - 1;
                        }

                        if (prev_hunk_y != -1) {
                            range->y_end = prev_hunk_y;
                            range->x_end = 0;
                        }
                    }
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
                if (cmd->specifier[0] == 'i') {
                    if (is_word_char(get_char_at(line, buffer->position_x))) {
                        while (range->x_start > 0 && is_word_char(get_char_at(line, range->x_start - 1))) range->x_start--;
                        while (range->x_end < line->char_count - 1 && is_word_char(get_char_at(line, range->x_end + 1))) range->x_end++;
                        range->x_end++;
                    }
                } else if (cmd->specifier[0] == 'a') {
                    if (is_word_char(get_char_at(line, buffer->position_x))) {
                        while (range->x_start > 0 && is_word_char(get_char_at(line, range->x_start - 1))) range->x_start--;
                        while (range->x_end < line->char_count - 1 && is_word_char(get_char_at(line, range->x_end + 1))) range->x_end++;

                        int x_end = range->x_end;
                        while (range->x_end < line->char_count - 1 && is_whitespace(get_char_at(line, range->x_end + 1))) range->x_end++;
                        range->x_end++;
                        if (x_end + 1 == range->x_end) {
                            while (range->x_start > 0 && is_whitespace(get_char_at(line, range->x_start - 1))) range->x_start--;
                        }
                    } else {
                        int end_of_space = buffer->position_x;
                        while(end_of_space < line->char_count && is_whitespace(get_char_at(line, end_of_space))) end_of_space++;
                        if (end_of_space < line->char_count) {
                            range->x_start = buffer->position_x;
                            while(range->x_start > 0 && is_whitespace(get_char_at(line, range->x_start - 1))) range->x_start--;
                            range->x_end = end_of_space;
                            while(range->x_end < line->char_count - 1 && is_word_char(get_char_at(line, range->x_end + 1))) range->x_end++;
                            range->x_end++;
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
                        while (is_whitespace(get_char_at(line, range->x_end))) {
                            range_expand_right(&line, range);
                        }
                    }
                    continue;
                }
                int touched = 0;
                while (!is_word_char(get_char_at(line, range->x_end)) && !is_whitespace(get_char_at(line, range->x_end))) {
                    touched = 1;
                    if (range_expand_right(&line, range)) break;
                }
                if (!touched) {
                    while (is_word_char(get_char_at(line, range->x_end))) {
                        if (range_expand_right(&line, range)) break;
                    }
                }

                while (range->x_end < line->char_count && is_whitespace(get_char_at(line, range->x_end))) {
                    if (range_expand_right(&line, range) && !line->char_count) break;
                }
            }
            break;
        case 'W':
            if (cmd->action) {
                if (cmd->specifier[0] == 'i') {
                    if (!is_whitespace(get_char_at(line, buffer->position_x))) {
                        while (range->x_start > 0 && !is_whitespace(get_char_at(line, range->x_start - 1))) range->x_start--;
                        while (range->x_end < line->char_count - 1 && !is_whitespace(get_char_at(line, range->x_end + 1))) range->x_end++;
                        range->x_end++;
                    }
                } else if (cmd->specifier[0] == 'a') {
                    if (!is_whitespace(get_char_at(line, buffer->position_x))) {
                        while (range->x_start > 0 && !is_whitespace(get_char_at(line, range->x_start - 1))) range->x_start--;
                        while (range->x_end < line->char_count - 1 && !is_whitespace(get_char_at(line, range->x_end + 1))) range->x_end++;

                        int x_end = range->x_end;
                        while (range->x_end < line->char_count - 1 && is_whitespace(get_char_at(line, range->x_end + 1))) range->x_end++;
                        range->x_end++;
                        if (x_end + 1 == range->x_end) {
                            while (range->x_start > 0 && is_whitespace(get_char_at(line, range->x_start - 1))) range->x_start--;
                        }
                    } else {
                        int end_of_space = buffer->position_x;
                        while(end_of_space < line->char_count && is_whitespace(get_char_at(line, end_of_space))) end_of_space++;
                        if (end_of_space < line->char_count) {
                            range->x_start = buffer->position_x;
                            while(range->x_start > 0 && is_whitespace(get_char_at(line, range->x_start - 1))) range->x_start--;
                            range->x_end = end_of_space;
                            while(range->x_end < line->char_count - 1 && !is_whitespace(get_char_at(line, range->x_end + 1))) range->x_end++;
                            range->x_end++;
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
                        while (is_whitespace(get_char_at(line, range->x_end))) {
                            range_expand_right(&line, range);
                        }
                    }
                    continue;
                }
                while (!is_whitespace(get_char_at(line, range->x_end))) {
                    if (range_expand_right(&line, range)) break;
                }

                while (range->x_end < line->char_count && is_whitespace(get_char_at(line, range->x_end))) {
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
                char *p = line->text;
                int char_idx = 0;
                while (*p) {
                    if (char_idx > range->x_end) {
                        if (strncmp(p, cmd->specifier, utf8_char_len(p)) == 0) {
                            range->x_end = char_idx;
                            found = 1;
                            break;
                        }
                    }
                    p += utf8_char_len(p);
                    char_idx++;
                }
                if (!found) break;
            }
            break;
        case 'F':
            for (int i = 0; i < count; i++) {
                int found = 0;
                char *p = line->text;
                int char_idx = 0;
                while (char_idx < range->x_end) {
                    if (strncmp(p, cmd->specifier, utf8_char_len(p)) == 0) {
                        range->x_end = char_idx;
                        found = 1;
                    }
                    p += utf8_char_len(p);
                    char_idx++;
                }
                if (!found) break;
            }
            break;
        case 't':
            for (int i = 0; i < count; i++) {
                int found = 0;
                char *p = line->text;
                int char_idx = 0;
                while (*p) {
                    if (char_idx > range->x_end + 1) {
                        if (strncmp(p, cmd->specifier, utf8_char_len(p)) == 0) {
                            range->x_end = char_idx - 1;
                            found = 1;
                            break;
                        }
                    }
                    p += utf8_char_len(p);
                    char_idx++;
                }
                if (!found) break;
            }
            break;
        case 'T':
            for (int i = 0; i < count; i++) {
                int found = 0;
                char *p = line->text;
                int char_idx = 0;
                while (char_idx < range->x_end -1) {
                    if (strncmp(p, cmd->specifier, utf8_char_len(p)) == 0) {
                        range->x_end = char_idx + 1;
                        found = 1;
                    }
                    p += utf8_char_len(p);
                    char_idx++;
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
    size_t total_len = 0;
    for (int y = top; y <= bottom; y++) {
        BufferLine *line = b->lines[y];
        int start_byte = 0;
        if (y == top) {
            char *p = line->text;
            int char_idx = 0;
            while(*p && char_idx < left) {
                int len = utf8_char_len(p);
                start_byte += len;
                p += len;
                char_idx++;
            }
        }

        int end_byte = line->text_len;
        if (y == bottom) {
            end_byte = 0;
            char *p = line->text;
            int char_idx = 0;
            while(*p && char_idx < right) {
                int len = utf8_char_len(p);
                end_byte += len;
                p += len;
                char_idx++;
            }
        }
        total_len += end_byte - start_byte;
        if (y < bottom) {
            total_len++; // for '\n'
        }
    }

    if (total_len == 0) return NULL;
    char *text = malloc(total_len + 1);
    if (!text) return NULL;
    char *p_text = text;

    for (int y = top; y <= bottom; y++) {
        BufferLine *line = b->lines[y];
        int start_byte = 0;
        if (y == top) {
            char *ptr = line->text;
            int char_idx = 0;
            while(*ptr && char_idx < left) {
                int len = utf8_char_len(ptr);
                start_byte += len;
                ptr += len;
                char_idx++;
            }
        }

        int end_byte = line->text_len;
        if (y == bottom) {
            end_byte = 0;
            char *ptr = line->text;
            int char_idx = 0;
            while(*ptr && char_idx < right) {
                int len = utf8_char_len(ptr);
                end_byte += len;
                ptr += len;
                char_idx++;
            }
        }

        memcpy(p_text, line->text + start_byte, end_byte - start_byte);
        p_text += end_byte - start_byte;

        if (y < bottom) {
            *p_text++ = '\n';
        }
    }
    *p_text = '\0';
    return text;
}

void range_delete(Buffer *b, Range *range, EditorCommand *cmd) {
    int left = range_get_left_boundary(range);
    int right = range_get_right_boundary(range);
    int top = range_get_top_boundary(range);
    int bottom = range_get_bottom_boundary(range);
    if (left == right && top == bottom) return;

    if (cmd->target == 'p' && top == bottom) {
        // This case is for deleting a whole paragraph, which is now handled by the generic logic.
        // For now, we assume this is not a common path and simplify.
    }

    if (!is_undo_redo_active) {
        char *deleted_text = buffer_get_text_in_range(b, top, left, bottom, right);
        if (deleted_text) {
            history_add_change(b->history, CHANGE_TYPE_DELETE, top, left, deleted_text);
            free(deleted_text);
        }
    }

    int left_byte = 0;
    char *p = b->lines[top]->text;
    for(int i=0; i<left; i++) {
        int len = utf8_char_len(p);
        left_byte += len;
        p += len;
    }

    int right_byte = 0;
    p = b->lines[bottom]->text;
    for(int i=0; i<right; i++) {
        int len = utf8_char_len(p);
        right_byte += len;
        p += len;
    }

    if (b->parser && b->tree) {
        uint32_t start_byte_offset = 0;
        for (int i = 0; i < top; i++) {
            start_byte_offset += b->lines[i]->text_len + 1; // +1 for newline
        }
        uint32_t start_byte = start_byte_offset + left_byte;

        uint32_t old_end_byte = start_byte_offset;
        for (int i = top; i < bottom; i++) {
            old_end_byte += b->lines[i]->text_len + 1;
        }
        old_end_byte += right_byte;

        ts_tree_edit(b->tree, &(TSInputEdit){
            .start_byte = start_byte,
            .old_end_byte = old_end_byte,
            .new_end_byte = start_byte,
            .start_point = { (uint32_t)top, (uint32_t)left_byte },
            .old_end_point = { (uint32_t)bottom, (uint32_t)right_byte },
            .new_end_point = { (uint32_t)top, (uint32_t)left_byte }
        });
        b->needs_parse = 1;
    }

    BufferLine *top_line = b->lines[top];
    BufferLine *bottom_line = b->lines[bottom];

    int bottom_remaining_bytes = bottom_line->text_len - right_byte;
    int new_top_len = left_byte + bottom_remaining_bytes;

    char *new_text = malloc(new_top_len + 1);
    memcpy(new_text, top_line->text, left_byte);
    memcpy(new_text + left_byte, bottom_line->text + right_byte, bottom_remaining_bytes);
    new_text[new_top_len] = '\0';

    free(top_line->text);
    top_line->text = new_text;
    top_line->text_len = new_top_len;
    top_line->capacity = new_top_len + 1;
    top_line->char_count = left + (bottom_line->char_count - right);

    if (b->parser) {
        top_line->needs_highlight = 1;
    }

    if (bottom > top) {
        for (int i = top + 1; i <= bottom; i++) {
            buffer_line_destroy(b->lines[i]);
            free(b->lines[i]);
        }

        int lines_to_remove = bottom - top;
        if (b->line_count > bottom + 1) {
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

                    if (!is_undo_redo_active) {
                        char *deleted_text = buffer_get_text_in_range(buffer, top, left, bottom, right);
                        if (deleted_text) {
                            if (bottom < buffer->line_count - 1) {
                                size_t len = strlen(deleted_text);
                                char *text_with_newline = malloc(len + 2);
                                if (text_with_newline) {
                                    strcpy(text_with_newline, deleted_text);
                                    text_with_newline[len] = '\n';
                                    text_with_newline[len+1] = '\0';
                                    history_add_change(buffer->history, CHANGE_TYPE_DELETE, top, left, text_with_newline);
                                    free(text_with_newline);
                                }
                            } else {
                                history_add_change(buffer->history, CHANGE_TYPE_DELETE, top, left, deleted_text);
                            }
                            free(deleted_text);
                        }
                    }

                    if (buffer->parser && buffer->tree) {
                        uint32_t start_byte = 0;
                        for (int i = 0; i < top; i++) {
                            start_byte += buffer->lines[i]->text_len + 1;
                        }

                        uint32_t old_end_byte = start_byte;
                        for (int i = top; i <= bottom; i++) {
                            old_end_byte += buffer->lines[i]->text_len;
                             if (i < buffer->line_count -1) {
                                old_end_byte++;
                            }
                        }

                        ts_tree_edit(buffer->tree, &(TSInputEdit){
                            .start_byte = start_byte,
                            .old_end_byte = old_end_byte,
                            .new_end_byte = start_byte,
                            .start_point = { (uint32_t)top, 0 },
                            .old_end_point = { (uint32_t)bottom + 1, 0 },
                            .new_end_point = { (uint32_t)top, 0 }
                        });
                        buffer->needs_parse = 1;
                    }

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
        if (buffer->history->undo_stack.count == 0) {
            buffer->dirty = 0;
        }
        buffer_reset_offset_x(buffer, editor.screen_cols);
        buffer_reset_offset_y(buffer, editor.screen_rows);
    }

    is_undo_redo_active = 0;
    pthread_mutex_unlock(&editor_mutex);
}

void editor_redo(void) {
    pthread_mutex_lock(&editor_mutex);
    is_undo_redo_active = 1;

    Change *change = history_pop_redo(buffer->history);
    if (change) {
        int end_y, end_x;
        calculate_end_point(change->text, change->y, change->x, &end_y, &end_x);
        if (change->type == CHANGE_TYPE_INSERT) {
            pthread_mutex_unlock(&editor_mutex);
            editor_insert_string_at(change->text, change->y, change->x);
            pthread_mutex_lock(&editor_mutex);
            buffer->position_y = end_y;
            buffer->position_x = end_x;
        } else { // CHANGE_TYPE_DELETE
            Range range = { .y_start = change->y, .x_start = change->x, .y_end = end_y, .x_end = end_x };
            EditorCommand cmd = {0}; // dummy cmd
            range_delete(buffer, &range, &cmd);
            buffer->position_y = change->y;
            buffer->position_x = change->x;
        }
        history_push_undo(buffer->history, change);
        editor_did_change_buffer();
        buffer_reset_offset_x(buffer, editor.screen_cols);
        buffer_reset_offset_y(buffer, editor.screen_rows);
    }

    is_undo_redo_active = 0;
    pthread_mutex_unlock(&editor_mutex);
}

Buffer *editor_get_active_buffer(void) {
    return buffer;
}
