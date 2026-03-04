/*
 * Unit tests for TMR (Triple Modular Redundancy) median voter
 *
 * Includes tmr.c directly for white-box access to the voter function.
 */

/* Mock layer — must come before application includes */
#include "mock_freertos.h"
#include "mock_hal.h"
#include "mock_deps.h"

/* flight_sm mock — defined in mock_flight_sm.c */
#include "mock_flight_sm.h"

#include "test_harness.h"

/* Include tmr.c directly for white-box access to voter internals.
 * ekf_altitude functions are linked from app_ekf.o */
#include "tmr.c"

/* ---- Tests ---- */

static int test_median_all_equal(void)
{
    TEST_ASSERT_FLOAT_EQ(tmr_vote_median3(5.0f, 5.0f, 5.0f), 5.0f, 0.001f);
    return 0;
}

static int test_median_ascending(void)
{
    TEST_ASSERT_FLOAT_EQ(tmr_vote_median3(1.0f, 2.0f, 3.0f), 2.0f, 0.001f);
    return 0;
}

static int test_median_descending(void)
{
    TEST_ASSERT_FLOAT_EQ(tmr_vote_median3(3.0f, 2.0f, 1.0f), 2.0f, 0.001f);
    return 0;
}

static int test_median_first_outlier(void)
{
    TEST_ASSERT_FLOAT_EQ(tmr_vote_median3(100.0f, 5.0f, 5.5f), 5.5f, 0.001f);
    return 0;
}

static int test_median_second_outlier(void)
{
    TEST_ASSERT_FLOAT_EQ(tmr_vote_median3(5.0f, -999.0f, 5.5f), 5.0f, 0.001f);
    return 0;
}

static int test_median_third_outlier(void)
{
    TEST_ASSERT_FLOAT_EQ(tmr_vote_median3(10.0f, 10.5f, 999.0f), 10.5f, 0.001f);
    return 0;
}

static int test_median_negative_values(void)
{
    TEST_ASSERT_FLOAT_EQ(tmr_vote_median3(-3.0f, -1.0f, -2.0f), -2.0f, 0.001f);
    return 0;
}

static int test_median_mixed_sign(void)
{
    TEST_ASSERT_FLOAT_EQ(tmr_vote_median3(-10.0f, 0.0f, 10.0f), 0.0f, 0.001f);
    return 0;
}

static int test_median_two_equal(void)
{
    TEST_ASSERT_FLOAT_EQ(tmr_vote_median3(7.0f, 7.0f, 3.0f), 7.0f, 0.001f);
    TEST_ASSERT_FLOAT_EQ(tmr_vote_median3(3.0f, 7.0f, 7.0f), 7.0f, 0.001f);
    TEST_ASSERT_FLOAT_EQ(tmr_vote_median3(7.0f, 3.0f, 7.0f), 7.0f, 0.001f);
    return 0;
}

static int test_median_all_permutations(void)
{
    float vals[6][3] = {
        {1, 2, 3}, {1, 3, 2}, {2, 1, 3},
        {2, 3, 1}, {3, 1, 2}, {3, 2, 1}
    };
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_FLOAT_EQ(
            tmr_vote_median3(vals[i][0], vals[i][1], vals[i][2]),
            2.0f, 0.001f);
    }
    return 0;
}

/* ---- Suite runner ---- */
void run_tmr_tests(void)
{
    TEST_SUITE("TMR Voter Tests");
    RUN_TEST(test_median_all_equal);
    RUN_TEST(test_median_ascending);
    RUN_TEST(test_median_descending);
    RUN_TEST(test_median_first_outlier);
    RUN_TEST(test_median_second_outlier);
    RUN_TEST(test_median_third_outlier);
    RUN_TEST(test_median_negative_values);
    RUN_TEST(test_median_mixed_sign);
    RUN_TEST(test_median_two_equal);
    RUN_TEST(test_median_all_permutations);
}
