#include "test_lsp.h"
#include "test.h"
#include "../src/config.h"
#include "../src/lsp.h"
#include <unistd.h>
#include <string.h>
#include <signal.h>

static void test_multi_server_lifecycle() {
    Config config;
    config_init(&config); // Assumes a function to initialize config

    // Simulate opening a C file
    lsp_init(&config, "test.c");
    ASSERT("C server should be running", lsp_is_running("c"));
    ASSERT("Go server should not be running", !lsp_is_running("py"));

    // Simulate opening a Go file
    lsp_init(&config, "test.go");
    ASSERT("C server should still be running", lsp_is_running("c"));
    ASSERT("Go server should be running", lsp_is_running("go"));

    // Shutdown all servers
    lsp_shutdown_all();
    ASSERT("C server should not be running after shutdown", !lsp_is_running("c"));
    ASSERT("Go server should not be running after shutdown", !lsp_is_running("go"));

    config_destroy(&config); // Assumes a function to clean up config
}

static void test_per_file_diagnostics() {
    Config config;
    config_init(&config);
    lsp_init(&config, "/tmp/test.c");

    const char *diag1 = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"file:///tmp/test1.c\",\"diagnostics\":[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":1}},\"message\":\"Error 1\",\"severity\":1}]}}";
    const char *diag2 = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"file:///tmp/test2.c\",\"diagnostics\":[{\"range\":{\"start\":{\"line\":1,\"character\":0},\"end\":{\"line\":1,\"character\":1}},\"message\":\"Error 2\",\"severity\":1}]}}";

    lsp_test_inject_message("c", diag1);
    lsp_test_inject_message("c", diag2);

    Diagnostic *diags;
    int count;

    lsp_get_diagnostics("/tmp/test1.c", &diags, &count);
    ASSERT("Should have 1 diagnostic for test1.c", count == 1);
    if (count == 1) {
        ASSERT("Correct message for test1.c", strcmp(diags[0].message, "Error 1") == 0);
        free(diags[0].message);
        free(diags);
    }

    lsp_get_diagnostics("/tmp/test2.c", &diags, &count);
    ASSERT("Should have 1 diagnostic for test2.c", count == 1);
    if (count == 1) {
        ASSERT("Correct message for test2.c", strcmp(diags[0].message, "Error 2") == 0);
        free(diags[0].message);
        free(diags);
    }

    lsp_shutdown_all();
    config_destroy(&config);
}

void test_lsp_suite(void) {
    printf("Running LSP tests...\n");
    test_multi_server_lifecycle();
    test_per_file_diagnostics();
}
