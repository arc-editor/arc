#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ASSERT(condition) \
    if (!(condition)) { \
        fprintf(stderr, "Assertion failed: %s, file %s, line %d\n", #condition, __FILE__, __LINE__); \
        exit(1); \
    }

#define ASSERT_STRING_EQUAL(s1, s2) \
    if (strcmp(s1, s2) != 0) { \
        fprintf(stderr, "Assertion failed: strings are not equal.\n"); \
        fprintf(stderr, "  Expected: \"%s\"\n", s2); \
        fprintf(stderr, "  Actual:   \"%s\"\n", s1); \
        exit(1); \
    }

#endif // TEST_H
