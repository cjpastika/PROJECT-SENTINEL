/*
 * Unit tests for the flight state machine transition logic.
 *
 * Since evaluate_state() is static, we include flight_sm.c directly
 * for white-box testing of the internal logic.
 */

#include "mock_freertos.h"
#include "mock_hal.h"
#include "mock_deps.h"

#include "test_harness.h"

/* Make public FSM functions static to avoid clashing with mock_flight_sm.o */
#define flight_sm_get_state  fsm_test_get_state
#define flight_sm_arm        fsm_test_arm
#define flight_sm_init       fsm_test_init

/* Include the implementation directly for white-box access */
#include "flight_sm.c"

/* Restore original names for test use */
#undef flight_sm_get_state
#undef flight_sm_arm
#undef flight_sm_init

/* ---- Helpers ---- */

static void reset_fsm(void)
{
    current_state = FLIGHT_IDLE;
    prev_state = FLIGHT_IDLE;
    confirm_count = 0;
    last_accel_z = 0;
    arm_requested = 0;
    mock_fault_reset();
}

/* ---- Tests ---- */

static int test_fsm_initial_state(void)
{
    reset_fsm();
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_IDLE);
    return 0;
}

static int test_fsm_idle_stays_without_arm(void)
{
    reset_fsm();
    /* Apply high accel in IDLE — should NOT transition */
    for (int i = 0; i < 10; i++) {
        evaluate_state(5000);
    }
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_IDLE);
    return 0;
}

static int test_fsm_arm_transitions_to_armed(void)
{
    reset_fsm();
    fsm_test_arm();
    evaluate_state(1000);  /* normal 1g on pad */
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_ARMED);
    return 0;
}

static int test_fsm_armed_to_boost_debounced(void)
{
    reset_fsm();
    fsm_test_arm();
    evaluate_state(1000);  /* -> ARMED */

    /* 2 samples above threshold — not enough (need 3) */
    evaluate_state(3000);
    evaluate_state(3000);
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_ARMED);

    /* 3rd sample triggers transition */
    evaluate_state(3000);
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_BOOST);
    return 0;
}

static int test_fsm_armed_debounce_resets(void)
{
    reset_fsm();
    fsm_test_arm();
    evaluate_state(1000);  /* -> ARMED */

    /* 2 high, then 1 low — debounce resets */
    evaluate_state(3000);
    evaluate_state(3000);
    evaluate_state(500);   /* below threshold — resets counter */

    /* Need 3 more consecutive */
    evaluate_state(3000);
    evaluate_state(3000);
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_ARMED);
    evaluate_state(3000);
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_BOOST);
    return 0;
}

static int test_fsm_boost_to_coast(void)
{
    reset_fsm();
    fsm_test_arm();
    evaluate_state(1000);  /* -> ARMED */

    /* Force to BOOST */
    for (int i = 0; i < 3; i++) evaluate_state(3000);
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_BOOST);

    /* 5 low-accel samples -> COAST (COAST_CONFIRM = 5) */
    for (int i = 0; i < 4; i++) evaluate_state(100);
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_BOOST);
    evaluate_state(100);
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_COAST);
    return 0;
}

static int test_fsm_coast_to_descent(void)
{
    reset_fsm();
    current_state = FLIGHT_COAST;  /* direct set for brevity */
    confirm_count = 0;

    /* 3 negative-accel samples -> DESCENT (DESC_CONFIRM = 3) */
    evaluate_state(-500);
    evaluate_state(-500);
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_COAST);
    evaluate_state(-500);
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_DESCENT);
    return 0;
}

static int test_fsm_descent_to_landed(void)
{
    reset_fsm();
    current_state = FLIGHT_DESCENT;
    confirm_count = 0;

    /* 10 samples near 1g -> LANDED (LAND_CONFIRM = 10) */
    for (int i = 0; i < 9; i++) evaluate_state(1000);
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_DESCENT);
    evaluate_state(1000);
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_LANDED);
    return 0;
}

static int test_fsm_landed_is_terminal(void)
{
    reset_fsm();
    current_state = FLIGHT_LANDED;

    /* Any accel should not change state */
    evaluate_state(5000);
    evaluate_state(-5000);
    evaluate_state(1000);
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_LANDED);
    return 0;
}

static int test_fsm_full_flight_profile(void)
{
    /* Simulate a complete flight through all transitions */
    reset_fsm();

    /* IDLE -> ARMED */
    fsm_test_arm();
    evaluate_state(1000);
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_ARMED);

    /* ARMED -> BOOST (3 samples > 2g) */
    evaluate_state(3000);
    evaluate_state(3000);
    evaluate_state(3000);
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_BOOST);

    /* BOOST -> COAST (5 samples < 200 mg) */
    for (int i = 0; i < 5; i++) evaluate_state(50);
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_COAST);

    /* COAST -> DESCENT (3 samples < -300 mg) */
    for (int i = 0; i < 3; i++) evaluate_state(-400);
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_DESCENT);

    /* DESCENT -> LANDED (10 samples near 1g) */
    for (int i = 0; i < 10; i++) evaluate_state(1000);
    TEST_ASSERT_EQ(fsm_test_get_state(), FLIGHT_LANDED);

    return 0;
}

static int test_fsm_fault_on_transition(void)
{
    reset_fsm();
    fsm_test_arm();
    evaluate_state(1000);  /* -> ARMED, should record fault */
    TEST_ASSERT(mock_fault_count > 0);
    TEST_ASSERT_EQ(mock_last_fault_src, FAULT_SRC_FSM);
    TEST_ASSERT_EQ(mock_last_fault_detail, FAULT_DETAIL_FSM_TRANSITION);
    return 0;
}

/* ---- Suite runner ---- */
void run_fsm_tests(void)
{
    TEST_SUITE("Flight State Machine Tests");
    RUN_TEST(test_fsm_initial_state);
    RUN_TEST(test_fsm_idle_stays_without_arm);
    RUN_TEST(test_fsm_arm_transitions_to_armed);
    RUN_TEST(test_fsm_armed_to_boost_debounced);
    RUN_TEST(test_fsm_armed_debounce_resets);
    RUN_TEST(test_fsm_boost_to_coast);
    RUN_TEST(test_fsm_coast_to_descent);
    RUN_TEST(test_fsm_descent_to_landed);
    RUN_TEST(test_fsm_landed_is_terminal);
    RUN_TEST(test_fsm_full_flight_profile);
    RUN_TEST(test_fsm_fault_on_transition);
}
