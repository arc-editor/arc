#include <stdio.h>

// Test function declarations
void test_dw(void);
void test_start_of_line_db(void);

int main(void) {
    printf("Running tests...\\n");

    test_dw();
    test_start_of_line_db();

    printf("All tests passed.\\n");
    return 0;
}
