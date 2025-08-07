#include "test.h"
#include "../src/editor.h"
#include "../src/buffer.h"
#include "../src/normal.h"
#include <stdio.h>
#include <unistd.h>

// This test requires the binary to be compiled with -DTEST_BUILD
// to stub out terminal UI functions.

void test_dw(void) {
    printf("  - test_dw\\n");

    const char* filename = "test_dw_fixture.txt";
    FILE *fp = fopen(filename, "w");
    fprintf(fp, "hello world");
    fclose(fp);

    editor_init((char*)filename);

    Buffer *buffer = editor_get_active_buffer();
    buffer->position_x = 0;
    buffer->position_y = 0;

    // Simulate 'd' then 'w'
    normal_handle_input('d');
    normal_handle_input('w');

    char *result = buffer_get_content(buffer);
    ASSERT_STRING_EQUAL(result, " world");

    free(result);
    remove(filename);
}
