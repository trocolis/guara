#include <string.h>
#include <unity.h>

#include "crc32.h"

void setUp(void) {}
void tearDown(void) {}

static void test_crc32_vetor_padrao(void)
{
    /* Valor de verificação clássico do CRC-32/IEEE. */
    const char *texto = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, guara_crc32(0, texto, strlen(texto)));
}

static void test_crc32_vazio(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, guara_crc32(0, "", 0));
}

static void test_crc32_em_partes(void)
{
    const char *texto = "123456789";
    uint32_t crc = guara_crc32(0, texto, 4);
    crc = guara_crc32(crc, texto + 4, 5);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc32_vetor_padrao);
    RUN_TEST(test_crc32_vazio);
    RUN_TEST(test_crc32_em_partes);
    return UNITY_END();
}
