#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "git_diff.h"
#include "log.h"
#include "git.h"

// A simple structure to hold a line of text.
typedef struct {
    const char* text;
    size_t length;
} Line;

// A helper function to split a string into an array of lines.
static Line* split_into_lines(const char* content, int* line_count) {
    int count = 0;
    const char* p = content;
    while (*p) {
        if (*p == '\n') {
            count++;
        }
        p++;
    }
    // If the file doesn't end with a newline, we still have one more line.
    if (p > content && *(p - 1) != '\n') {
        count++;
    }

    Line* lines = malloc(sizeof(Line) * count);
    if (!lines) {
        log_error("Failed to allocate memory for lines");
        *line_count = 0;
        return NULL;
    }

    p = content;
    int i = 0;
    while (i < count) {
        const char* start = p;
        const char* end = strchr(p, '\n');
        if (end) {
            lines[i].text = start;
            lines[i].length = end - start;
            p = end + 1;
        } else {
            lines[i].text = start;
            lines[i].length = strlen(start);
            p = start + lines[i].length;
        }
        i++;
    }

    *line_count = count;
    return lines;
}

// A simple implementation of the Longest Common Subsequence (LCS) algorithm.
void diff_lines(const char* old_content, const char* new_content, Buffer* buffer) {
    log_info("diff_lines start");
    log_info("old_content: %s", old_content);
    log_info("new_content: %s", new_content);
    int old_line_count = 0;
    Line* old_lines = split_into_lines(old_content, &old_line_count);
    log_info("old_line_count: %d", old_line_count);

    int new_line_count = 0;
    Line* new_lines = split_into_lines(new_content, &new_line_count);
    log_info("new_line_count: %d", new_line_count);

    // Create a 2D array to store the lengths of the LCS.
    int** lcs = malloc(sizeof(int*) * (old_line_count + 1));
    for (int i = 0; i <= old_line_count; i++) {
        lcs[i] = malloc(sizeof(int) * (new_line_count + 1));
    }

    // Fill the LCS table.
    for (int i = 0; i <= old_line_count; i++) {
        for (int j = 0; j <= new_line_count; j++) {
            if (i == 0 || j == 0) {
                lcs[i][j] = 0;
            } else if (old_lines[i - 1].length == new_lines[j - 1].length &&
                       strncmp(old_lines[i - 1].text, new_lines[j - 1].text, old_lines[i - 1].length) == 0) {
                lcs[i][j] = lcs[i - 1][j - 1] + 1;
            } else {
                lcs[i][j] = (lcs[i - 1][j] > lcs[i][j - 1]) ? lcs[i - 1][j] : lcs[i][j - 1];
            }
        }
    }

    // Backtrack to find the diff.
    // This is a simplified version that just identifies the changes and doesn't
    // try to merge them into hunks.
    int i = old_line_count;
    int j = new_line_count;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && old_lines[i - 1].length == new_lines[j - 1].length &&
            strncmp(old_lines[i - 1].text, new_lines[j - 1].text, old_lines[i - 1].length) == 0) {
            // Lines are the same, move to the previous lines.
            i--;
            j--;
        } else if (j > 0 && (i == 0 || lcs[i][j - 1] >= lcs[i - 1][j])) {
            // Line was added.
            j--;
            buffer->hunk_count++;
            buffer->hunks = realloc(buffer->hunks, sizeof(GitHunk) * buffer->hunk_count);
            GitHunk* hunk = &buffer->hunks[buffer->hunk_count - 1];
            hunk->old_start = i;
            hunk->old_count = 0;
            hunk->new_start = j + 1;
            hunk->new_count = 1;
        } else if (i > 0 && (j == 0 || lcs[i][j - 1] < lcs[i - 1][j])) {
            // Line was deleted.
            i--;
            buffer->hunk_count++;
            buffer->hunks = realloc(buffer->hunks, sizeof(GitHunk) * buffer->hunk_count);
            GitHunk* hunk = &buffer->hunks[buffer->hunk_count - 1];
            hunk->old_start = i + 1;
            hunk->old_count = 1;
            hunk->new_start = j;
            hunk->new_count = 0;
        }
    }

    // The hunks are generated in reverse order, so we need to reverse them.
    for (int k = 0; k < buffer->hunk_count / 2; k++) {
        GitHunk temp = buffer->hunks[k];
        buffer->hunks[k] = buffer->hunks[buffer->hunk_count - k - 1];
        buffer->hunks[buffer->hunk_count - k - 1] = temp;
    }

    // Merge adjacent hunks.
    if (buffer->hunk_count > 1) {
        int i = 0;
        while (i < buffer->hunk_count - 1) {
            GitHunk* current = &buffer->hunks[i];
            GitHunk* next = &buffer->hunks[i + 1];
            if (current->old_start + current->old_count == next->old_start &&
                current->new_start + current->new_count == next->new_start) {
                // Merge next into current.
                current->old_count += next->old_count;
                current->new_count += next->new_count;
                // Remove next.
                memmove(next, next + 1, (buffer->hunk_count - i - 2) * sizeof(GitHunk));
                buffer->hunk_count--;
            } else {
                i++;
            }
        }
    }

    // Log the hunks.
    for (int i = 0; i < buffer->hunk_count; i++) {
        GitHunk* hunk = &buffer->hunks[i];
        log_info("Hunk %d: old_start=%d, old_count=%d, new_start=%d, new_count=%d",
                 i, hunk->old_start, hunk->old_count, hunk->new_start, hunk->new_count);
    }

    // Free the allocated memory.
    for (int k = 0; k <= old_line_count; k++) {
        free(lcs[k]);
    }
    free(lcs);
    free(old_lines);
    free(new_lines);
    log_info("diff_lines end");
}
