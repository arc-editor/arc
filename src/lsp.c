#define _POSIX_C_SOURCE 200809L

#include "lsp.h"
#include "log.h"
#include "cJSON.h"
#include "editor.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <fcntl.h>

static pid_t lsp_server_pid = -1;
static int to_server_pipe[2];
static int from_server_pipe[2];
static pthread_t lsp_reader_thread;
static Diagnostic *diagnostics = NULL;
static int diagnostic_count = 0;
static int diagnostics_version = 0;
static pthread_mutex_t diagnostics_mutex = PTHREAD_MUTEX_INITIALIZER;
static int lsp_next_id = 1;

// Buffer for reading partial messages
static char read_buffer[16384];
static int buffer_pos = 0;

cJSON *lsp_read_message();

void *lsp_reader_thread_func(void *arg __attribute__((unused))) {
  while (1) {
    cJSON *message = lsp_read_message();
    if (!message) {
      break; // Server closed connection
    }

    cJSON *method = cJSON_GetObjectItem(message, "method");
    if (method && strcmp(method->valuestring, "textDocument/publishDiagnostics") == 0) {
      cJSON *params = cJSON_GetObjectItem(message, "params");
      if (!params) {
        cJSON_Delete(message);
        continue;
      }
      
      cJSON *diagnostics_json = cJSON_GetObjectItem(params, "diagnostics");
      if (!diagnostics_json) {
        cJSON_Delete(message);
        continue;
      }

      pthread_mutex_lock(&diagnostics_mutex);
      diagnostics_version++;
      
      // Clear old diagnostics
      for (int i = 0; i < diagnostic_count; i++) {
        free(diagnostics[i].message);
      }
      free(diagnostics);
      diagnostics = NULL;
      diagnostic_count = 0;

      int new_diagnostic_count = cJSON_GetArraySize(diagnostics_json);

      if (new_diagnostic_count > 0) {
        diagnostics = malloc(sizeof(Diagnostic) * new_diagnostic_count);
        if (!diagnostics) {
          pthread_mutex_unlock(&diagnostics_mutex);
          cJSON_Delete(message);
          continue;
        }

        for (int i = 0; i < new_diagnostic_count; i++) {
          cJSON *diag_json = cJSON_GetArrayItem(diagnostics_json, i);
          if (!diag_json) continue;

          cJSON *range = cJSON_GetObjectItem(diag_json, "range");
          if (!range) continue;

          cJSON *start = cJSON_GetObjectItem(range, "start");
          cJSON *end = cJSON_GetObjectItem(range, "end");
          if (!start || !end) continue;

          cJSON *message_json = cJSON_GetObjectItem(diag_json, "message");
          if (!message_json) continue;

          cJSON *severity_json = cJSON_GetObjectItem(diag_json, "severity");

          cJSON *line_obj = cJSON_GetObjectItem(start, "line");
          cJSON *char_start_obj = cJSON_GetObjectItem(start, "character");
          cJSON *char_end_obj = cJSON_GetObjectItem(end, "character");
          
          if (!line_obj || !char_start_obj || !char_end_obj) continue;

          diagnostics[i].line = line_obj->valueint;
          diagnostics[i].col_start = char_start_obj->valueint;
          diagnostics[i].col_end = char_end_obj->valueint;

          if (severity_json) {
            diagnostics[i].severity = (DiagnosticSeverity)severity_json->valueint;
          } else {
            diagnostics[i].severity = LSP_DIAGNOSTIC_SEVERITY_HINT;
          }

          diagnostics[i].message = strdup(message_json->valuestring);
        }

        diagnostic_count = new_diagnostic_count;
      }

      pthread_mutex_unlock(&diagnostics_mutex);
      editor_request_redraw();
    }

    cJSON_Delete(message);
  }

  return NULL;
}

void lsp_send_message(cJSON *json_rpc) {
  if (lsp_server_pid <= 0) return;

  char *message = cJSON_PrintUnformatted(json_rpc);
  if (!message) return;

  char header[64];
  int len = strlen(message);
  snprintf(header, sizeof(header), "Content-Length: %d\r\n\r\n", len);

  // Write header and message atomically if possible
  ssize_t header_written = write(to_server_pipe[1], header, strlen(header));
  ssize_t message_written = write(to_server_pipe[1], message, len);
  
  if (header_written < 0 || message_written < 0) {
    log_error("lsp_send_message: write failed");
  }

  free(message);
}

