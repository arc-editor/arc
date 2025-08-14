#ifndef CONFIG_H
#define CONFIG_H

#include "tree_sitter/api.h"
#include "theme.h"
#include "tomlc17.h"

typedef enum {
    WHITESPACE_RENDER_NONE,
    WHITESPACE_RENDER_ALL,
    WHITESPACE_RENDER_TRAILING,
} WhitespaceRender;

typedef struct {
    WhitespaceRender space;
    WhitespaceRender tab;
    char *space_char;
    char *tab_char;
} WhitespaceConfig;

typedef struct {
  char *theme;
  WhitespaceConfig whitespace;
  toml_result_t toml_result;
} Config;

typedef TSLanguage *(*TSLanguageFn)(void);

TSLanguage *config_load_language(const char *name);
TSQuery *config_load_highlights(TSLanguage *language, const char *name);
void config_load_theme(const char *name, Theme *theme);
void config_init(Config *config);
void config_load(Config *config);
void config_destroy(Config *config);
int config_mkdir_p(const char *path);

#endif
