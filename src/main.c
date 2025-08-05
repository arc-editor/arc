#include "editor.h"
#include "lsp.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    lsp_init();
    char *filename = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("arc %s\n", EDITOR_VERSION);
            return 0;
        } else if (filename == NULL) {
            filename = argv[i];
        } else {
            fprintf(stderr, "Usage: arc [--version] [filename]\n");
            return 1;
        }
    }
    editor_start(filename);
    lsp_shutdown();
    return 0;
}
