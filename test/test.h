#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern int g_test_failures;

#define ASSERT(test_name, condition) \
    if (!(condition)) { \
        fprintf(stderr, "[%s] Assertion failed: %s, file %s, line %d\n", test_name, #condition, __FILE__, __LINE__); \
        g_test_failures++; \
    }

#define ASSERT_STRING_EQUAL(test_name, s1, s2) \
    if (strcmp(s1, s2) != 0) { \
        fprintf(stderr, "[%s] Assertion failed: strings are not equal.\n", test_name); \
        fprintf(stderr, "  Expected: \"%s\"\n", s2); \
        fprintf(stderr, "  Actual:   \"%s\"\n", s1); \
        g_test_failures++; \
    }

#define ASSERT_EQUAL(test_name, a, b) \
    if ((a) != (b)) { \
        fprintf(stderr, "[%s] Assertion failed: values are not equal.\n", test_name); \
        fprintf(stderr, "  Expected: %d\n", (b)); \
        fprintf(stderr, "  Actual:   %d\n", (a)); \
        g_test_failures++; \
    }

void test_helper(const char* test_name, const char* initial_content, int start_y, int start_x, const char* commands, const char* expected_content);
void paste_test_helper(const char* test_name, const char* initial_content, int start_y, int start_x, const char* commands, const char* paste_string, const char* expected_content);
void test_motion_helper(const char* test_name, const char* initial_content, int start_y, int start_x, const char* commands, int end_y, int end_x);
void test_visual_motion_helper(const char* test_name, const char* initial_content, int start_y, int start_x, const char* commands, int sel_start_y, int sel_start_x, int end_y, int end_x);
void test_visual_action_helper(const char* test_name, const char* initial_content, int start_y, int start_x, const char* commands, const char* expected_content);

#endif // TEST_H
