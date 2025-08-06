#ifndef LSP_H
#define LSP_H

typedef enum {
    LSP_DIAGNOSTIC_SEVERITY_ERROR = 1,
    LSP_DIAGNOSTIC_SEVERITY_WARNING = 2,
    LSP_DIAGNOSTIC_SEVERITY_INFO = 3,
    LSP_DIAGNOSTIC_SEVERITY_HINT = 4
} DiagnosticSeverity;

typedef struct {
    int line;
    int col_start;
    int col_end;
    DiagnosticSeverity severity;
    char *message;
} Diagnostic;

void lsp_init(void);
void lsp_shutdown(void);
void lsp_get_diagnostics(const char *file_path, Diagnostic **diagnostics, int *diagnostic_count);
void lsp_did_open(const char *file_path, const char *language_id, const char *text);
void lsp_did_change(const char *file_path, const char *text, int version);

#endif // LSP_H
