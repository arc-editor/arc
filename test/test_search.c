#include "test.h"
#include "../src/editor.h"
#include "../src/buffer.h"
#include "../src/search.h"
#include "../src/normal.h"
#include <stdio.h>
#include <unistd.h>

void test_generic_motion_helper(const char* test_name, const char* initial_content, int start_y, int start_x, const char* commands, int end_y, int end_x) {
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
        char ch_str[2] = {commands[i], '\0'};
        editor_handle_input(ch_str);
    }

    ASSERT_EQUAL(test_name, buffer->position_y, end_y);
    ASSERT_EQUAL(test_name, buffer->position_x, end_x);

    remove(filename);
}

void run_search_tests() {
    test_generic_motion_helper("test_forward_search", "hello world\nhello there", 0, 0, "/hello\x0d", 1, 0);
    test_generic_motion_helper("test_forward_search_from_middle", "hello world\nhello there", 0, 5, "/hello\x0d", 1, 0);
    test_generic_motion_helper("test_backward_search", "hello world\nhello there", 1, 0, "?hello\x0d", 0, 0);
    test_generic_motion_helper("test_backward_search_from_middle", "hello world\nhello there", 1, 5, "?hello\x0d", 1, 0);
    test_generic_motion_helper("test_no_match_forward", "hello world", 0, 0, "/goodbye\x0d", 0, 0);
    test_generic_motion_helper("test_no_match_backward", "hello world", 0, 0, "?goodbye\x0d", 0, 0);
    test_generic_motion_helper("test_repeat_search", "hello hello hello", 0, 0, "/hello\x0d\x0d", 0, 12);
}
