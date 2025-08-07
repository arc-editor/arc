#include "test.h"
#include "../src/editor.h"
#include "../src/buffer.h"
#include "../src/normal.h"
#include <stdio.h>
#include <unistd.h>

void test_start_of_line_to_start_of_line_db(void) {
    printf("  - test_start_of_line_to_start_of_line_db\n");

    const char* filename = "test.txt";
    FILE *fp = fopen(filename, "w");
    fprintf(fp, "foo\nbar\nbaz");
    fclose(fp);

    editor_open((char*)filename);

    Buffer *buffer = editor_get_active_buffer();
    buffer->position_x = 0;
    buffer->position_y = 2;

    normal_handle_input('d');
    normal_handle_input('b');

    char *result = buffer_get_content(buffer);
    ASSERT_STRING_EQUAL(result, "foo\nbaz");

    free(result);
    remove(filename);
}
