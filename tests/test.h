/* Tiny assertion helper shared by the host tests. */
#pragma once
#include <stdio.h>

static int test_failures;
static int test_checks;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++test_checks;                                                       \
        if (!(cond)) {                                                       \
            ++test_failures;                                                 \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                    \
    } while (0)

#define TEST_REPORT(name)                                                    \
    (printf("%-24s %d/%d checks passed\n", name, test_checks - test_failures, \
            test_checks), test_failures != 0)
