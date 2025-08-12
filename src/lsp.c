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
#include <sys/stat.h>

#define MAX_LSP_SERVERS 10

char *find_project_root(const char *file_path) {
    char path[1024];
    strncpy(path, file_path, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';

    char *last_slash = strrchr(path, '/');
    if (last_slash) {
        *last_slash = '\0';
    }

    char check_path[1024];
    struct stat st;

    while (strlen(path) > 0) {
        snprintf(check_path, sizeof(check_path), "%s/.git", path);
        if (stat(check_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            return strdup(path);
        }

        snprintf(check_path, sizeof(check_path), "%s/go.mod", path);
        if (stat(check_path, &st) == 0 && S_ISREG(st.st_mode)) {
            return strdup(path);
        }

        snprintf(check_path, sizeof(check_path), "%s/package.json", path);
        if (stat(check_path, &st) == 0 && S_ISREG(st.st_mode)) {
            return strdup(path);
        }

        last_slash = strrchr(path, '/');
        if (last_slash) {
            *last_slash = '\0';
        } else {
            break;
        }
    }

    // Fallback to the file's directory
    strncpy(path, file_path, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    last_slash = strrchr(path, '/');
    if (last_slash) {
        *last_slash = '\0';
        return strdup(path);
    }

    return strdup(".");
}

static LspServer *lsp_servers[MAX_LSP_SERVERS] = {NULL};
static int lsp_server_count = 0;

static const char *get_lang_id_from_filename(const char *file_name) {
  if (!file_name) {
    return NULL;
  }
  const char *ext = strrchr(file_name, '.');
  if (!ext) {
    return NULL;
  }
  return ext + 1;
}

static LspServer *get_server(const char *language_id) {
  if (!language_id) {
    return NULL;
  }
  for (int i = 0; i < lsp_server_count; i++) {
    if (strcmp(lsp_servers[i]->lang_id, language_id) == 0) {
      return lsp_servers[i];
    }
  }
  return NULL;
}

static void lsp_send_message(LspServer *server, cJSON *json_rpc) {
  if (!server || server->pid <= 0)
    return;

  char *message = cJSON_PrintUnformatted(json_rpc);
  if (!message)
    return;

  char header[64];
  int len = strlen(message);
  snprintf(header, sizeof(header), "Content-Length: %d\r\n\r\n", len);

  ssize_t header_written =
      write(server->to_server_pipe[1], header, strlen(header));
  ssize_t message_written = write(server->to_server_pipe[1], message, len);

  if (header_written < 0 || message_written < 0) {
    log_error("lsp_send_message: write failed");
  }

  free(message);
}

static cJSON *lsp_read_message(LspServer *server) {
  while (1) {
    char *header_end = strstr(server->read_buffer, "\r\n\r\n");
    if (!header_end) {
      ssize_t bytes_read =
          read(server->from_server_pipe[0],
               server->read_buffer + server->buffer_pos,
               sizeof(server->read_buffer) - server->buffer_pos - 1);
      if (bytes_read <= 0) {
        return NULL;
      }
      server->buffer_pos += bytes_read;
      server->read_buffer[server->buffer_pos] = '\0';
      continue;
    }

    char *content_length_start =
        strstr(server->read_buffer, "Content-Length: ");
    if (!content_length_start || content_length_start > header_end) {
      log_error("lsp_read_message: invalid header format");
      return NULL;
    }

    int content_length = atoi(content_length_start + 16);
    if (content_length <= 0 || content_length > 1048576) {
      log_error("lsp_read_message: invalid content length %d", content_length);
      return NULL;
    }

    char *json_start = header_end + 4;
    int header_size = json_start - server->read_buffer;
    int available_json = server->buffer_pos - header_size;

    if (available_json >= content_length) {
      char *json_copy = malloc(content_length + 1);
      if (!json_copy) {
        log_error("lsp_read_message: malloc failed");
        return NULL;
      }

      memcpy(json_copy, json_start, content_length);
      json_copy[content_length] = '\0';

      cJSON *parsed = cJSON_Parse(json_copy);
      free(json_copy);

      int remaining = server->buffer_pos - (header_size + content_length);
      if (remaining > 0) {
        memmove(server->read_buffer, json_start + content_length, remaining);
      }
      server->buffer_pos = remaining;
      server->read_buffer[server->buffer_pos] = '\0';

      return parsed;
    } else {
      int needed = content_length - available_json;
      if (server->buffer_pos + needed >= (int)sizeof(server->read_buffer)) {
        log_error("lsp_read_message: message too large for buffer");
        return NULL;
      }

      ssize_t bytes_read = read(server->from_server_pipe[0],
                                server->read_buffer + server->buffer_pos, needed);
      if (bytes_read <= 0) {
        return NULL;
      }
      server->buffer_pos += bytes_read;
      server->read_buffer[server->buffer_pos] = '\0';
    }
  }
}

static void *lsp_reader_thread_func(void *arg) {
  LspServer *server = (LspServer *)arg;
  while (1) {
    cJSON *message = lsp_read_message(server);
    if (!message) {
      break;
    }

    cJSON *method = cJSON_GetObjectItem(message, "method");
    if (method &&
        strcmp(method->valuestring, "textDocument/publishDiagnostics") == 0) {
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

      pthread_mutex_lock(&server->diagnostics_mutex);
      server->diagnostics_version++;

      for (int i = 0; i < server->diagnostic_count; i++) {
        free(server->diagnostics[i].message);
      }
      free(server->diagnostics);
      server->diagnostics = NULL;
      server->diagnostic_count = 0;

      int new_diagnostic_count = cJSON_GetArraySize(diagnostics_json);

      if (new_diagnostic_count > 0) {
        server->diagnostics =
            malloc(sizeof(Diagnostic) * new_diagnostic_count);
        if (!server->diagnostics) {
          pthread_mutex_unlock(&server->diagnostics_mutex);
          cJSON_Delete(message);
          continue;
        }

        for (int i = 0; i < new_diagnostic_count; i++) {
          cJSON *diag_json = cJSON_GetArrayItem(diagnostics_json, i);
          if (!diag_json)
            continue;

          cJSON *range = cJSON_GetObjectItem(diag_json, "range");
          if (!range)
            continue;

          cJSON *start = cJSON_GetObjectItem(range, "start");
          cJSON *end = cJSON_GetObjectItem(range, "end");
          if (!start || !end)
            continue;

          cJSON *message_json = cJSON_GetObjectItem(diag_json, "message");
          if (!message_json)
            continue;

          cJSON *severity_json = cJSON_GetObjectItem(diag_json, "severity");
          cJSON *line_obj = cJSON_GetObjectItem(start, "line");
          cJSON *char_start_obj = cJSON_GetObjectItem(start, "character");
          cJSON *char_end_obj = cJSON_GetObjectItem(end, "character");

          if (!line_obj || !char_start_obj || !char_end_obj)
            continue;

          server->diagnostics[i].line = line_obj->valueint;
          server->diagnostics[i].col_start = char_start_obj->valueint;
          server->diagnostics[i].col_end = char_end_obj->valueint;

          if (severity_json) {
            server->diagnostics[i].severity =
                (DiagnosticSeverity)severity_json->valueint;
          } else {
            server->diagnostics[i].severity = LSP_DIAGNOSTIC_SEVERITY_HINT;
          }

          server->diagnostics[i].message = strdup(message_json->valuestring);
        }
        server->diagnostic_count = new_diagnostic_count;
      }

      pthread_mutex_unlock(&server->diagnostics_mutex);
      editor_request_redraw();
    }

    cJSON_Delete(message);
  }

  return NULL;
}

void lsp_init(const Config *config, const char *file_name) {
  const char *lang_id = get_lang_id_from_filename(file_name);
  if (!lang_id || get_server(lang_id) != NULL) {
    return;
  }

  if (lsp_server_count >= MAX_LSP_SERVERS) {
    log_error("lsp_init: max number of LSP servers reached");
    return;
  }

  char key[256];
  snprintf(key, sizeof(key), "lsp.%s", lang_id);

  const char *command = NULL;
  if (config->toml_result.ok) {
    toml_datum_t cmd_datum = toml_seek(config->toml_result.toptab, key);
    if (cmd_datum.type == TOML_STRING) {
      command = cmd_datum.u.s;
    }
  }

  if (!command) {
    if (strcmp(lang_id, "c") == 0 || strcmp(lang_id, "h") == 0 ||
        strcmp(lang_id, "cpp") == 0) {
      command = "clangd";
    } else if (strcmp(lang_id, "py") == 0) {
      command = "pylsp";
    } else if (strcmp(lang_id, "rs") == 0) {
      command = "rust-analyzer";
    } else if (strcmp(lang_id, "go") == 0) {
      command = "gopls";
    } else if (strcmp(lang_id, "ts") == 0 || strcmp(lang_id, "js") == 0) {
      command = "typescript-language-server --stdio";
    }
  }

  if (!command) {
    return;
  }

  LspServer *server = malloc(sizeof(LspServer));
  if (!server) {
    log_error("lsp_init: malloc failed");
    return;
  }

  strncpy(server->lang_id, lang_id, sizeof(server->lang_id) - 1);
  server->lang_id[sizeof(server->lang_id) - 1] = '\0';
  server->diagnostics = NULL;
  server->diagnostic_count = 0;
  server->diagnostics_version = 0;
  pthread_mutex_init(&server->diagnostics_mutex, NULL);
  server->buffer_pos = 0;
  server->next_id = 1;

  if (pipe(server->to_server_pipe) == -1 ||
      pipe(server->from_server_pipe) == -1) {
    log_error("lsp_init: pipe failed");
    free(server);
    return;
  }

  server->pid = fork();
  if (server->pid == -1) {
    log_error("lsp_init: fork failed");
    free(server);
    return;
  }

  if (server->pid == 0) { // Child process
    close(server->to_server_pipe[1]);
    dup2(server->to_server_pipe[0], STDIN_FILENO);
    close(server->to_server_pipe[0]);

    close(server->from_server_pipe[0]);
    dup2(server->from_server_pipe[1], STDOUT_FILENO);
    close(server->from_server_pipe[1]);

    char *cmd_copy = strdup(command);
    if (!cmd_copy) {
      log_error("lsp_init: strdup failed");
      exit(1);
    }
    char *args[64];
    int i = 0;
    char *token = strtok(cmd_copy, " ");
    while (token != NULL && i < 63) {
      args[i++] = token;
      token = strtok(NULL, " ");
    }
    args[i] = NULL;

    execvp(args[0], args);
    free(cmd_copy);
    log_error("lsp_init: execvp failed for %s", command);
    exit(1);
  } else { // Parent process
    close(server->to_server_pipe[0]);
    close(server->from_server_pipe[1]);
    log_info("LSP server for %s started with PID %d", server->lang_id,
             server->pid);

    lsp_servers[lsp_server_count++] = server;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(root, "id", server->next_id++);
    cJSON_AddStringToObject(root, "method", "initialize");

    cJSON *params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "processId", getpid());
    cJSON *client_info = cJSON_CreateObject();
    cJSON_AddStringToObject(client_info, "name", "arc");
    cJSON_AddStringToObject(client_info, "version", "0.1");
    cJSON_AddItemToObject(params, "clientInfo", client_info);

    char *project_root = find_project_root(file_name);
    if (project_root) {
        char root_uri[1024 + 7];
        snprintf(root_uri, sizeof(root_uri), "file://%s", project_root);
        cJSON_AddStringToObject(params, "rootUri", root_uri);
        free(project_root);
    } else {
        cJSON_AddStringToObject(params, "rootUri", "file:///");
    }

    cJSON *capabilities = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "capabilities", capabilities);
    cJSON_AddItemToObject(root, "params", params);

    lsp_send_message(server, root);
    cJSON_Delete(root);

    if (pthread_create(&server->reader_thread, NULL, lsp_reader_thread_func,
                       server) != 0) {
      log_error("lsp_init: failed to create reader thread");
    }
  }
}

void lsp_did_open(const char *file_path, const char *language_id,
                  const char *text) {
  LspServer *server = get_server(language_id);
  if (!server)
    return;

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "jsonrpc", "2.0");
  cJSON_AddStringToObject(root, "method", "textDocument/didOpen");

  cJSON *params = cJSON_CreateObject();
  cJSON *text_document = cJSON_CreateObject();
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

  lsp_send_message(server, root);
  cJSON_Delete(root);
}

