#ifndef GIT_DIFF_H
#define GIT_DIFF_H

#include "buffer.h"

typedef struct GitHunk {
    int old_start;
    int old_count;
    int new_start;
    int new_count;
} GitHunk;

void diff_lines(const char* old_content, const char* new_content, Buffer* buffer);

#endif // GIT_DIFF_H
