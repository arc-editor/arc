#ifndef UTF8_H
#define UTF8_H

#include <stdio.h>
#include <stddef.h>

int read_utf8_char(FILE *fp, char *buf, size_t buf_size);
int read_utf8_char_from_stdin(char *buf, size_t buf_size);
int utf8_char_width(const char *s);

#endif // UTF8_H
