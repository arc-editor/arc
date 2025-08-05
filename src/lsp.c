#define _POSIX_C_SOURCE 200809L

#include "lsp.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

void lsp_init(void) {
    log_info("LSP initialized");
}

void lsp_shutdown(void) {
    log_info("LSP shutdown");
}

void lsp_get_diagnostics(const char *file_path, Diagnostic **diagnostics, int *diagnostic_count) {
    // Dummy implementation
    *diagnostic_count = 1;
    *diagnostics = malloc(sizeof(Diagnostic));
    (*diagnostics)[0].line = 0;
    (*diagnostics)[0].col_start = 5;
    (*diagnostics)[0].col_end = 10;
    (*diagnostics)[0].message = strdup("This is a dummy diagnostic.");
}
