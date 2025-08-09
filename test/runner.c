#include "test.h"
#include "../src/editor.h"
#include "../src/buffer.h"
#include "../src/normal.h"
#include "../src/visual.h"
#include "test_search.h"
#include "test_normal.h"
#include "test_undo.h"
#include <stdio.h>
#include <unistd.h>

int g_test_failures = 0;

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
        char ch_str[2] = {commands[i], '\0'};
        normal_handle_input(ch_str);
    }

    char *result = buffer_get_content(buffer);
    ASSERT_STRING_EQUAL(test_name, result, expected_content);

    free(result);
    remove(filename);
}

void test_visual_motion_helper(const char* test_name, const char* initial_content, int start_y, int start_x, const char* commands, int sel_start_y, int sel_start_x, int end_y, int end_x) {
    printf("  - %s\n", test_name);

    const char* filename = "test.txt";
    FILE *fp = fopen(filename, "w");
    fprintf(fp, "%s", initial_content);
    fclose(fp);

    editor_open((char*)filename);

    Buffer *buffer = editor_get_active_buffer();
    buffer->position_y = start_y;
    buffer->position_x = start_x;

    normal_handle_input("v");
    for (int i = 0; commands[i] != '\0'; i++) {
        char ch_str[2] = {commands[i], '\0'};
        visual_handle_input(ch_str);
    }

    ASSERT_EQUAL(test_name, buffer->selection_start_y, sel_start_y);
    ASSERT_EQUAL(test_name, buffer->selection_start_x, sel_start_x);
    ASSERT_EQUAL(test_name, buffer->position_y, end_y);
    ASSERT_EQUAL(test_name, buffer->position_x, end_x);

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
        char ch_str[2] = {commands[i], '\0'};
        normal_handle_input(ch_str);
    }

    ASSERT_EQUAL(test_name, buffer->position_y, end_y);
    ASSERT_EQUAL(test_name, buffer->position_x, end_x);

    remove(filename);
}

void test_visual_action_helper(const char* test_name, const char* initial_content, int start_y, int start_x, const char* commands, const char* expected_content) {
    printf("  - %s\n", test_name);

    const char* filename = "test.txt";
    FILE *fp = fopen(filename, "w");
    fprintf(fp, "%s", initial_content);
    fclose(fp);

    editor_open((char*)filename);

    Buffer *buffer = editor_get_active_buffer();
    buffer->position_y = start_y;
    buffer->position_x = start_x;

    normal_handle_input("v");
    for (int i = 0; commands[i] != '\0'; i++) {
        char ch_str[2] = {commands[i], '\0'};
        visual_handle_input(ch_str);
    }

    char *result = buffer_get_content(buffer);
    ASSERT_STRING_EQUAL(test_name, result, expected_content);

    free(result);
    remove(filename);
}

