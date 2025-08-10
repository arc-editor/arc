#include "test.h"

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
}
