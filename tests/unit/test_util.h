#ifndef ELFR_TEST_UTIL_H
#define ELFR_TEST_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Deliberately not assert()-based: NDEBUG (set by e.g. RelWithDebInfo)
 * would silently compile assert() checks away. */
#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);   \
            exit(1);                                                                   \
        }                                                                              \
    } while (0)

#define CHECK_STREQ(a, b)                                                                          \
    do {                                                                                           \
        const char *_a = (a), *_b = (b);                                                           \
        if (strcmp(_a, _b) != 0) {                                                                 \
            fprintf(stderr, "%s:%d: CHECK_STREQ failed: '%s' != '%s'\n", __FILE__, __LINE__, _a, _b); \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#endif /* ELFR_TEST_UTIL_H */