int main(void) {
    printf("Running tests...\n");
    editor_init(NULL);
    editor_set_screen_size(24, 80);

    // ================ deletions ================
    // web: words
    test_helper("test_dw", "hello world", 0, 0, "dw", " world");
    test_helper("test_de", "hello world", 0, 3, "de", "hel world");
    test_helper("test_mid_word_db", "hello world", 0, 8, "db", "hello rld");
    test_helper("test_start_of_line_to_mid_line_db", "hello world\njon doe\ngoodbye", 1, 0, "db", "hello jon doe\ngoodbye");
    test_helper("test_start_of_line_to_start_of_line_db", "foo\nbar\nbaz", 2, 0, "db", "foo\nbaz");
    test_helper("test_start_of_line_to_start_of_line_db", "foo\nbar\nbaz", 2, 0, "db", "foo\nbaz");

    // WEB: words
    test_helper("test_dW", "hello world", 0, 0, "dW", " world");
    test_helper("test_dE", "hello world", 0, 3, "dE", "hel world");
    test_helper("test_mid_word_dB", "hello world", 0, 8, "dB", "hello rld");
    test_helper("test_start_of_line_to_mid_line_dB", "hello world\njon doe\ngoodbye", 1, 0, "dB", "hello jon doe\ngoodbye");
    test_helper("test_start_of_line_to_start_of_line_dB", "foo\nbar\nbaz", 2, 0, "dB", "foo\nbaz");
    test_helper("test_start_of_line_to_start_of_line_dB", "foo\nbar\nbaz", 2, 0, "dB", "foo\nbaz");

    // lh: lines
    test_helper("test_partial_line_dl", "hello world\n", 0, 3, "dl", "hel\n");
    test_helper("test_whole_line_dl", "hello world\n", 0, 0, "dl", "\n");
    test_helper("test_partial_line_dh", "hello world\n", 0, 3, "dh", "lo world\n");
    test_helper("test_whole_line_dh", "hello world\n", 0, 10, "dh", "d\n");
    test_helper("test_whole_line_far_right_dh", "hello world\n", 0, 11, "dh", "\n");

    // p: paragraphs
    test_helper("test_mid_file_single_line_dp_noop", "foo\n\nbar\n\nbat", 2, 0, "dp", "foo\n\nbar\n\nbat");
    test_helper("test_mid_file_single_line_dip", "foo\n\nbar\n\nbat", 2, 0, "dip", "foo\n\n\nbat");
    test_helper("test_mid_file_multi_line_from_toip_dp", "foo\n\nbar\nbaz\n\nbat", 2, 0, "dip", "foo\n\n\nbat");
    test_helper("test_mid_file_multi_line_from_middle_dip", "foo\n\nbar\nbaz\nbat\n\nbam", 3, 0, "dip", "foo\n\n\nbam");
    test_helper("test_mid_file_multi_line_from_bottom_dip", "foo\n\nbar\nbaz\n\nbat", 3, 0, "dip", "foo\n\n\nbat");
    test_helper("test_mid_file_single_line_dap", "foo\n\nbar\n\nbat", 2, 0, "dap", "foo\n\nbat");
    test_helper("test_mid_file_multi_line_from_toip_dp", "foo\n\nbar\nbaz\n\nbat", 2, 0, "dap", "foo\n\nbat");
    test_helper("test_mid_file_multi_line_from_middle_dap", "foo\n\nbar\nbaz\nbat\n\nbam", 3, 0, "dap", "foo\n\nbam");
    test_helper("test_mid_file_multi_line_from_bottom_dap", "foo\n\nbar\nbaz\n\nbat", 3, 0, "dap", "foo\n\nbat");

    run_normal_tests();
    run_undo_tests();

    // ================ target only commands ================
    test_motion_helper("test_w_motion", "hello world", 0, 0, "w", 0, 6);
    test_motion_helper("test_e_motion", "hello world", 0, 0, "e", 0, 4);
    test_motion_helper("test_b_motion", "hello world", 0, 8, "b", 0, 6);
    test_motion_helper("test_b_motion", "hello world", 0, 6, "b", 0, 0);

    // ================ find commands ================
    test_motion_helper("test_f_motion", "hello world", 0, 0, "fl", 0, 2);
    test_motion_helper("test_f_motion_no_match", "hello world", 0, 0, "fz", 0, 0);
    test_motion_helper("test_2f_motion", "hello world, hello", 0, 0, "2fl", 0, 3);
    test_motion_helper("test_F_motion", "hello world", 0, 5, "Fh", 0, 0);
    test_motion_helper("test_t_motion", "hello world", 0, 0, "tw", 0, 5);
    test_motion_helper("test_2t_motion", "hello world", 0, 1, "tl", 0, 2);
    test_motion_helper("test_2t_motion", "hello world", 0, 0, "2to", 0, 6);
    test_motion_helper("test_T_motion", "hello world", 0, 5, "Th", 0, 1);

    // ================ paragraph motion ================
    test_motion_helper("test_np_motion", "p1\n\np2", 0, 0, "np", 2, 0);
    test_motion_helper("test_np_motion_mid_para", "p1 l1\np1 l2\n\np2", 1, 0, "np", 3, 0);
    test_motion_helper("test_np_motion_end_of_file", "p1\n\np2", 2, 0, "np", 2, 0);
    test_motion_helper("test_2np_motion", "p1\n\np2\n\np3", 0, 0, "2np", 4, 0);
    test_motion_helper("test_pp_motion", "p1\n\np2", 2, 0, "pp", 0, 0);
    test_motion_helper("test_pp_motion_mid_para", "p1\n\np2 l1\np2 l2", 3, 0, "pp", 2, 0);
    test_motion_helper("test_pp_motion_start_of_para", "p1\n\np2", 2, 0, "pp", 0, 0);
    test_motion_helper("test_pp_motion_start_of_file", "p1\n\np2", 0, 0, "pp", 0, 0);
    test_motion_helper("test_2pp_motion", "p1\n\np2\n\np3", 4, 0, "2pp", 0, 0);

    // ================ find with delete ================
    test_helper("test_df_motion", "hello world", 0, 0, "dfl", "lo world");
    test_helper("test_dF_motion", "hello world", 0, 5, "dFh", "world");
    test_helper("test_dt_motion", "hello world", 0, 0, "dtw", "world");
    test_helper("test_dT_motion", "hello world", 0, 5, "dTh", "hworld");

    // ================ visual mode ================
    test_visual_motion_helper("test_visual_enter_and_move_right", "hello world", 0, 0, "l", 0, 0, 0, 1);
    test_visual_motion_helper("test_visual_enter_and_move_left", "hello world", 0, 5, "h", 0, 5, 0, 4);
    test_visual_motion_helper("test_visual_enter_and_move_down", "hello\nworld", 0, 0, "j", 0, 0, 1, 0);
    test_visual_motion_helper("test_visual_enter_and_move_up", "hello\nworld", 1, 0, "k", 1, 0, 0, 0);
    test_visual_motion_helper("test_visual_w", "hello world", 0, 0, "w", 0, 0, 0, 6);
    test_visual_motion_helper("test_visual_b", "hello world", 0, 6, "b", 0, 6, 0, 0);
    test_visual_motion_helper("test_visual_e", "hello world", 0, 0, "e", 0, 0, 0, 4);
    test_visual_motion_helper("test_visual_f", "hello world", 0, 0, "fl", 0, 0, 0, 2);
    test_visual_action_helper("test_visual_delete", "hello world", 0, 0, "lld", "lo world");
    test_visual_action_helper("test_visual_delete_backward", "hello world", 0, 3, "hhd", "ho world");
    test_visual_action_helper("test_visual_delete_backward_across_line", "foo\nbar", 1, 1, "kld", "for");
    test_visual_action_helper("test_visual_delete_forward_across_line", "foo\nbar", 0, 1, "jhd", "far");

    run_search_tests();

    if (g_test_failures == 0) {
        printf("All tests passed.\n");
        return 0;
    } else {
        printf("%d test(s) failed.\n", g_test_failures);
        return 1;
    }
}
