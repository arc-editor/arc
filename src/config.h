#ifndef CONFIG_H
#define CONFIG_H

#include "tree_sitter/api.h"
#include "theme.h"

typedef struct {
  char *theme;  
} Config;

typedef TSLanguage *(*TSLanguageFn)(void);

TSLanguage *config_load_language(const char *name);
TSQuery *config_load_highlights(TSLanguage *language, const char *name);
void config_load_theme(char *name, Theme *theme);
void config_load(Config *config);
void config_destroy(Config *config);
int config_mkdir_p(const char *path);

#endif
