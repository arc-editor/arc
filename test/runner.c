#include <stdio.h>
#include "../src/editor.h"

void test_dw(void);
void test_de(void);
void test_start_of_line_to_mid_line_db(void);
void test_mid_word_db(void);
void test_start_of_line_to_start_of_line_db(void);
void test_mid_file_single_line_dp(void);
void test_mid_file_multi_line_from_top_dp(void);
void test_mid_file_multi_line_from_bottom_dp(void);

int main(void) {
    printf("Running tests...\n");

    editor_init(NULL);

    test_dw();
    test_de();
    test_mid_word_db();
    test_start_of_line_to_mid_line_db();
    test_start_of_line_to_start_of_line_db();
    test_start_of_line_to_start_of_line_db();
    test_mid_file_single_line_dp();
    test_mid_file_multi_line_from_top_dp();
    test_mid_file_multi_line_from_bottom_dp();

    printf("All tests passed.\n");
    return 0;
}
