#include "test.h"

void run_undo_tests(void) {
    printf("--- Undo/Redo tests ---\n");

    // To use editor_insert_char, we need to be in insert mode
    test_helper("test_insert_undo", "start", 0, 0, "ihello\x1bu", "start");
    test_helper("test_insert_undo_redo", "start", 0, 0, "ihello\x1buU", "hellostart");

    test_helper("test_delete_undo", "hello", 0, 0, "x\x1bu", "hello");
    test_helper("test_delete_undo_redo", "hello", 0, 0, "x\x1buU", "ello");

    test_helper("test_multiline_insert_undo", "line1\nline3", 0, 5, "i\nline2\x1bu", "line1\nline3");
    test_helper("test_multiline_insert_undo_redo", "line1\nline3", 0, 5, "i\nline2\x1buU", "line1\nline2\nline3");
}
