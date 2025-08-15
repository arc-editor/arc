#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "log.h"
#include "git.h"
#include "buffer.h"

static char cached_branch_name[256] = "";
static long last_update_ms = 0;

long get_current_time_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000L + now.tv_nsec / 1000000L;
}

void git_current_branch(char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        log_error("git.git_current_branch: null buffer provided to git_current_branch");
        exit(1);
    }
    buffer[0] = '\0';

    long now_ms = get_current_time_ms();
    if (now_ms - last_update_ms < 1000) {
        snprintf(buffer, buffer_size, "%s", cached_branch_name);
        return;
    }
    last_update_ms = now_ms;

    FILE *head_file;
    char line[256];

    head_file = fopen(".git/HEAD", "r");
    if (head_file == NULL) {
        snprintf(buffer, buffer_size, "");
        snprintf(cached_branch_name, sizeof(cached_branch_name), "%s", buffer);
        return;
    }

    if (fgets(line, sizeof(line), head_file) != NULL) {
        line[strcspn(line, "\n")] = 0; // Remove trailing newline

        if (strncmp(line, "ref: ", 4) == 0) {
            char *branch_path = line + 4;
            char *branch_name_start = strstr(branch_path, "refs/heads/");
            if (branch_name_start != NULL) {
                branch_name_start += strlen("refs/heads/");
                snprintf(buffer, buffer_size, "%s", branch_name_start);
            } else {
                snprintf(buffer, buffer_size, "UNKNOWN_REF_FORMAT");
            }
        } else {
            snprintf(buffer, buffer_size, "%s", line); // Detached HEAD, copy the commit hash
        }
    } else {
        snprintf(buffer, buffer_size, "EMPTY_HEAD_FILE");
    }

    fclose(head_file);
    snprintf(cached_branch_name, sizeof(cached_branch_name), "%s", buffer);
}

static void parse_hunk_header(const char* line, GitHunk* hunk) {
    const char *p = line;
    // Skip "@@ -"
    p += 4;
    hunk->old_start = strtol(p, (char**)&p, 10);
    if (*p == ',') {
        p++;
        hunk->old_count = strtol(p, (char**)&p, 10);
    } else {
        hunk->old_count = 1;
    }

    // Skip " +"
    while (*p == ' ') p++;
    p++;
    hunk->new_start = strtol(p, (char**)&p, 10);
    if (*p == ',') {
        p++;
        hunk->new_count = strtol(p, (char**)&p, 10);
    } else {
        hunk->new_count = 1;
    }
}

void git_update_diff(Buffer *buffer) {
    if (buffer->hunks) {
        free(buffer->hunks);
        buffer->hunks = NULL;
    }
    buffer->hunk_count = 0;

    if (!buffer->file_name) {
        return;
    }

    char head_path[] = "/tmp/arc_git_head_XXXXXX";
    int head_fd = mkstemp(head_path);
    if (head_fd == -1) return;

    char command[1024];
    snprintf(command, sizeof(command), "git show HEAD:./%s > %s 2>/dev/null", buffer->file_name, head_path);
    int ret = system(command);
    // if file is not in git, system will return non-zero
    if (ret != 0) {
        // check if the file exists on disk
        struct stat st;
        if (stat(buffer->file_name, &st) == 0) {
            // file exists, but not in git. diff against empty file
            FILE* f = fopen(head_path, "w");
            if (f) fclose(f);
        } else {
            close(head_fd);
            unlink(head_path);
            return;
        }
    }
    close(head_fd);

    char buffer_path[] = "/tmp/arc_git_buffer_XXXXXX";
    int buffer_fd = mkstemp(buffer_path);
    if (buffer_fd == -1) {
        unlink(head_path);
        return;
    }

    char* content = buffer_get_content(buffer);
    if (content) {
        write(buffer_fd, content, strlen(content));
        free(content);
    }
    close(buffer_fd);

    snprintf(command, sizeof(command), "git diff --no-index --no-color -U0 %s %s", head_path, buffer_path);
    FILE* fp = popen(command, "r");
    if (!fp) {
        unlink(head_path);
        unlink(buffer_path);
        return;
    }

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "@@", 2) == 0) {
            buffer->hunk_count++;
            buffer->hunks = realloc(buffer->hunks, sizeof(GitHunk) * buffer->hunk_count);
            parse_hunk_header(line, &buffer->hunks[buffer->hunk_count - 1]);
        }
    }

    pclose(fp);
    unlink(head_path);
    unlink(buffer_path);
}

GitLineStatus git_get_line_status(const Buffer *buffer, int line_num, int* deleted_lines) {
    *deleted_lines = 0;
    if (!buffer || !buffer->hunks) {
        return GIT_LINE_UNMODIFIED;
    }

    int ln = line_num + 1; // convert to 1-based

    for (int i = 0; i < buffer->hunk_count; i++) {
        GitHunk* h = &buffer->hunks[i];
        if (ln >= h->new_start && ln < h->new_start + h->new_count) {
            if (h->old_count == 0) {
                return GIT_LINE_ADDED;
            }
            return GIT_LINE_MODIFIED;
        }
    }

    // It's an unmodified line. Check for deletions after it.
    int offset = 0;
    for (int i = 0; i < buffer->hunk_count; i++) {
        GitHunk* h = &buffer->hunks[i];
        if (ln < h->new_start) {
            break;
        }
        offset += h->old_count - h->new_count;
    }
    int old_ln = ln + offset;

    for (int i = 0; i < buffer->hunk_count; i++) {
        GitHunk* h = &buffer->hunks[i];
        if (h->new_count == 0 && h->old_start == old_ln - 1) {
            *deleted_lines = h->old_count;
            break;
        }
    }

    return GIT_LINE_UNMODIFIED;
}
