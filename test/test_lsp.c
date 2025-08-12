#include "test_lsp.h"
#include "test.h"
#include "../src/config.h"
#include "../src/lsp.h"
#include <unistd.h>

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

void test_lsp_suite(void) {
    printf("Running LSP tests...\n");
    test_multi_server_lifecycle();
}
