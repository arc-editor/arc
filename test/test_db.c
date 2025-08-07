#include "test.h"
#include "../src/editor.h"
#include "../src/buffer.h"
#include "../src/normal.h"
#include <stdio.h>
#include <unistd.h>

void test_start_of_line_db(void) {
    printf("  - test_start_of_line_db\\n");

    const char* filename = "test_db_fixture.txt";
    FILE *fp = fopen(filename, "w");
    fprintf(fp, "hello world\njon doe");
    fclose(fp);

    editor_init((char*)filename);

    Buffer *buffer = editor_get_active_buffer();
    buffer->position_x = 0;
    buffer->position_y = 1;

    // Simulate 'd' then 'b'
    normal_handle_input('d');
    normal_handle_input('b');

    char *result = buffer_get_content(buffer);
    ASSERT_STRING_EQUAL(result, "hello jon doe");

    free(result);
    remove(filename);
}
