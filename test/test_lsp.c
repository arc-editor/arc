#include "test_lsp.h"
#include "test.h"
#include "../src/config.h"
#include <string.h>
#include <unistd.h>

static const char* get_command_for_file(const Config *config, const char *file_name) {
    if (!file_name) {
        return NULL;
    }

    const char *ext = strrchr(file_name, '.');
    if (!ext) {
        return NULL;
    }

    char key[256];
    snprintf(key, sizeof(key), "lsp.%s", ext + 1);

    const char *command = NULL;
    if (config->toml_result.ok) {
        toml_datum_t cmd_datum = toml_seek(config->toml_result.toptab, key);
        if (cmd_datum.type == TOML_STRING) {
            command = cmd_datum.u.s;
        }
    }

    if (!command) {
        // Try defaults
        if (strcmp(ext + 1, "c") == 0 || strcmp(ext + 1, "h") == 0 || strcmp(ext + 1, "cpp") == 0) {
            command = "clangd";
        } else if (strcmp(ext + 1, "py") == 0) {
            command = "pylsp";
        } else if (strcmp(ext + 1, "rs") == 0) {
            command = "rust-analyzer";
        } else if (strcmp(ext + 1, "go") == 0) {
            command = "gopls";
        } else if (strcmp(ext + 1, "ts") == 0 || strcmp(ext + 1, "js") == 0) {
            command = "typescript-language-server --stdio";
        }
    }
    return command;
}

static void test_lsp_config_logic() {
    Config manual_config;
    const char* toml_content = "[lsp]\nc = \"my_clangd\"\n";
    manual_config.toml_result = toml_parse(toml_content, strlen(toml_content));
    ASSERT("test_lsp_config_logic: toml should parse", manual_config.toml_result.ok);

    const char *command = get_command_for_file(&manual_config, "test.c");
    ASSERT("test_lsp_config_logic: c command should not be null", command != NULL);
    ASSERT_STRING_EQUAL("test_lsp_config_logic: c command should be my_clangd", command, "my_clangd");

    command = get_command_for_file(&manual_config, "test.py");
    ASSERT("test_lsp_config_logic: py command should not be null", command != NULL);
    ASSERT_STRING_EQUAL("test_lsp_config_logic: py command should be pylsp", command, "pylsp");

    toml_free(manual_config.toml_result);
}

void test_lsp_suite(void) {
    printf("Running LSP tests...\n");
    test_lsp_config_logic();
}
