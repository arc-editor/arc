#define _XOPEN_SOURCE 700
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
#include <limits.h>
#include <time.h>

#define MAX_LSP_SERVERS 10

char *find_project_root(const char *file_path) {
    char path[PATH_MAX];
    if (realpath(file_path, path) == NULL) {
        // Fallback to the original path if realpath fails
        strncpy(path, file_path, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }

    char *last_slash = strrchr(path, '/');
    if (last_slash) {
        *last_slash = '\0';
    }

    char check_path[PATH_MAX];
    struct stat st;

    char *root_path = NULL;

    while (strlen(path) > 0) {
        snprintf(check_path, sizeof(check_path), "%s/.git", path);
        if (stat(check_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            root_path = strdup(path);
            break;
        }

        snprintf(check_path, sizeof(check_path), "%s/go.mod", path);
        if (stat(check_path, &st) == 0 && S_ISREG(st.st_mode)) {
            root_path = strdup(path);
            break;
        }

        snprintf(check_path, sizeof(check_path), "%s/package.json", path);
        if (stat(check_path, &st) == 0 && S_ISREG(st.st_mode)) {
            root_path = strdup(path);
            break;
        }

        last_slash = strrchr(path, '/');
        if (last_slash) {
            *last_slash = '\0';
        } else {
            break;
        }
    }

    if (!root_path) {
        if (realpath(file_path, path) == NULL) {
            strncpy(path, file_path, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
        }
        last_slash = strrchr(path, '/');
        if (last_slash) {
            *last_slash = '\0';
            root_path = strdup(path);
        } else {
            root_path = strdup(".");
        }
    }

    char absolute_path[PATH_MAX];
    if (realpath(root_path, absolute_path) == NULL) {
        return root_path;
    }

    free(root_path);
    return strdup(absolute_path);
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
  ext++;
  const char *name;
  if (!strcmp(ext, "js")) {
    name = "javascript";
  } else if (!strcmp(ext, "ts")) {
    name = "typescript";
  } else {
    name = ext;
  }
  return name;
}

static bool lsp_wait_for_initialization(LspServer *server) {
  if (!server) return false;

  pthread_mutex_lock(&server->init_mutex);
  if (!server->initialized) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5; // 5 second timeout
    int rc = pthread_cond_timedwait(&server->init_cond, &server->init_mutex, &ts);
    if (rc != 0) {
      log_error("lsp_wait_for_initialization: timeout waiting for server to initialize");
      pthread_mutex_unlock(&server->init_mutex);
      return false;
    }
  }
  pthread_mutex_unlock(&server->init_mutex);
  return true;
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

  ssize_t header_written = write(server->to_server_pipe[1], header, strlen(header));
  ssize_t message_written = write(server->to_server_pipe[1], message, len);
  log_info("lsp.lsp_send_message: %s", message);

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
    if (method && strcmp(method->valuestring, "textDocument/publishDiagnostics") == 0) {
      cJSON *params = cJSON_GetObjectItem(message, "params");
      if (!params) {
        cJSON_Delete(message);
        continue;
      }
      cJSON *uriobj = cJSON_GetObjectItem(params, "uri");
      if (!uriobj) {
        cJSON_Delete(message);
        continue;
      }
      char *uri = strdup(uriobj->valuestring);
      cJSON *diagnostics_json = cJSON_GetObjectItem(params, "diagnostics");
      if (!diagnostics_json) {
        cJSON_Delete(message);
        continue;
      }

      pthread_mutex_lock(&server->diagnostics_mutex);
      server->diagnostics_version++;

      
      int incoming_diagnostic_count = cJSON_GetArraySize(diagnostics_json);
      Diagnostic *new_diagnostics = malloc(sizeof(Diagnostic) * (incoming_diagnostic_count + server->diagnostic_count));
      if (!new_diagnostics) {
        pthread_mutex_unlock(&server->diagnostics_mutex);
        cJSON_Delete(message);
        continue;
      }
      int new_diagnostics_count = 0;
      for (int i = 0; i < server->diagnostic_count; i++) {
        if (!strcmp(server->diagnostics[i].uri, uri)) {
          free(server->diagnostics[i].message);
          free(server->diagnostics[i].uri);
        } else {
          new_diagnostics[new_diagnostics_count++] = server->diagnostics[i];
        }
      }
      free(uri);
      free(server->diagnostics);
      server->diagnostics = new_diagnostics;
      server->diagnostic_count = new_diagnostics_count + incoming_diagnostic_count;
      if (incoming_diagnostic_count > 0) {
        for (int i = 0; i < incoming_diagnostic_count; i++) {
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
          int new_index = new_diagnostics_count + i;
          server->diagnostics[new_index].line = line_obj->valueint;
          server->diagnostics[new_index].col_start = char_start_obj->valueint;
          server->diagnostics[new_index].col_end = char_end_obj->valueint;

          if (severity_json) {
            server->diagnostics[new_index].severity = (DiagnosticSeverity)severity_json->valueint;
          } else {
            server->diagnostics[new_index].severity = LSP_DIAGNOSTIC_SEVERITY_HINT;
          }
          server->diagnostics[new_index].uri = strdup(uriobj->valuestring);
          server->diagnostics[new_index].message = strdup(message_json->valuestring);
        }
      }

      pthread_mutex_unlock(&server->diagnostics_mutex);
      editor_request_redraw();
    } else if (cJSON_GetObjectItem(message, "id") && cJSON_GetObjectItem(message, "id")->valueint == 1) {
      log_info("lsp.lsp_reader_thread_func: received initialize response");
      pthread_mutex_lock(&server->init_mutex);
      server->initialized = true;
      pthread_cond_signal(&server->init_cond);
      pthread_mutex_unlock(&server->init_mutex);
      cJSON *root = cJSON_CreateObject();
      cJSON_AddStringToObject(root, "jsonrpc", "2.0");
      cJSON_AddStringToObject(root, "method", "initialized");
      cJSON_AddItemToObject(root, "params", cJSON_CreateObject());
      lsp_send_message(server, root);
      cJSON_Delete(root);
    } else {
      char *message_str = cJSON_PrintUnformatted(message);
      log_warning("lsp.lsp_reader_thread_func: unhandled message %s", message_str);
      free(message_str);
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
    } else if (strcmp(lang_id, "python") == 0) {
      command = "pylsp";
    } else if (strcmp(lang_id, "rs") == 0) {
      command = "rust-analyzer";
    } else if (strcmp(lang_id, "go") == 0) {
      command = "gopls";
    } else if (strcmp(lang_id, "typescript") == 0 || strcmp(lang_id, "javascript") == 0) {
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
  pthread_mutex_init(&server->init_mutex, NULL);
  pthread_cond_init(&server->init_cond, NULL);
  server->buffer_pos = 0;
  server->next_id = 1;
  server->initialized = false;

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

    int dev_null = open("/dev/null", O_WRONLY);
    if (dev_null != -1) {
      dup2(dev_null, STDERR_FILENO);
      close(dev_null);
    }

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
    log_info("LSP server for %s started with PID %d", server->lang_id, server->pid);

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

        // For backwards compatibility with older LSP servers
        cJSON_AddStringToObject(params, "rootPath", project_root);
        cJSON_AddStringToObject(params, "rootUri", root_uri);

        // For modern LSP servers
        cJSON *workspace_folders = cJSON_CreateArray();
        cJSON *folder = cJSON_CreateObject();
        cJSON_AddStringToObject(folder, "uri", root_uri);
        cJSON_AddStringToObject(folder, "name", "root");
        cJSON_AddItemToArray(workspace_folders, folder);
        cJSON_AddItemToObject(params, "workspaceFolders", workspace_folders);

        free(project_root);
    } else {
        cJSON_AddNullToObject(params, "rootUri");
        cJSON_AddNullToObject(params, "rootPath");
    }

    cJSON *capabilities = cJSON_CreateObject();

    cJSON *workspace = cJSON_CreateObject();
    cJSON_AddBoolToObject(workspace, "workspaceFolders", true);
    cJSON_AddItemToObject(capabilities, "workspace", workspace);
    cJSON_AddItemToObject(params, "capabilities", capabilities);
    cJSON_AddItemToObject(root, "params", params);

    lsp_send_message(server, root);
    cJSON_Delete(root);

    if (pthread_create(&server->reader_thread, NULL, lsp_reader_thread_func, server) != 0) {
      log_error("lsp_init: failed to create reader thread");
    }
  }
}

void lsp_did_open(const char *file_path, const char *language_id, const char *text) {
  LspServer *server = get_server(language_id);
  if (!server) return;

  if (!lsp_wait_for_initialization(server)) {
    return;
  }

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "jsonrpc", "2.0");
  cJSON_AddStringToObject(root, "method", "textDocument/didOpen");

  cJSON *params = cJSON_CreateObject();
  cJSON *text_document = cJSON_CreateObject();
  char uri[1024 + 7];
  snprintf(uri, sizeof(uri), "file://%s", file_path);
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

  if (!lsp_wait_for_initialization(server)) {
    return;
  }

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
    pthread_mutex_destroy(&server->init_mutex);
    pthread_cond_destroy(&server->init_cond);
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

int lsp_get_diagnostics(const char *file_path, Diagnostic **out_diagnostics, int *out_diagnostic_count) {
  char *project_root = find_project_root(file_path);
  if (!project_root) {
    *out_diagnostic_count = 0;
    *out_diagnostics = NULL;
    return 0;
  }
  char root_uri[1024 + 7];
  snprintf(root_uri, sizeof(root_uri), "file://%s/%s", project_root, file_path);

  const char *lang_id = get_lang_id_from_filename(file_path);
  LspServer *server = get_server(lang_id);
  if (!server) {
    *out_diagnostic_count = 0;
    *out_diagnostics = NULL;
    return 0;
  }

  pthread_mutex_lock(&server->diagnostics_mutex);
  *out_diagnostic_count = 0;
  if (server->diagnostic_count > 0) {
    *out_diagnostics = malloc(sizeof(Diagnostic) * server->diagnostic_count);
    if (*out_diagnostics) {
      for (int i = 0; i < server->diagnostic_count; i++) {
        if (strcmp(server->diagnostics[i].uri, root_uri)) continue;
        (*out_diagnostics)[*out_diagnostic_count] = server->diagnostics[i];
        (*out_diagnostics)[*out_diagnostic_count].message = strdup(server->diagnostics[i].message);
        (*out_diagnostic_count)++;
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

