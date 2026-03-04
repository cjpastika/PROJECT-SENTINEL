/*
 * Unit tests for CCSDS CRC-16/CCITT implementation
 * Includes ccsds.c directly for access to ccsds_crc16().
 */

#include "mock_freertos.h"
#include "mock_hal.h"
#include "mock_deps.h"

#include "test_harness.h"

/* CCSDS implementation linked from app_ccsds.o */
#include "ccsds.h"

/* ---- Known CRC-16/CCITT test vectors ---- */

static int test_crc_empty(void)
{
    uint16_t crc = ccsds_crc16(NULL, 0);
    TEST_ASSERT_EQ(crc, 0xFFFF);
    return 0;
}

static int test_crc_single_zero(void)
{
    uint8_t data[] = { 0x00 };
    uint16_t crc = ccsds_crc16(data, 1);
    TEST_ASSERT_EQ(crc, 0xE1F0);
    return 0;
}

static int test_crc_known_vector_123456789(void)
{
    /* Classic test string "123456789" -> CRC-CCITT = 0x29B1 */
    uint8_t data[] = { '1','2','3','4','5','6','7','8','9' };
    uint16_t crc = ccsds_crc16(data, 9);
    TEST_ASSERT_EQ(crc, 0x29B1);
    return 0;
}

static int test_crc_all_ones(void)
{
    uint8_t data[] = { 0xFF, 0xFF, 0xFF, 0xFF };
    uint16_t crc = ccsds_crc16(data, 4);
    uint16_t crc2 = ccsds_crc16(data, 4);
    TEST_ASSERT_EQ(crc, crc2);
    TEST_ASSERT(crc != 0xFFFF);
    return 0;
}

static int test_crc_deterministic(void)
{
    uint8_t data[] = { 0x41, 0x42, 0x43, 0x44, 0x45, 0x46 };
    uint16_t crc = ccsds_crc16(data, 6);
    uint16_t crc2 = ccsds_crc16(data, 6);
    TEST_ASSERT_EQ(crc, crc2);
    return 0;
}

static int test_crc_single_bit_sensitivity(void)
{
    uint8_t data1[] = { 0x00, 0x00, 0x00, 0x00 };
    uint8_t data2[] = { 0x00, 0x00, 0x00, 0x01 };
    uint16_t crc1 = ccsds_crc16(data1, 4);
    uint16_t crc2 = ccsds_crc16(data2, 4);
    TEST_ASSERT(crc1 != crc2);
    return 0;
}

static int test_crc_byte_order_sensitivity(void)
{
    uint8_t data1[] = { 0x01, 0x02 };
    uint8_t data2[] = { 0x02, 0x01 };
    uint16_t crc1 = ccsds_crc16(data1, 2);
    uint16_t crc2 = ccsds_crc16(data2, 2);
    TEST_ASSERT(crc1 != crc2);
    return 0;
}

static int test_crc_long_payload(void)
{
    uint8_t data[128];
    for (int i = 0; i < 128; i++) data[i] = (uint8_t)i;
    uint16_t crc = ccsds_crc16(data, 128);
    uint16_t crc2 = ccsds_crc16(data, 128);
    TEST_ASSERT_EQ(crc, crc2);
    TEST_ASSERT(crc != 0);
    return 0;
}

/* ---- Suite runner ---- */
void run_crc_tests(void)
{
    TEST_SUITE("CRC-16/CCITT Tests");
    RUN_TEST(test_crc_empty);
    RUN_TEST(test_crc_single_zero);
    RUN_TEST(test_crc_known_vector_123456789);
    RUN_TEST(test_crc_all_ones);
    RUN_TEST(test_crc_deterministic);
    RUN_TEST(test_crc_single_bit_sensitivity);
    RUN_TEST(test_crc_byte_order_sensitivity);
    RUN_TEST(test_crc_long_payload);
}
