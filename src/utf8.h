#ifndef UTF8_H
#define UTF8_H

#include <stdint.h>
#include <stddef.h>

int utf8_decode(const unsigned char *s, uint32_t *codepoint);
int utf8_encode(uint32_t codepoint, char *s);

#endif
