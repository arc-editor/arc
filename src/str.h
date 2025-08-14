#ifndef STR_H
#define STR_H

char* str_dup(const char* s);

// The returned pointer is guaranteed to live at least as long as file_name.
// The caller does NOT own the returned string and must NOT free it.
const char *str_get_lang_name_from_file_name(const char *file_name);

#endif
