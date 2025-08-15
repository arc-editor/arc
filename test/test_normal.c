#include "test.h"
#include "../src/lsp.h"
#include <stdlib.h>
#include <string.h>

static Diagnostic* mock_diagnostics = NULL;
static int mock_diagnostic_count = 0;

int lsp_get_diagnostics(const char *file_path, Diagnostic **diagnostics, int *diagnostic_count) {
    (void)file_path;
    if (mock_diagnostic_count == 0) {
        *diagnostics = NULL;
        *diagnostic_count = 0;
        return 0;
    }

    *diagnostics = malloc(sizeof(Diagnostic) * mock_diagnostic_count);
    for (int i = 0; i < mock_diagnostic_count; i++) {
        (*diagnostics)[i] = mock_diagnostics[i];
        // The calling code frees the message, so we need to give it a heap-allocated string.
        if (mock_diagnostics[i].message) {
            (*diagnostics)[i].message = strdup(mock_diagnostics[i].message);
        }
    }
    *diagnostic_count = mock_diagnostic_count;
    return 1;
}

void setup_diagnostics(Diagnostic* diags, int count) {
    mock_diagnostics = diags;
    mock_diagnostic_count = count;
}

void teardown_diagnostics() {
    mock_diagnostics = NULL;
    mock_diagnostic_count = 0;
}

