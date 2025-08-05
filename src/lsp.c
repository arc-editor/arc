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
static pthread_mutex_t diagnostics_mutex = PTHREAD_MUTEX_INITIALIZER;
static int lsp_next_id = 1;

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
            cJSON *diagnostics_json = cJSON_GetObjectItem(params, "diagnostics");

            pthread_mutex_lock(&diagnostics_mutex);

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
                for (int i = 0; i < new_diagnostic_count; i++) {
                    cJSON *diag_json = cJSON_GetArrayItem(diagnostics_json, i);
                    cJSON *range = cJSON_GetObjectItem(diag_json, "range");
                    cJSON *start = cJSON_GetObjectItem(range, "start");
                    cJSON *end = cJSON_GetObjectItem(range, "end");
                    cJSON *message_json = cJSON_GetObjectItem(diag_json, "message");

                    diagnostics[i].line = cJSON_GetObjectItem(start, "line")->valueint;
                    diagnostics[i].col_start = cJSON_GetObjectItem(start, "character")->valueint;
                    diagnostics[i].col_end = cJSON_GetObjectItem(end, "character")->valueint;
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

    write(to_server_pipe[1], header, strlen(header));
    write(to_server_pipe[1], message, len);

    free(message);
}

cJSON *lsp_read_message() {
    // This is a simplified implementation. A real implementation would need to
    // handle partial reads and be non-blocking.
    char buffer[4096];
    ssize_t bytes_read = read(from_server_pipe[0], buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        return NULL;
    }
    buffer[bytes_read] = '\0';

    char *json_str = strstr(buffer, "\r\n\r\n");
    if (!json_str) {
        return NULL;
    }
    json_str += 4; // Skip the header separator

    return cJSON_Parse(json_str);
}

void lsp_init(void) {
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

        execlp("clangd", "clangd", NULL);
        log_error("lsp.lsp_init: execlp failed");
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
            char root_uri[1024];
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
    cJSON_AddStringToObject(text_document, "uri", file_path);
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
    cJSON_AddStringToObject(text_document, "uri", file_path);
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
        cJSON_AddNumberToObject(root, "id", lsp_next_id++); // Use a unique ID
        cJSON_AddStringToObject(root, "method", "shutdown");
        lsp_send_message(root);
        cJSON_Delete(root);

        // Sending exit notification
        root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "jsonrpc", "2.0");
        cJSON_AddStringToObject(root, "method", "exit");
        lsp_send_message(root);
        cJSON_Delete(root);

        // Wait for the reader thread to finish
        pthread_join(lsp_reader_thread, NULL);

        kill(lsp_server_pid, SIGTERM);
        waitpid(lsp_server_pid, NULL, 0);
        close(to_server_pipe[1]);
        close(from_server_pipe[0]);
        log_info("lsp.lsp_shutdown: LSP server shut down");
    }
}

void lsp_get_diagnostics(const char *file_path __attribute__((unused)), Diagnostic **out_diagnostics, int *out_diagnostic_count) {
    pthread_mutex_lock(&diagnostics_mutex);
    *out_diagnostic_count = diagnostic_count;
    if (diagnostic_count > 0) {
        *out_diagnostics = malloc(sizeof(Diagnostic) * diagnostic_count);
        memcpy(*out_diagnostics, diagnostics, sizeof(Diagnostic) * diagnostic_count);
    } else {
        *out_diagnostics = NULL;
    }
    pthread_mutex_unlock(&diagnostics_mutex);
}
