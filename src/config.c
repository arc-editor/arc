#define _POSIX_C_SOURCE 200809L

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/stat.h>
#include <errno.h>
#include "tomlc17.h"
#include "tree_sitter/api.h"
#include "config.h"
#include "log.h"
#include "theme.h"

// Create directories recursively (like mkdir -p)
int mkdir_recursive(const char *path, __mode_t mode) {
    char *path_copy = strdup(path);
    if (!path_copy) {
        return -1;
    }
    
    char *p = path_copy;
    
    // Skip leading slash if present
    if (*p == '/') {
        p++;
    }
    
    while ((p = strchr(p, '/'))) {
        *p = '\0';
        
        if (mkdir(path_copy, mode) != 0 && errno != EEXIST) {
            free(path_copy);
            return -1;
        }
        
        *p = '/';
        p++;
    }
    
    // Create the final directory
    if (mkdir(path_copy, mode) != 0 && errno != EEXIST) {
        free(path_copy);
        return -1;
    }
    
    free(path_copy);
    return 0;
}

// Extract directory path from full file path and create directories
int ensure_directory_exists(const char *file_path) {
    char *path_copy = strdup(file_path);
    if (!path_copy) {
        log_warning("ensure_directory_exists: strdup failed");
        return -1;
    }
    
    // Find the last slash to separate directory from filename
    char *last_slash = strrchr(path_copy, '/');
    if (!last_slash) {
        // No directory component, just a filename
        free(path_copy);
        return 0;
    }
    
    // Terminate string at last slash to get directory path
    *last_slash = '\0';
    
    // Create the directory structure
    if (mkdir_recursive(path_copy, 0755) != 0) {
        log_warning("ensure_directory_exists: mkdir_recursive failed for %s", path_copy);
        free(path_copy);
        return -1;
    }
    
    free(path_copy);
    return 0;
}

// caller must free the return value
char *get_path(const char *partial_format, ...) {
    const char *home_dir = getenv("HOME");
    if (!home_dir) {
        log_warning("config.make_config_path: unable to get env HOME");
        return NULL;
    }

    // Format the partial path with variadic arguments
    va_list args;
    va_start(args, partial_format);
    int partial_len = vsnprintf(NULL, 0, partial_format, args);
    va_end(args);

    if (partial_len < 0) {
        log_warning("config.make_config_path: vsnprintf failed (size calc)");
        return NULL;
    }

    char *partial_path = malloc(partial_len + 1);
    if (!partial_path) {
        log_warning("config.make_config_path: malloc failed for partial_path");
        return NULL;
    }

    va_start(args, partial_format);
    vsnprintf(partial_path, partial_len + 1, partial_format, args);
    va_end(args);

    const char *prefix = "/.config/arc";
    size_t full_len = strlen(home_dir) + strlen(prefix) + partial_len + 1;
    char *full_path = malloc(full_len);
    if (!full_path) {
        log_warning("config.make_config_path: malloc failed for full_path");
        free(partial_path);
        return NULL;
    }

    snprintf(full_path, full_len, "%s%s%s", home_dir, prefix, partial_path);
    free(partial_path);
    if (ensure_directory_exists(full_path) != 0) {
        log_warning("config.make_config_path: failed to create directories for %s", full_path);
        free(full_path);
        return NULL;
    }
    return full_path;
}

TSLanguage *config_load_language(char *name) {
    if (name == NULL) return NULL;
    char *parser_path = get_path("/grammars/%s.so", name);
    if (!parser_path) {
        log_error("config.load_language: make_config_path failed");
        return NULL;
    }
    
    size_t symbol_len = strlen("tree_sitter_") + strlen(name) + 1;
    char *symbol = malloc(symbol_len);
    if (!symbol) {
        log_error("config.load_language: memory allocation failed for symbol");
        free(parser_path);
        return NULL;
    }
    snprintf(symbol, symbol_len, "tree_sitter_%s", name);
    
    void *handle = dlopen(parser_path, RTLD_NOW);
    if (!handle) {
        log_warning("config.load_language: dlopen failed: %s", dlerror());
        free(parser_path);
        free(symbol);
        return NULL;
    }
    
    dlerror(); // Clear any existing error
    
    TSLanguageFn fn = (TSLanguageFn)dlsym(handle, symbol);
    const char *err = dlerror();
    if (err != NULL) {
        log_error("config.load_language: dlsym failed: %s", err);
        dlclose(handle);
        free(parser_path);
        free(symbol);
        return NULL;
    }
    
    log_info("config.load_language: success: %s from %s", symbol, parser_path);
    
    // Note: We don't dlclose(handle) here because the TSLanguage object
    // may contain function pointers that need the library to remain loaded
    
    free(parser_path);
    free(symbol);
    
    return fn();
}

TSQuery *config_load_highlights(TSLanguage *language, char *name) {
    if (name == NULL || language == NULL) return NULL;
    char *highlights_path = get_path("/highlights/%s.scm", name);
    if (!highlights_path) {
        log_error("config.config_load_highlights: make_config_path failed");
        return NULL;
    }
    
    FILE *file = fopen(highlights_path, "r");
    if (!file) {
        log_warning("config.config_load_highlights: failed to open highlights file");
        free(highlights_path);
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size <= 0) {
        log_error("config.config_load_highlights: invalid or empty highliths file");
        fclose(file);
        free(highlights_path);
        return NULL;
    }
    
    // Allocate buffer and read file
    char *highlights_scm = malloc(file_size + 1);
    if (!highlights_scm) {
        log_error("config.config_load_highlights: failed to allocate memory for highliths");
        fclose(file);
        free(highlights_path);
        return NULL;
    }
    
    size_t bytes_read = fread(highlights_scm, 1, file_size, file);
    if ((long)bytes_read != file_size) {
        log_error("config.config_load_highlights: failed to read complete highlights file");
        free(highlights_scm);
        free(highlights_path);
        fclose(file);
        return NULL;
    }
    
    highlights_scm[file_size] = '\0';
    fclose(file);
    
    // Parse the query
    uint32_t error_offset;
    TSQueryError error_type;
    
    TSQuery *query = ts_query_new(
        language,
        highlights_scm,
        strlen(highlights_scm),
        &error_offset,
        &error_type
    );
    free(highlights_scm);
    if (!query) {
        log_warning("config.config_load_highlights: failed to parse highlight query at offset %u", error_offset);
        free(highlights_path);
        return NULL;
    }
    log_info("loaded highlights from %s", highlights_path);
    free(highlights_path);
    return query;
}

void config_load(Config *config) {
    config->theme = strdup("default");

    char *path = get_path("/config.toml");
    if (!path) {
        log_error("config.config_load: make_config_path failed");
        return;
    }
    
    toml_result_t result = toml_parse_file_ex(path);
    if (!result.ok) {
        log_warning("config.config_load: cannot parse %s - %s", path, result.errmsg);
        return;
    }

    toml_datum_t theme_data = toml_seek(result.toptab, "theme");
    if (theme_data.type == TOML_STRING) {
        config->theme = strdup(theme_data.u.s);
    }

    free(path);
    toml_free(result);
}

void config_destroy(Config *config) {
    free(config->theme);    
}

void config_load_theme(char *name, Theme *theme) {
    if (name == NULL) return;
    char *path = get_path("/themes/%s.toml", name);
    if (!path) {
        log_error("config.config_load_theme: make_config_path failed");
        return;
    }
    theme_load(path, theme);
    free(path);
}
