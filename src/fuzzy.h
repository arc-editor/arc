#ifndef FUZZY_H
#define FUZZY_H

int fuzzy_search(const char **input, int input_len, const char *query, int *indices);
int fuzzy_match(const char *input, const char *query, int *matches, int max_matches);

#endif
