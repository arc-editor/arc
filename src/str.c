#include <string.h>
#include <stdlib.h>

char* str_dup(const char* s) {
    size_t len = strlen(s) + 1;
    char* new_str = malloc(len);
    if (new_str == NULL) return NULL;
    return memcpy(new_str, s, len);
}

const char *str_get_lang_name_from_file_name(const char *file_name) {
  if (!file_name) return NULL;
  const char *ext = strrchr(file_name, '.');
  if (!ext) return NULL;
  if (!strcmp(ext + 1, "h")) return "c";
  if (!strcmp(ext + 1, "js")) return "javascript";
  if (!strcmp(ext + 1, "ts")) return "typescript";
  return ext + 1;
}
