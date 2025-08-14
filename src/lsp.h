#ifndef LSP_H
#define LSP_H

#include "config.h"
#include <stdbool.h>
#include <pthread.h>

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
  char *uri;
} Diagnostic;

typedef struct {
  char lang_name[64];
  pid_t pid;
  int to_server_pipe[2];
  int from_server_pipe[2];
  pthread_t reader_thread;
  Diagnostic *diagnostics;
  int diagnostic_count;
  int diagnostics_version;
  pthread_mutex_t diagnostics_mutex;
  pthread_mutex_t init_mutex;
  pthread_cond_t init_cond;
  char read_buffer[16384];
  int buffer_pos;
  int next_id;
  bool initialized;
} LspServer;

void lsp_init(const Config *config, const char *file_name);
void lsp_shutdown_all(void);
int lsp_get_diagnostics(const char *file_path, Diagnostic **diagnostics,
                        int *diagnostic_count);
void lsp_did_open(const char *file_path, const char *language_id,
                  const char *text);
void lsp_did_change(const char *file_path, const char *text, int version);
bool lsp_is_running(const char *language_id);
char *find_project_root(const char *file_path);

#endif // LSP_H

