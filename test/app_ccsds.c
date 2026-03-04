/* Compile ccsds.c with mock dependencies for host testing */
#include "mock_freertos.h"
#include "mock_hal.h"
#define MOCK_SKIP_CCSDS
#include "mock_deps.h"
#include "ccsds.c"
