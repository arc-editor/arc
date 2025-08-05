#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "log.h"

static char cached_branch_name[256] = "";
static long last_update_ms = 0;

long get_current_time_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000L + now.tv_nsec / 1000000L;
}

void git_current_branch(char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        log_error("git.git_current_branch: null buffer provided to git_current_branc");
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
