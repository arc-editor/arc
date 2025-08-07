#include "test.h"
#include "../src/editor.h"
#include "../src/buffer.h"
#include "../src/normal.h"
#include <stdio.h>
#include <unistd.h>

void test_mid_file_multi_line_from_top_dp(void) {
    printf("  - test_mid_file_multi_line_from_top_dp\n");

    const char* filename = "test.txt";
    FILE *fp = fopen(filename, "w");
    fprintf(fp, "foo\n\nbar\nbaz\n\nbat");
    fclose(fp);

    editor_open((char*)filename);

    Buffer *buffer = editor_get_active_buffer();
    buffer->position_x = 0;
    buffer->position_y = 2;

    normal_handle_input('d');
    normal_handle_input('p');

    char *result = buffer_get_content(buffer);
    ASSERT_STRING_EQUAL(result, "foo\n\n\nbat");

    free(result);
    remove(filename);
}
