/*
 * PROJECT-SENTINEL Unit Test Runner
 *
 * Compiles with host GCC and runs all test suites.
 * Usage: make -C test && ./test/run_tests
 */

#include <stdio.h>

/* ---- Test suite declarations ---- */
extern void run_tmr_tests(void);
extern void run_crc_tests(void);
extern void run_ekf_tests(void);
extern void run_fsm_tests(void);

/* Shared counters from test_harness.h (defined in each TU but we
 * need a single set — redeclare here as the real storage) */
int tests_run = 0;
int tests_passed = 0;
int tests_failed = 0;

int main(void)
{
    printf("PROJECT-SENTINEL Unit Tests\n");
    printf("============================================\n");

    run_tmr_tests();
    run_crc_tests();
    run_ekf_tests();
    run_fsm_tests();

    printf("\n============================================\n");
    printf("Results: %d passed, %d failed, %d total\n",
           tests_passed, tests_failed, tests_run);
    printf("============================================\n");

    return tests_failed > 0 ? 1 : 0;
}
