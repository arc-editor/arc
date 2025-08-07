#include "test.h"
#include "../src/editor.h"
#include "../src/buffer.h"
#include "../src/normal.h"
#include <stdio.h>
#include <unistd.h>

void test_mid_word_db(void) {
    printf("  - test_mid_word_db\n");

    const char* filename = "test.txt";
    FILE *fp = fopen(filename, "w");
    fprintf(fp, "hello world");
    fclose(fp);

    editor_open((char*)filename);

    Buffer *buffer = editor_get_active_buffer();
    buffer->position_x = 8;
    buffer->position_y = 0;

    normal_handle_input('d');
    normal_handle_input('b');

    char *result = buffer_get_content(buffer);
    ASSERT_STRING_EQUAL(result, "hello rld");

    free(result);
    remove(filename);
}