void lsp_did_change(const char *file_path, const char *text, int version) {
  const char *lang_id = get_lang_id_from_filename(file_path);
  LspServer *server = get_server(lang_id);
  if (!server)
    return;

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "jsonrpc", "2.0");
  cJSON_AddStringToObject(root, "method", "textDocument/didChange");

  cJSON *params = cJSON_CreateObject();
  cJSON *text_document = cJSON_CreateObject();
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

  lsp_send_message(server, root);
  cJSON_Delete(root);
}

static void lsp_shutdown(LspServer *server) {
  if (server && server->pid > 0) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(root, "id", server->next_id++);
    cJSON_AddStringToObject(root, "method", "shutdown");
    lsp_send_message(server, root);
    cJSON_Delete(root);

    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "method", "exit");
    lsp_send_message(server, root);
    cJSON_Delete(root);

    pthread_join(server->reader_thread, NULL);

    kill(server->pid, SIGTERM);
    waitpid(server->pid, NULL, 0);
    close(server->to_server_pipe[1]);
    close(server->from_server_pipe[0]);
    log_info("LSP server for %s shut down", server->lang_id);

    for (int i = 0; i < server->diagnostic_count; i++) {
      free(server->diagnostics[i].message);
    }
    free(server->diagnostics);
    pthread_mutex_destroy(&server->diagnostics_mutex);
    free(server);
  }
}

