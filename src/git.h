#ifndef GIT_H
#define GIT_H

#include <time.h>
#include "buffer.h"

typedef enum {
    GIT_LINE_UNMODIFIED,
    GIT_LINE_ADDED,
    GIT_LINE_MODIFIED,
    GIT_LINE_DELETED
} GitLineStatus;

typedef struct GitHunk {
    int old_start;
    int old_count;
    int new_start;
    int new_count;
    struct GitHunk* next;
} GitHunk;

void git_update_diff(Buffer *buffer);
GitLineStatus git_get_line_status(const Buffer *buffer, int line_num, int* deleted_lines);
void git_current_branch(char *buffer, size_t buffer_size);

#endif
