/*
 * Unit tests for EKF altitude estimator math
 */

#include "mock_freertos.h"
#include "mock_hal.h"
#include "mock_deps.h"

/* flight_sm mock — defined in mock_flight_sm.c */
#include "mock_flight_sm.h"

#include "test_harness.h"

/* EKF implementation compiled in app_ekf.c */
#include "ekf_altitude.h"

/* ---- Helper: create and init an EKF ---- */
static void setup_ekf(ekf_state_t *ekf)
{
    ekf_init(ekf);
    /* First update initializes timestamp */
    mock_set_tick(0);
    ekf_update(ekf, 1000, 0);  /* 1g on pad */
}

/* ---- Tests ---- */

static int test_ekf_init_zeros(void)
{
    ekf_state_t ekf;
    ekf_init(&ekf);
    TEST_ASSERT_FLOAT_EQ(ekf.x[0], 0.0f, 0.001f);  /* altitude */
    TEST_ASSERT_FLOAT_EQ(ekf.x[1], 0.0f, 0.001f);  /* velocity */
    TEST_ASSERT_EQ(ekf.initialized, 0);
    return 0;
}

static int test_ekf_first_update_initializes(void)
{
    ekf_state_t ekf;
    ekf_init(&ekf);
    ekf_update(&ekf, 1000, 100);
    TEST_ASSERT_EQ(ekf.initialized, 1);
    /* Should not have changed state on first call */
    TEST_ASSERT_FLOAT_EQ(ekf.x[0], 0.0f, 0.001f);
    TEST_ASSERT_FLOAT_EQ(ekf.x[1], 0.0f, 0.001f);
    return 0;
}

static int test_ekf_stationary_on_pad(void)
{
    /* 1g accel => gravity-compensated = 0 => should stay at 0 */
    flight_sm_set_mock_state(FLIGHT_IDLE);
    ekf_state_t ekf;
    setup_ekf(&ekf);

    for (int i = 1; i <= 50; i++) {
        ekf_update(&ekf, 1000, i * 20);  /* 50Hz, 1g */
    }

    TEST_ASSERT_FLOAT_EQ(ekf.x[0], 0.0f, 0.1f);    /* altitude ~0 */
    TEST_ASSERT_FLOAT_EQ(ekf.x[1], 0.0f, 0.1f);    /* velocity ~0 */
    return 0;
}

static int test_ekf_upward_acceleration(void)
{
    /* 2g accel => 1g net upward => positive velocity and altitude */
    flight_sm_set_mock_state(FLIGHT_BOOST);
    ekf_state_t ekf;
    setup_ekf(&ekf);

    for (int i = 1; i <= 50; i++) {
        ekf_update(&ekf, 2000, i * 20);  /* 2g for 1 second at 50Hz */
    }

    /* After 1s at 1g net: v = 9.81 m/s, alt = 0.5*9.81*1 = 4.9 m */
    TEST_ASSERT(ekf.x[0] > 3.0f);   /* altitude positive */
    TEST_ASSERT(ekf.x[1] > 7.0f);   /* velocity positive */
    return 0;
}

static int test_ekf_altitude_clamp_idle(void)
{
    /* In IDLE state, altitude should be clamped to >= 0 */
    flight_sm_set_mock_state(FLIGHT_IDLE);
    ekf_state_t ekf;
    setup_ekf(&ekf);

    /* Apply slight negative accel (below 1g) */
    for (int i = 1; i <= 20; i++) {
        ekf_update(&ekf, 900, i * 20);  /* -0.1g net */
    }

    /* Should be clamped to 0, not negative */
    TEST_ASSERT(ekf.x[0] >= 0.0f);
    return 0;
}

static int test_ekf_velocity_zero_landed(void)
{
    /* In LANDED state, velocity should be forced to 0 */
    flight_sm_set_mock_state(FLIGHT_LANDED);
    ekf_state_t ekf;
    setup_ekf(&ekf);

    ekf_update(&ekf, 1100, 20);  /* slight upward reading */
    TEST_ASSERT_FLOAT_EQ(ekf.x[1], 0.0f, 0.001f);
    return 0;
}

static int test_ekf_skip_large_dt(void)
{
    /* dt > 1000ms should be skipped */
    flight_sm_set_mock_state(FLIGHT_BOOST);
    ekf_state_t ekf;
    setup_ekf(&ekf);

    ekf_update(&ekf, 5000, 20);    /* normal update */
    float alt_before = ekf.x[0];

    ekf_update(&ekf, 5000, 2000);  /* dt=1980ms > 1000, should skip */
    TEST_ASSERT_FLOAT_EQ(ekf.x[0], alt_before, 0.001f);
    return 0;
}

static int test_ekf_covariance_grows(void)
{
    /* Covariance should grow over time with process noise */
    flight_sm_set_mock_state(FLIGHT_BOOST);
    ekf_state_t ekf;
    setup_ekf(&ekf);

    float P00_initial = ekf.P[0];

    for (int i = 1; i <= 50; i++) {
        ekf_update(&ekf, 1000, i * 20);
    }

    TEST_ASSERT(ekf.P[0] > P00_initial);
    TEST_ASSERT(ekf.P[3] > 0.0f);
    return 0;
}

static int test_ekf_zero_dt_skipped(void)
{
    flight_sm_set_mock_state(FLIGHT_BOOST);
    ekf_state_t ekf;
    setup_ekf(&ekf);

    ekf_update(&ekf, 2000, 20);
    float alt_after = ekf.x[0];

    /* Same timestamp => dt=0 => should skip */
    ekf_update(&ekf, 5000, 20);
    TEST_ASSERT_FLOAT_EQ(ekf.x[0], alt_after, 0.001f);
    return 0;
}

/* ---- Suite runner ---- */
void run_ekf_tests(void)
{
    TEST_SUITE("EKF Altitude Tests");
    RUN_TEST(test_ekf_init_zeros);
    RUN_TEST(test_ekf_first_update_initializes);
    RUN_TEST(test_ekf_stationary_on_pad);
    RUN_TEST(test_ekf_upward_acceleration);
    RUN_TEST(test_ekf_altitude_clamp_idle);
    RUN_TEST(test_ekf_velocity_zero_landed);
    RUN_TEST(test_ekf_skip_large_dt);
    RUN_TEST(test_ekf_covariance_grows);
    RUN_TEST(test_ekf_zero_dt_skipped);
}