cJSON *lsp_read_message() {
    while (1) {
        // Look for complete header in buffer
        char *header_end = strstr(read_buffer, "\r\n\r\n");
        if (!header_end) {
            // Need more data for header
            ssize_t bytes_read = read(from_server_pipe[0], read_buffer + buffer_pos, 
                                    sizeof(read_buffer) - buffer_pos - 1);
            if (bytes_read <= 0) {
                return NULL; // EOF or error
            }
            buffer_pos += bytes_read;
            read_buffer[buffer_pos] = '\0';
            continue;
        }

        // Parse Content-Length from header
        char *content_length_start = strstr(read_buffer, "Content-Length: ");
        if (!content_length_start || content_length_start > header_end) {
            log_error("lsp_read_message: invalid header format");
            return NULL;
        }

        int content_length = atoi(content_length_start + 16);
        if (content_length <= 0 || content_length > 1048576) { // 1MB limit
            log_error("lsp_read_message: invalid content length %d", content_length);
            return NULL;
        }

        char *json_start = header_end + 4;
        int header_size = json_start - read_buffer;
        int available_json = buffer_pos - header_size;

        // Check if we have the complete JSON message
        if (available_json >= content_length) {
            // We have a complete message
            char *json_copy = malloc(content_length + 1);
            if (!json_copy) {
                log_error("lsp_read_message: malloc failed");
                return NULL;
            }
            
            memcpy(json_copy, json_start, content_length);
            json_copy[content_length] = '\0';

            cJSON *parsed = cJSON_Parse(json_copy);
            free(json_copy);

            // Move remaining data to beginning of buffer
            int remaining = buffer_pos - (header_size + content_length);
            if (remaining > 0) {
                memmove(read_buffer, json_start + content_length, remaining);
            }
            buffer_pos = remaining;
            read_buffer[buffer_pos] = '\0';

            return parsed;
        } else {
            // Need more data for JSON content
            int needed = content_length - available_json;
            if (buffer_pos + needed >= (int)sizeof(read_buffer)) {
                log_error("lsp_read_message: message too large for buffer");
                return NULL;
            }

            ssize_t bytes_read = read(from_server_pipe[0], read_buffer + buffer_pos, needed);
            if (bytes_read <= 0) {
                return NULL; // EOF or error
            }
            buffer_pos += bytes_read;
            read_buffer[buffer_pos] = '\0';
        }
    }
}

static void get_language_id_and_server(const char *file_name, const char **language_id, const char **lsp_server_cmd) {
    if (!file_name) {
        *language_id = NULL;
        *lsp_server_cmd = NULL;
        return;
    }

    const char *ext = strrchr(file_name, '.');
    if (!ext) {
        *language_id = NULL;
        *lsp_server_cmd = NULL;
        return;
    }

    if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0) {
        *language_id = "c";
        *lsp_server_cmd = "clangd";
    } else if (strcmp(ext, ".js") == 0) {
        *language_id = "javascript";
        *lsp_server_cmd = "typescript-language-server";
    } else if (strcmp(ext, ".ts") == 0) {
        *language_id = "typescript";
        *lsp_server_cmd = "typescript-language-server";
    } else if (strcmp(ext, ".go") == 0) {
        *language_id = "go";
        *lsp_server_cmd = "gopls";
    } else {
        *language_id = NULL;
        *lsp_server_cmd = NULL;
    }
}

void lsp_init(const char *file_name) {
  const char *language_id;
  const char *lsp_server_cmd;
  get_language_id_and_server(file_name, &language_id, &lsp_server_cmd);

  if (!lsp_server_cmd) {
    log_info("lsp.lsp_init: no LSP server for file %s", file_name);
    return;
  }
  if (pipe(to_server_pipe) == -1 || pipe(from_server_pipe) == -1) {
    log_error("lsp.lsp_init: pipe failed");
    return;
  }

  lsp_server_pid = fork();
  if (lsp_server_pid == -1) {
    log_error("lsp.lsp_init: fork failed");
    return;
  }

  if (lsp_server_pid == 0) { // Child process
    close(to_server_pipe[1]);
    dup2(to_server_pipe[0], STDIN_FILENO);
    close(to_server_pipe[0]);

    close(from_server_pipe[0]);
    dup2(from_server_pipe[1], STDOUT_FILENO);
    close(from_server_pipe[1]);

    int dev_null = open("/dev/null", O_WRONLY);
    if (dev_null != -1) {
      dup2(dev_null, STDERR_FILENO);
      close(dev_null);
    }

    execlp(lsp_server_cmd, lsp_server_cmd, NULL);
    log_error("lsp.lsp_init: execlp failed for %s", lsp_server_cmd);
    exit(1);
  } else { // Parent process
    close(to_server_pipe[0]);
    close(from_server_pipe[1]);
    log_info("lsp.lsp_init: LSP server started with PID %d", lsp_server_pid);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(root, "id", lsp_next_id++);
    cJSON_AddStringToObject(root, "method", "initialize");

    cJSON *params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "processId", getpid());
    cJSON *client_info = cJSON_CreateObject();
    cJSON_AddStringToObject(client_info, "name", "arc");
    cJSON_AddStringToObject(client_info, "version", "0.1");
    cJSON_AddItemToObject(params, "clientInfo", client_info);
   
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
      char root_uri[1024 + 7]; // Extra space for "file://"
      snprintf(root_uri, sizeof(root_uri), "file://%s", cwd);
      cJSON_AddStringToObject(params, "rootUri", root_uri);
    } else {
      cJSON_AddStringToObject(params, "rootUri", "file:///");
    }

    cJSON *capabilities = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "capabilities", capabilities);

    cJSON_AddItemToObject(root, "params", params);

    lsp_send_message(root);
    cJSON_Delete(root);

    if (pthread_create(&lsp_reader_thread, NULL, lsp_reader_thread_func, NULL) != 0) {
      log_error("lsp.lsp_init: failed to create reader thread");
    }
  }
}

