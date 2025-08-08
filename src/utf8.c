#include "utf8.h"
#include <stdint.h>
#include <stddef.h>

// s must be a non-null pointer to a byte sequence
// codepoint must be a non-null pointer
int utf8_decode(const unsigned char *s, uint32_t *codepoint) {
    if (s == NULL || codepoint == NULL) {
        return 0;
    }

    if (s[0] < 0x80) {
        // 1-byte sequence (ASCII)
        *codepoint = s[0];
        return 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        // 2-byte sequence
        if ((s[1] & 0xC0) != 0x80) return 0; // Invalid sequence
        *codepoint = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        return 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        // 3-byte sequence
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return 0; // Invalid sequence
        *codepoint = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    } else if ((s[0] & 0xF8) == 0xF0) {
        // 4-byte sequence
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return 0; // Invalid sequence
        *codepoint = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        return 4;
    }

    return 0; // Invalid start byte
}

// s must be a non-null pointer to a buffer of at least 4 bytes
int utf8_encode(uint32_t codepoint, char *s) {
    if (s == NULL) {
        return 0;
    }

    if (codepoint < 0x80) {
        s[0] = codepoint;
        return 1;
    } else if (codepoint < 0x800) {
        s[0] = 0xC0 | (codepoint >> 6);
        s[1] = 0x80 | (codepoint & 0x3F);
        return 2;
    } else if (codepoint < 0x10000) {
        s[0] = 0xE0 | (codepoint >> 12);
        s[1] = 0x80 | ((codepoint >> 6) & 0x3F);
        s[2] = 0x80 | (codepoint & 0x3F);
        return 3;
    } else if (codepoint < 0x110000) {
        s[0] = 0xF0 | (codepoint >> 18);
        s[1] = 0x80 | ((codepoint >> 12) & 0x3F);
        s[2] = 0x80 | ((codepoint >> 6) & 0x3F);
        s[3] = 0x80 | (codepoint & 0x3F);
        return 4;
    }

    return 0; // Invalid codepoint
}
