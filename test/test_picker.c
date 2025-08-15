#include "test.h"
#include "../src/picker.h"
#include "../src/normal.h"
#include "../src/buffer.h"
#include "../src/editor.h"
#include <stdbool.h>

static void test_open_file_duplicate_buffer() {
    const char *test_name = "test_open_file_duplicate_buffer";
    printf("  - %s\n", test_name);

    editor_init(NULL, false);
    editor_set_screen_size(20, 80);
    editor_handle_input = normal_handle_input;

    // Open file picker
    normal_handle_input(" ", 1);
    normal_handle_input("f", 1);
    ASSERT(test_name, editor_handle_input == (void*)picker_handle_input);

    // Type file name and select.
    picker_handle_input("R", 1);
    picker_handle_input("E", 1);
    picker_handle_input("A", 1);
    picker_handle_input("D", 1);
    picker_handle_input("M", 1);
    picker_handle_input("E", 1);
    picker_handle_input("\n", 1);

    // Check buffer
    Buffer* buf1 = editor_get_active_buffer();
    ASSERT_STRING_EQUAL(test_name, buf1->file_name, "README.md");
    int buffer_count;
    editor_get_buffers(&buffer_count);
    ASSERT_EQUAL(test_name, buffer_count, 1);

    // Open another file
    normal_handle_input(" ", 1);
    normal_handle_input("f", 1);
    picker_handle_input("M", 1);
    picker_handle_input("a", 1);
    picker_handle_input("k", 1);
    picker_handle_input("e", 1);
    picker_handle_input("\n", 1);

    Buffer* buf2 = editor_get_active_buffer();
    ASSERT_STRING_EQUAL(test_name, buf2->file_name, "Makefile");
    editor_get_buffers(&buffer_count);
    ASSERT_EQUAL(test_name, buffer_count, 2);

    // Open first file again
    normal_handle_input(" ", 1);
    normal_handle_input("f", 1);
    picker_handle_input("R", 1);
    picker_handle_input("E", 1);
    picker_handle_input("A", 1);
    picker_handle_input("D", 1);
    picker_handle_input("M", 1);
    picker_handle_input("E", 1);
    picker_handle_input("\n", 1);

    Buffer* buf3 = editor_get_active_buffer();
    ASSERT_STRING_EQUAL(test_name, buf3->file_name, "README.md");
    editor_get_buffers(&buffer_count);
    ASSERT_EQUAL(test_name, buffer_count, 2); // This is the crucial check
}

void test_picker_suite() {
    printf("--- Picker tests ---\n");
    test_open_file_duplicate_buffer();
}
