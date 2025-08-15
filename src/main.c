#include "editor.h"
#include "lsp.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "log.h"

int main(int argc, char *argv[]) {
    log_info("Starting arc editor");
    char *filename = NULL;
    bool benchmark_mode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("arc %s\n", EDITOR_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--benchmark") == 0) {
            benchmark_mode = true;
        } else if (filename == NULL) {
            filename = argv[i];
        } else {
            fprintf(stderr, "Usage: arc [--version] [--benchmark] [filename]\n");
            return 1;
        }
    }
    editor_start(filename, benchmark_mode);
    lsp_shutdown_all();
    return 0;
}
