#include "test.h"
#include "../src/editor.h"
#include "../src/buffer.h"
#include "../src/normal.h"
#include <stdio.h>
#include <unistd.h>

void test_helper(const char* test_name, const char* initial_content, int start_y, int start_x, const char* commands, const char* expected_content) {
    printf("  - %s\n", test_name);

    const char* filename = "test.txt";
    FILE *fp = fopen(filename, "w");
    fprintf(fp, "%s", initial_content);
    fclose(fp);

    editor_open((char*)filename);

    Buffer *buffer = editor_get_active_buffer();
    buffer->position_y = start_y;
    buffer->position_x = start_x;

    for (int i = 0; commands[i] != '\0'; i++) {
        normal_handle_input(commands[i]);
    }

    char *result = buffer_get_content(buffer);
    ASSERT_STRING_EQUAL(result, expected_content);

    free(result);
    remove(filename);
}

void test_motion_helper(const char* test_name, const char* initial_content, int start_y, int start_x, const char* commands, int end_y, int end_x) {
    printf("  - %s\n", test_name);

    const char* filename = "test.txt";
    FILE *fp = fopen(filename, "w");
    fprintf(fp, "%s", initial_content);
    fclose(fp);

    editor_open((char*)filename);

    Buffer *buffer = editor_get_active_buffer();
    buffer->position_y = start_y;
    buffer->position_x = start_x;

    for (int i = 0; commands[i] != '\0'; i++) {
        normal_handle_input(commands[i]);
    }

    ASSERT_EQUAL(buffer->position_y, end_y);
    ASSERT_EQUAL(buffer->position_x, end_x);

    remove(filename);
}

int main(void) {
    printf("Running tests...\n");
    editor_init(NULL);

    // ================ deletions ================
    // WEB: words
    test_helper("test_dw", "hello world", 0, 0, "dw", " world");
    test_helper("test_de", "hello world", 0, 3, "de", "hel world");
    test_helper("test_mid_word_db", "hello world", 0, 8, "db", "hello rld");
    test_helper("test_start_of_line_to_mid_line_db", "hello world\njon doe\ngoodbye", 1, 0, "db", "hello jon doe\ngoodbye");
    test_helper("test_start_of_line_to_start_of_line_db", "foo\nbar\nbaz", 2, 0, "db", "foo\nbaz");
    test_helper("test_start_of_line_to_start_of_line_db", "foo\nbar\nbaz", 2, 0, "db", "foo\nbaz");

    // LH: lines
    test_helper("test_partial_line_dl", "hello world\n", 0, 3, "dl", "hel\n");
    test_helper("test_whole_line_dl", "hello world\n", 0, 0, "dl", "\n");
    test_helper("test_partial_line_dh", "hello world\n", 0, 3, "dh", "lo world\n");
    test_helper("test_whole_line_dh", "hello world\n", 0, 10, "dh", "\n");
    test_helper("test_whole_line_far_right_dh", "hello world\n", 0, 11, "dh", "\n");

    // P: paragraphs
    test_helper("test_mid_file_single_line_dp", "foo\n\nbar\n\nbat", 2, 0, "dp", "foo\n\n\nbat");
    test_helper("test_mid_file_multi_line_from_top_dp", "foo\n\nbar\nbaz\n\nbat", 2, 0, "dp", "foo\n\n\nbat");
    test_helper("test_mid_file_multi_line_from_middle_dp", "foo\n\nbar\nbaz\nbat\n\nbam", 3, 0, "dp", "foo\n\n\nbam");
    test_helper("test_mid_file_multi_line_from_bottom_dp", "foo\n\nbar\nbaz\n\nbat", 3, 0, "dp", "foo\n\n\nbat");

    // ================ target only commands ================
    test_motion_helper("test_w_motion", "hello world", 0, 0, "w", 0, 6);

    printf("All tests passed.\n");
    return 0;
}
