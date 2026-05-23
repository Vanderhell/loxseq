#ifndef LOXSEQ_TEST_UTIL_H
#define LOXSEQ_TEST_UTIL_H

#include <stdio.h>

static int g_test_failures = 0;

#define TEST_FAIL(msg)                                                        \
    do {                                                                      \
        ++g_test_failures;                                                    \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, (msg));            \
    } while (0)

#define TEST_ASSERT(expr)                                                     \
    do {                                                                      \
        if (!(expr)) {                                                        \
            ++g_test_failures;                                                \
            fprintf(stderr, "%s:%d: assertion failed: %s\n",                  \
                    __FILE__, __LINE__, #expr);                               \
        }                                                                     \
    } while (0)

#define TEST_ASSERT_EQ_INT(a, b)                                              \
    do {                                                                      \
        int _a = (int)(a);                                                    \
        int _b = (int)(b);                                                    \
        if (_a != _b) {                                                       \
            ++g_test_failures;                                                \
            fprintf(stderr, "%s:%d: expected %s == %s (%d != %d)\n",          \
                    __FILE__, __LINE__, #a, #b, _a, _b);                      \
        }                                                                     \
    } while (0)

#define TEST_ASSERT_EQ_U32(a, b)                                              \
    do {                                                                      \
        unsigned long _a = (unsigned long)(a);                                \
        unsigned long _b = (unsigned long)(b);                                \
        if (_a != _b) {                                                       \
            ++g_test_failures;                                                \
            fprintf(stderr, "%s:%d: expected %s == %s (%lu != %lu)\n",        \
                    __FILE__, __LINE__, #a, #b, _a, _b);                      \
        }                                                                     \
    } while (0)

#endif /* LOXSEQ_TEST_UTIL_H */

