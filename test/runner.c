#include <stdio.h>

void test_dw(void);
void test_de(void);
void test_start_of_line_db(void);
void test_mid_word_db(void);

int main(void) {
    printf("Running tests...\n");

    test_dw();
    test_de();
    test_start_of_line_db();
    test_mid_word_db();

    printf("All tests passed.\n");
    return 0;
}
