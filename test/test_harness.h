/*
 * Minimal test harness for PROJECT-SENTINEL unit tests
 *
 * Provides assert macros, test registration, and a runner.
 * Compiles with host GCC (not arm-none-eabi).
 */

#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- Counters (defined in test_main.c) ---- */
extern int tests_run;
extern int tests_passed;
extern int tests_failed;

/* ---- Assert macros ---- */
#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define TEST_ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("  FAIL: %s:%d: %s == %s (%d != %d)\n", \
               __FILE__, __LINE__, #a, #b, (int)(a), (int)(b)); \
        return 1; \
    } \
} while (0)

#define TEST_ASSERT_FLOAT_EQ(a, b, eps) do { \
    float _a = (a), _b = (b); \
    if (fabsf(_a - _b) > (eps)) { \
        printf("  FAIL: %s:%d: %s ~= %s (%.6f != %.6f, eps=%.6f)\n", \
               __FILE__, __LINE__, #a, #b, (double)_a, (double)_b, (double)(eps)); \
        return 1; \
    } \
} while (0)

/* ---- Test runner ---- */
#define RUN_TEST(fn) do { \
    printf("  %-50s", #fn); \
    tests_run++; \
    if (fn() == 0) { \
        printf("PASS\n"); \
        tests_passed++; \
    } else { \
        tests_failed++; \
    } \
} while (0)

#define TEST_SUITE(name) \
    printf("\n=== %s ===\n", name)

#define TEST_SUMMARY() do { \
    printf("\n--------------------------------------------\n"); \
    printf("Results: %d passed, %d failed, %d total\n", \
           tests_passed, tests_failed, tests_run); \
    printf("--------------------------------------------\n"); \
    return tests_failed > 0 ? 1 : 0; \
} while (0)

#endif /* TEST_HARNESS_H */
