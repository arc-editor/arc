#include "language.h"
#include <string.h>

const char* get_language_id_from_file_name(const char *file_name) {
    if (!file_name) {
        return NULL;
    }

    const char *ext = strrchr(file_name, '.');
    if (!ext) {
        return NULL;
    }

    if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0) {
        return "c";
    } else if (strcmp(ext, ".js") == 0) {
        return "js";
    } else if (strcmp(ext, ".ts") == 0) {
        return "ts";
    } else if (strcmp(ext, ".go") == 0) {
        return "go";
    }

    return NULL;
}

const char* get_treesitter_language_name_from_file_name(const char *file_name) {
    if (!file_name) {
        return NULL;
    }

    const char *ext = strrchr(file_name, '.');
    if (!ext) {
        return NULL;
    }

    if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0) {
        return "c";
    } else if (strcmp(ext, ".js") == 0) {
        return "javascript";
    } else if (strcmp(ext, ".ts") == 0) {
        return "typescript";
    } else if (strcmp(ext, ".go") == 0) {
        return "go";
    }

    return NULL;
}
