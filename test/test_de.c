#include "test.h"
#include "../src/editor.h"
#include "../src/buffer.h"
#include "../src/normal.h"
#include <stdio.h>
#include <unistd.h>

void test_de(void) {
    printf("  - test_de\n");

    const char* filename = "test_de_fixture.txt";
    FILE *fp = fopen(filename, "w");
    fprintf(fp, "hello world");
    fclose(fp);

    editor_init((char*)filename);

    Buffer *buffer = editor_get_active_buffer();
    buffer->position_x = 3;
    buffer->position_y = 0;

    normal_handle_input('d');
    normal_handle_input('e');

    char *result = buffer_get_content(buffer);
    ASSERT_STRING_EQUAL(result, "hel world");

    free(result);
    remove(filename);
}