void run_normal_tests(void) {
    printf("--- Normal mode tests ---\n");

    // dw should behave like de
    test_helper("test_dw_at_start_of_word", "hello world", 0, 0, "dw", " world");
    test_helper("test_dw_in_middle_of_word", "hello world", 0, 2, "dw", "he world");

    // dW should behave like dE
    test_helper("test_dW_at_start_of_WORD", "hello-world goodbye", 0, 0, "dW", " goodbye");
    test_helper("test_dW_in_middle_of_WORD", "hello-world goodbye", 0, 2, "dW", "he goodbye");

    // diw
    test_helper("test_diw_at_start", "hello world", 0, 0, "diw", " world");
    test_helper("test_diw_in_middle", "hello world", 0, 2, "diw", " world");
    test_helper("test_diw_at_end", "hello world", 0, 4, "diw", " world");
    test_helper("test_diw_on_space", "hello world", 0, 5, "diw", "hello world"); // Should do nothing

    // daw
    test_helper("test_daw_at_start", "hello world", 0, 0, "daw", "world");
    test_helper("test_daw_in_middle", "hello world", 0, 2, "daw", "world");
    test_helper("test_daw_at_end", "hello world", 0, 4, "daw", "world");
    test_helper("test_daw_on_space_1", "hello  world", 0, 5, "daw", "hello");
    test_helper("test_daw_on_space_2", "hello  world", 0, 6, "daw", "hello");
    test_helper("test_daw_on_trailing_space", "hello ", 0, 5, "daw", "hello ");
    test_helper("test_daw_with_leading_space", " hello world", 0, 1, "daw", " world");
    test_helper("test_daw_with_trailing_space", "hello world ", 0, 0, "daw", "world ");
    test_helper("test_daw_in_middle_1", "foo bar baz", 0, 5, "daw", "foo baz");
    test_helper("test_daw_in_middle_2", "foo bar  baz", 0, 5, "daw", "foo baz");
    test_helper("test_daw_in_middle_3", "foo  bar baz", 0, 6, "daw", "foo  baz");

    // daW
    test_helper("test_daW_at_start", "hello world", 0, 0, "daW", "world");
    test_helper("test_daW_in_middle", "hello world", 0, 2, "daW", "world");
    test_helper("test_daW_at_end", "hello world", 0, 4, "daW", "world");
    test_helper("test_daW_on_space_1", "hello  world", 0, 5, "daW", "hello");
    test_helper("test_daW_on_space_2", "hello  world", 0, 6, "daW", "hello");
    test_helper("test_daW_on_trailing_space", "hello ", 0, 5, "daW", "hello ");
    test_helper("test_daW_with_leading_space", " hello world", 0, 1, "daW", " world");
    test_helper("test_daW_with_trailing_space", "hello world ", 0, 0, "daW", "world ");
    test_helper("test_daW_in_middle_1", "foo bar baz", 0, 5, "daW", "foo baz");
    test_helper("test_daW_in_middle_2", "foo bar  baz", 0, 5, "daW", "foo baz");
    test_helper("test_daW_in_middle_3", "foo  bar baz", 0, 6, "daW", "foo  baz");

    // diW
    test_helper("test_diW_at_start", "hello-world goodbye", 0, 0, "diW", " goodbye");
    test_helper("test_diW_in_middle", "hello-world goodbye", 0, 2, "diW", " goodbye");
    test_helper("test_diW_at_end", "hello-world goodbye", 0, 10, "diW", " goodbye");

    // daW
    test_helper("test_daW_at_start", "hello-world goodbye", 0, 0, "daW", "goodbye");
    test_helper("test_daW_in_middle", "hello-world goodbye", 0, 2, "daW", "goodbye");
    test_helper("test_daW_at_end", "hello-world goodbye", 0, 10, "daW", "goodbye");

    test_visual_motion_helper("test_visual_swap_selection", "hello world", 0, 2, "ll;", 0, 4, 0, 2);

    // o and O
    test_helper("test_o_in_middle_of_line", "hello\nworld", 0, 2, "o", "hello\n\nworld");
    test_helper("test_O_in_middle_of_line", "hello\nworld", 1, 2, "O", "hello\n\nworld");
    test_helper("test_o_at_end_of_file", "hello", 0, 5, "o", "hello\n");
    test_helper("test_O_at_start_of_file", "hello", 0, 0, "O", "\nhello");
    // s
    test_helper("test_s_at_start_of_word", "hello world", 0, 0, "sH", "Hello world");
    test_helper("test_s_in_middle_of_word", "hello world", 0, 2, "sL", "heLlo world");
    test_helper("test_s_at_end_of_word", "hello world", 0, 4, "sO", "hellO world");

    // S
    test_helper("test_S_on_line_with_content", "hello world\ngoodbye", 0, 3, "SNew", "New\ngoodbye");
    test_helper("test_S_on_empty_line", "hello\n\nworld", 1, 0, "SNew", "hello\nNew\nworld");
    // J
    test_helper("test_J_join_lines", "hello\nworld", 0, 2, "J", "hello world");
    test_helper("test_J_on_last_line", "hello", 0, 2, "J", "hello");

    // Diagnostic navigation
    printf("--- Diagnostic navigation tests ---\n");
    Diagnostic diags1[] = {{.line = 2, .col_start = 3, .message = "error"}};
    setup_diagnostics(diags1, 1);
    test_motion_helper("test_nd_simple_move", "line1\nline2\nline3\nline4\nline5", 0, 0, "nd", 2, 3);
    teardown_diagnostics();

    Diagnostic diags2[] = {{.line = 1, .col_start = 2, .message = "error"}};
    setup_diagnostics(diags2, 1);
    test_motion_helper("test_pd_simple_move", "line1\nline2\nline3\nline4\nline5", 3, 0, "pd", 1, 2);
    teardown_diagnostics();

    Diagnostic diags3[] = {{.line = 3, .col_start = 1, .message = "error"}, {.line = 1, .col_start = 2, .message = "warning"}};
    setup_diagnostics(diags3, 2);
    test_motion_helper("test_nd_finds_closest", "line1\nline2\nline3\nline4\nline5", 1, 0, "nd", 1, 2);
    teardown_diagnostics();

    Diagnostic diags4[] = {{.line = 0, .col_start = 1, .message = "error"}, {.line = 2, .col_start = 2, .message = "warning"}};
    setup_diagnostics(diags4, 2);
    test_motion_helper("test_pd_finds_closest", "line1\nline2\nline3\nline4\nline5", 2, 5, "pd", 2, 2);
    teardown_diagnostics();

    test_motion_helper("test_nd_no_diagnostics", "line1\nline2\nline3", 0, 0, "nd", 0, 0);

    printf("--- Diagnostic navigation wrap around tests ---\n");
    Diagnostic diags5[] = {{.line = 0, .col_start = 1, .message = "error"}};
    setup_diagnostics(diags5, 1);
    test_motion_helper("test_nd_wrap_around", "line1\nline2\nline3", 1, 0, "nd", 0, 1);
    teardown_diagnostics();

    Diagnostic diags6[] = {{.line = 2, .col_start = 3, .message = "error"}};
    setup_diagnostics(diags6, 1);
    test_motion_helper("test_pd_wrap_around", "line1\nline2\nline3", 1, 0, "pd", 2, 3);
    teardown_diagnostics();

    printf("--- Paragraph navigation wrap around tests ---\n");
    test_motion_helper("test_np_wrap_around", "p1\n\np2", 2, 0, "np", 0, 0);
    test_motion_helper("test_pp_wrap_around", "p1\n\np2", 0, 0, "pp", 2, 0);
}
