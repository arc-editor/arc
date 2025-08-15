#include <string.h>
#include <stdlib.h>

const char *str_get_lang_name_from_file_name(const char *file_name) {
  if (!file_name) return NULL;
  const char *ext = strrchr(file_name, '.');
  if (!ext) return NULL;
  if (!strcmp(ext + 1, "h")) return "c";
  if (!strcmp(ext + 1, "js")) return "javascript";
  if (!strcmp(ext + 1, "ts")) return "typescript";
  return ext + 1;
}

const char *str_uri_to_relative_path(const char *uri, const char *cwd) {
  if (!uri) return NULL;
  const char *relative = uri;
  if (strncmp(relative, "file://", 7) == 0) {
    relative += 7;
  }
  size_t cwd_len = strlen(cwd);
  if (strncmp(relative, cwd, cwd_len) == 0) {
    relative += cwd_len;
    if (relative[0] == '/') {
      relative++;
    }
  }
  return relative;
}