void lsp_shutdown_all(void) {
  for (int i = 0; i < lsp_server_count; i++) {
    lsp_shutdown(lsp_servers[i]);
    lsp_servers[i] = NULL;
  }
  lsp_server_count = 0;
}

int lsp_get_diagnostics(const char *file_path, Diagnostic **out_diagnostics,
                        int *out_diagnostic_count) {
  const char *lang_id = get_lang_id_from_filename(file_path);
  LspServer *server = get_server(lang_id);
  if (!server) {
    *out_diagnostic_count = 0;
    *out_diagnostics = NULL;
    return 0;
  }

  pthread_mutex_lock(&server->diagnostics_mutex);
  *out_diagnostic_count = server->diagnostic_count;
  if (server->diagnostic_count > 0) {
    *out_diagnostics = malloc(sizeof(Diagnostic) * server->diagnostic_count);
    if (*out_diagnostics) {
      for (int i = 0; i < server->diagnostic_count; i++) {
        (*out_diagnostics)[i] = server->diagnostics[i];
        (*out_diagnostics)[i].message =
            strdup(server->diagnostics[i].message);
      }
    } else {
      *out_diagnostic_count = 0;
      *out_diagnostics = NULL;
    }
  } else {
    *out_diagnostics = NULL;
  }
  int version = server->diagnostics_version;
  pthread_mutex_unlock(&server->diagnostics_mutex);
  return version;
}

bool lsp_is_running(const char *language_id) {
  return get_server(language_id) != NULL;
}
