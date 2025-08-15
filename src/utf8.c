#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>
#include <unistd.h>
#include <errno.h>
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

int read_utf8_char_from_stdin(char *buf, size_t buf_size) {
    char first_byte_char;
    ssize_t nread;

    do {
        nread = read(STDIN_FILENO, &first_byte_char, 1);
    } while (nread == -1 && errno == EINTR);

    if (nread <= 0) {
        return 0; // EOF or error
    }

    unsigned char first_byte = (unsigned char)first_byte_char;
    int len;

    if (first_byte < 0x80) { // 0xxxxxxx
        len = 1;
    } else if ((first_byte & 0xE0) == 0xC0) { // 110xxxxx
        len = 2;
    } else if ((first_byte & 0xF0) == 0xE0) { // 1110xxxx
        len = 3;
    } else if ((first_byte & 0xF8) == 0xF0) { // 11110xxx
        len = 4;
    } else {
        len = 1;
    }

    if ((size_t)len >= buf_size) {
        return -1; // Error
    }

    buf[0] = first_byte;
    if (len > 1) {
        ssize_t total_read = 0;
        while (total_read < len - 1) {
            nread = read(STDIN_FILENO, buf + 1 + total_read, len - 1 - total_read);
            if (nread == -1) {
                if (errno == EINTR) continue;
                return -1; // other read error
            }
            if (nread == 0) return -1; // EOF in the middle of a char
            total_read += nread;
        }
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

int utf8_char_len(const char *s) {
    if (!s || !*s) {
        return 0; // No character, no length
    }
    unsigned char first_byte = (unsigned char)s[0];
    if (first_byte < 0x80) {        // 0xxxxxxx
        return 1;
    } else if ((first_byte & 0xE0) == 0xC0) { // 110xxxxx
        return 2;
    } else if ((first_byte & 0xF0) == 0xE0) { // 1110xxxx
        return 3;
    } else if ((first_byte & 0xF8) == 0xF0) { // 11110xxx
        return 4;
    } else {
        return 1;
    }
}


size_t utf8_strlen(const char *s) {
    if (!s) return 0;
    size_t char_count = 0;
    const unsigned char *p = (const unsigned char*)s;
    while (*p) {
        char_count++;
        if (*p < 0x80) { // 0xxxxxxx
            p += 1;
        } else if ((*p & 0xE0) == 0xC0) { // 110xxxxx
            p += 2;
        } else if ((*p & 0xF0) == 0xE0) { // 1110xxxx
            p += 3;
        } else if ((*p & 0xF8) == 0xF0) { // 11110xxx
            p += 4;
        } else {
            p += 1;
        }
    }
    return char_count;
}

size_t utf8_strlen_len(const char *s, size_t n) {
    if (!s) return 0;
    size_t char_count = 0;
    const unsigned char *p = (const unsigned char*)s;
    const unsigned char *end = p + n;
    while (p < end) {
        char_count++;
        if (*p < 0x80) {
            p += 1;
        } else if ((*p & 0xE0) == 0xC0) {
            p += 2;
        } else if ((*p & 0xF0) == 0xE0) {
            p += 3;
        } else if ((*p & 0xF8) == 0xF0) {
            p += 4;
        } else {
            p += 1;
        }
    }
    return char_count;
}
