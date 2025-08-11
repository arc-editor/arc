#ifndef UTF8_H
#define UTF8_H

#include <stdio.h>
#include <stddef.h>

#include <stddef.h>

int read_utf8_char(FILE *fp, char *buf, size_t buf_size);
int read_utf8_char_from_stdin(char *buf, size_t buf_size);
int utf8_char_width(const char *s);
size_t utf8_strlen(const char *s);
int utf8_char_len(const char *s);
int utf8_char_to_byte_index(const char *s, int char_index);
int utf8_char_count(const char *s, int byte_index);

#endif // UTF8_H
