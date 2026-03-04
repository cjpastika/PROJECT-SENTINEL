/*
 * Shared mock for flight_sm_get_state() and flight_sm_arm().
 * Used by test_tmr and test_ekf (test_fsm includes flight_sm.c directly).
 *
 * Weak symbols so test_fsm.c can override with the real implementation.
 */
#include "flight_sm.h"

static flight_state_t mock_state = FLIGHT_IDLE;

__attribute__((weak))
flight_state_t flight_sm_get_state(void)
{
    return mock_state;
}

void flight_sm_set_mock_state(flight_state_t s)
{
    mock_state = s;
}

__attribute__((weak))
void flight_sm_arm(void) {}

__attribute__((weak))
void flight_sm_init(void) {}
