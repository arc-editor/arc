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
void lsp_did_open(const char *file_path, const char *language_id, const char *text);
void lsp_did_change(const char *file_path, const char *text, int version);

#endif // LSP_H