void lsp_did_open(const char *file_path, const char *language_id, const char *text) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "jsonrpc", "2.0");
  cJSON_AddStringToObject(root, "method", "textDocument/didOpen");

  cJSON *params = cJSON_CreateObject();
  cJSON *text_document = cJSON_CreateObject();
  
  // Ensure file_path is a proper URI
  char uri[2048];
  if (strncmp(file_path, "file://", 7) == 0) {
    strncpy(uri, file_path, sizeof(uri) - 1);
  } else {
    snprintf(uri, sizeof(uri), "file://%s", file_path);
  }
  uri[sizeof(uri) - 1] = '\0';
  
  cJSON_AddStringToObject(text_document, "uri", uri);
  cJSON_AddStringToObject(text_document, "languageId", language_id);
  cJSON_AddNumberToObject(text_document, "version", 1);
  cJSON_AddStringToObject(text_document, "text", text);
  cJSON_AddItemToObject(params, "textDocument", text_document);
  cJSON_AddItemToObject(root, "params", params);

  lsp_send_message(root);
  cJSON_Delete(root);
}

void lsp_did_change(const char *file_path, const char *text, int version) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "jsonrpc", "2.0");
  cJSON_AddStringToObject(root, "method", "textDocument/didChange");

  cJSON *params = cJSON_CreateObject();
  cJSON *text_document = cJSON_CreateObject();
  
  // Ensure file_path is a proper URI
  char uri[2048];
  if (strncmp(file_path, "file://", 7) == 0) {
    strncpy(uri, file_path, sizeof(uri) - 1);
  } else {
    snprintf(uri, sizeof(uri), "file://%s", file_path);
  }
  uri[sizeof(uri) - 1] = '\0';
  
  cJSON_AddStringToObject(text_document, "uri", uri);
  cJSON_AddNumberToObject(text_document, "version", version);
  cJSON_AddItemToObject(params, "textDocument", text_document);

  cJSON *content_changes = cJSON_CreateArray();
  cJSON *change = cJSON_CreateObject();
  cJSON_AddStringToObject(change, "text", text);
  cJSON_AddItemToArray(content_changes, change);
  cJSON_AddItemToObject(params, "contentChanges", content_changes);

  cJSON_AddItemToObject(root, "params", params);

  lsp_send_message(root);
  cJSON_Delete(root);
}

void lsp_shutdown(void) {
  if (lsp_server_pid > 0) {
    // Sending shutdown request
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(root, "id", lsp_next_id++);
    cJSON_AddStringToObject(root, "method", "shutdown");
    lsp_send_message(root);
    cJSON_Delete(root);

    // Small delay to let shutdown process
    // usleep(100000); // 100ms

    // Sending exit notification
    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "method", "exit");
    lsp_send_message(root);
    cJSON_Delete(root);

    // Wait for the reader thread to finish (with timeout)
    pthread_join(lsp_reader_thread, NULL);

    kill(lsp_server_pid, SIGTERM);
    waitpid(lsp_server_pid, NULL, 0);
    close(to_server_pipe[1]);
    close(from_server_pipe[0]);
    log_info("lsp.lsp_shutdown: LSP server shut down");
    
    lsp_server_pid = -1;
    buffer_pos = 0; // Reset buffer
  }
}

int lsp_get_diagnostics(const char *file_name __attribute__((unused)), Diagnostic **out_diagnostics, int *out_diagnostic_count) {
    pthread_mutex_lock(&diagnostics_mutex);
    
    *out_diagnostic_count = diagnostic_count;
    if (diagnostic_count > 0) {
        *out_diagnostics = malloc(sizeof(Diagnostic) * diagnostic_count);
        if (*out_diagnostics) {
            for (int i = 0; i < diagnostic_count; i++) {
                (*out_diagnostics)[i] = diagnostics[i]; // Copy struct members
                (*out_diagnostics)[i].message = strdup(diagnostics[i].message); // Deep copy the string
            }
        } else {
            // Malloc failed, return an empty set
            *out_diagnostic_count = 0;
            *out_diagnostics = NULL;
        }
    } else {
        *out_diagnostics = NULL;
    }

    int version = diagnostics_version;
    pthread_mutex_unlock(&diagnostics_mutex);
    return version;
}
