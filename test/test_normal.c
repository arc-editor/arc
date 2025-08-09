#include "test.h"
#include "../src/editor.h"
#include "../src/buffer.h"
#include "../src/normal.h"

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
    test_helper("test_daw_with_leading_space", " hello world", 0, 1, "daw", "world");
    test_helper("test_daw_with_trailing_space", "hello world ", 0, 0, "daw", "world ");

    // diW
    test_helper("test_diW_at_start", "hello-world goodbye", 0, 0, "diW", " goodbye");
    test_helper("test_diW_in_middle", "hello-world goodbye", 0, 2, "diW", " goodbye");
    test_helper("test_diW_at_end", "hello-world goodbye", 0, 10, "diW", " goodbye");

    // daW
    test_helper("test_daW_at_start", "hello-world goodbye", 0, 0, "daW", "goodbye");
    test_helper("test_daW_in_middle", "hello-world goodbye", 0, 2, "daW", "goodbye");
    test_helper("test_daW_at_end", "hello-world goodbye", 0, 10, "daW", "goodbye");
}
