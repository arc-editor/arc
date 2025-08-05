#ifndef LSP_H
#define LSP_H

typedef struct {
    int line;
    int col_start;
    int col_end;
    char *message;
} Diagnostic;

void lsp_init(void);
void lsp_shutdown(void);
void lsp_get_diagnostics(const char *file_path, Diagnostic **diagnostics, int *diagnostic_count);

#endif // LSP_H
