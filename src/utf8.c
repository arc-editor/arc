#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>

#include "utf8.h"
#include "log.h"

static int utf8_initialized = 0;

void init_utf8() {
    if (!utf8_initialized) {
        if (!setlocale(LC_CTYPE, "")) {
            log_warning("Could not set locale for UTF-8 support. Character widths may be incorrect.");
        }
        utf8_initialized = 1;
    }
}

int read_utf8_char(FILE *fp, char *buf, size_t buf_size) {
    int c = fgetc(fp);
    if (c == EOF) {
        return 0; // End of file
    }

    unsigned char first_byte = (unsigned char)c;
    int len;

    if (first_byte < 0x80) { // 0xxxxxxx
        len = 1;
    } else if ((first_byte & 0xE0) == 0xC0) { // 110xxxxx
        len = 2;
    } else if ((first_byte & 0xF0) == 0xE0) { // 1110xxxx
        len = 3;
    } else if ((first_byte & 0xF8) == 0xF0) { // 11110xxx
        len = 4;
    } else if ((first_byte & 0xFC) == 0xF8) { // 111110xx
        len = 5;
    } else if ((first_byte & 0xFE) == 0xFC) { // 1111110x
        len = 6;
    } else {
        // Invalid UTF-8 start byte. For simplicity, treat as a single byte.
        len = 1;
    }

    if ((size_t)len >= buf_size) {
        // Buffer not large enough
        ungetc(c, fp); // Push the byte back
        return -1; // Error
    }

    buf[0] = first_byte;
    for (int i = 1; i < len; i++) {
        c = fgetc(fp);
        if (c == EOF) {
            // Unexpected EOF
            return -1; // Error
        }
        buf[i] = (unsigned char)c;
    }
    buf[len] = '\0';

    return len;
}

int read_utf8_char_from_stdin(char *buf, size_t buf_size) {
    int c = getchar();
    if (c == EOF) {
        return 0; // End of file
    }

    unsigned char first_byte = (unsigned char)c;
    int len;

    if (first_byte < 0x80) { // 0xxxxxxx
        len = 1;
    } else if ((first_byte & 0xE0) == 0xC0) { // 110xxxxx
        len = 2;
    } else if ((first_byte & 0xF0) == 0xE0) { // 1110xxxx
        len = 3;
    } else if ((first_byte & 0xF8) == 0xF0) { // 11110xxx
        len = 4;
    } else if ((first_byte & 0xFC) == 0xF8) { // 111110xx
        len = 5;
    } else if ((first_byte & 0xFE) == 0xFC) { // 1111110x
        len = 6;
    } else {
        // Invalid UTF-8 start byte. For simplicity, treat as a single byte.
        len = 1;
    }

    if ((size_t)len >= buf_size) {
        // This should not happen if buf_size is large enough
        return -1; // Error
    }

    buf[0] = first_byte;
    for (int i = 1; i < len; i++) {
        c = getchar();
        if (c == EOF) {
            // Unexpected EOF
            return -1; // Error
        }
        buf[i] = (unsigned char)c;
    }
    buf[len] = '\0';

    return len;
}

int utf8_char_width(const char *s) {
    init_utf8();

    if (!s || !*s) {
        return 0;
    }

    wchar_t wc;
    int bytes_converted = mbtowc(&wc, s, MB_CUR_MAX);

    if (bytes_converted <= 0) {
        // Invalid or null character, treat as width 1 (e.g., for a replacement character)
        return 1;
    }

    int width = wcwidth(wc);
    if (width < 0) {
        // E.g., for control characters
        return 0;
    }

    return width;
}
