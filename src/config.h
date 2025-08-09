#ifndef CONFIG_H
#define CONFIG_H

#include "tree_sitter/api.h"
#include "theme.h"

typedef enum {
    WHITESPACE_RENDER_NONE,
    WHITESPACE_RENDER_ALL,
    WHITESPACE_RENDER_TRAILING,
} WhitespaceRender;

typedef struct {
    WhitespaceRender space;
    WhitespaceRender tab;
} WhitespaceConfig;

typedef struct {
  char *theme;
  WhitespaceConfig whitespace;
} Config;

typedef TSLanguage *(*TSLanguageFn)(void);

TSLanguage *config_load_language(char *name);
TSQuery *config_load_highlights(TSLanguage *language, char *name);
void config_load_theme(char *name, Theme *theme);
void config_load(Config *config);
void config_destroy(Config *config);
int config_mkdir_p(const char *path);

#endif
