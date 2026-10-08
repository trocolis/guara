#include <string.h>
#include <unity.h>

#include "crc32.h"

void setUp(void) {}
void tearDown(void) {}

static void test_crc32_check_vector(void)
{
    /* Valor de verificação clássico do CRC-32/IEEE. */
    const char *text = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, guara_crc32(0, text, strlen(text)));
}

static void test_crc32_empty(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, guara_crc32(0, "", 0));
}

static void test_crc32_incremental(void)
{
    const char *text = "123456789";
    uint32_t crc = guara_crc32(0, text, 4);
    crc = guara_crc32(crc, text + 4, 5);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc32_check_vector);
    RUN_TEST(test_crc32_empty);
    RUN_TEST(test_crc32_incremental);
    return UNITY_END();
}
