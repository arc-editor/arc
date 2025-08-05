#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include "log.h"

#define MAX_INPUT_LEN 1024
#define MAX_QUERY_LEN 256

static const char **global_input;

static int calculate_fuzzy_score(const char *input, const char *query, int *matches) {
    int input_len = strlen(input);
    int query_len = strlen(query);

    if (query_len == 0) return 1;
    if (input_len == 0 || input_len > MAX_INPUT_LEN || query_len > MAX_QUERY_LEN) return 0;

    int score[MAX_QUERY_LEN][MAX_INPUT_LEN];
    int path[MAX_QUERY_LEN][MAX_INPUT_LEN];

    for (int i = 0; i < query_len; i++) {
        for (int j = 0; j < input_len; j++) {
            score[i][j] = 0;
            path[i][j] = -1;
        }
    }

    for (int i = 0; i < query_len; i++) {
        for (int j = 0; j < input_len; j++) {
            if (tolower(input[j]) == tolower(query[i])) {
                int s = 1;
                int p = -1;
                if (i > 0) {
                    s = -1;
                    for (int k = 0; k < j; k++) {
                        if (score[i-1][k] > 0) {
                            int current_score = score[i-1][k] + 1;
                            if (k + 1 == j) { // Contiguous bonus
                                current_score += 2;
                            }
                            if (s < current_score) {
                                s = current_score;
                                p = k;
                            }
                        }
                    }
                }
                score[i][j] = s;
                path[i][j] = p;
            }
        }
    }

    int best_score = 0;
    int last_index = -1;
    for (int j = 0; j < input_len; j++) {
        if (score[query_len - 1][j] > best_score) {
            best_score = score[query_len - 1][j];
            last_index = j;
        }
    }

    if (last_index != -1 && matches) {
        int match_count = 0;
        int current_index = last_index;
        for (int i = query_len - 1; i >= 0; i--) {
            matches[match_count++] = current_index;
            current_index = path[i][current_index];
        }
        // Reverse matches
        for (int i = 0; i < match_count / 2; i++) {
            int temp = matches[i];
            matches[i] = matches[match_count - 1 - i];
            matches[match_count - 1 - i] = temp;
        }
    }

    return best_score;
}


typedef struct {
    int index;
    int score;
} SearchResult;

static int compare_results(const void *a, const void *b) {
    SearchResult *resultA = (SearchResult*)a;
    SearchResult *resultB = (SearchResult*)b;
    if (resultA->score == resultB->score) {
        return strlen(global_input[resultA->index]) - strlen(global_input[resultB->index]);
    }
    return resultB->score - resultA->score;
}

int fuzzy_search(const char **input, int input_len, const char *query, int *indices) {
    if (input_len == 0) return 0;
    global_input = input;
    SearchResult *results = malloc(input_len * sizeof(SearchResult));
    if (!results) {
        log_error("fuzzy.fuzzy_search: failed to allocate memory for search results");
        return 0;
    }

    for (int i = 0; i < input_len; i++) {
        results[i].index = i;
        results[i].score = calculate_fuzzy_score(input[i], query, NULL);
    }
    qsort(results, input_len, sizeof(SearchResult), compare_results);
    int count = 0;
    for (int i = 0; i < input_len && results[i].score > 0; i++) {
        indices[count++] = results[i].index;
    }
    free(results);
    return count;
}

int fuzzy_match(const char *input, const char *query, int *matches, int max_matches) {
    (void)max_matches; // The new algorithm finds all matches for the best score
    return calculate_fuzzy_score(input, query, matches) > 0 ? strlen(query) : 0;
}
