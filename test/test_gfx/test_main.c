#include <string.h>
#include <unity.h>

#include "gfx.h"

static gfx_t g;

void setUp(void) { gfx_iniciar(&g); }
void tearDown(void) {}

static void test_pixel_formato_paginas(void)
{
    gfx_pixel(&g, 5, 9, GFX_BRANCO);
    TEST_ASSERT_EQUAL_HEX8(0x02, g.buf[1][5]);
    TEST_ASSERT_TRUE(gfx_ler_pixel(&g, 5, 9));
    gfx_pixel(&g, 5, 9, GFX_INVERTER);
    TEST_ASSERT_FALSE(gfx_ler_pixel(&g, 5, 9));
}

static void test_recorte(void)
{
    gfx_recortar(&g, 10, 10, 20, 20);
    gfx_retangulo_cheio(&g, 0, 0, 128, 64, GFX_BRANCO);
    TEST_ASSERT_FALSE(gfx_ler_pixel(&g, 9, 10));
    TEST_ASSERT_TRUE(gfx_ler_pixel(&g, 10, 10));
    TEST_ASSERT_TRUE(gfx_ler_pixel(&g, 19, 19));
    TEST_ASSERT_FALSE(gfx_ler_pixel(&g, 20, 19));
}

static void test_utf8(void)
{
    const char *s = "Maçã";
    TEST_ASSERT_EQUAL_INT(4, utf8_tamanho(s));
    TEST_ASSERT_EQUAL_UINT32('M', utf8_proximo(&s));
    TEST_ASSERT_EQUAL_UINT32('a', utf8_proximo(&s));
    TEST_ASSERT_EQUAL_UINT32(0xE7, utf8_proximo(&s));
    TEST_ASSERT_EQUAL_UINT32(0xE3, utf8_proximo(&s));
    TEST_ASSERT_EQUAL_UINT32(0, utf8_proximo(&s));
    TEST_ASSERT_EQUAL_INT(23, gfx_largura_texto("Maçã"));
}

static void test_glyph_acentuado_existe(void)
{
    const uint8_t *q = fonte_glyph(&FONTE_5X8, '?');
    TEST_ASSERT_NOT_EQUAL(q, fonte_glyph(&FONTE_5X8, 0xE7)); /* ç */
    TEST_ASSERT_EQUAL_PTR(q, fonte_glyph(&FONTE_5X8, 0x4E00)); /* inexistente vira '?' */
}

static void test_bitmap_modos(void)
{
    /* 2x1: pixel 0 aceso, pixel 1 apagado */
    static const uint8_t dados[] = {0x80};
    gfx_bitmap_t b = {2, 1, dados};
    gfx_pixel(&g, 1, 0, GFX_BRANCO);
    gfx_bitmap(&g, &b, 0, 0, GFX_OU, 0);
    TEST_ASSERT_TRUE(gfx_ler_pixel(&g, 0, 0));
    TEST_ASSERT_TRUE(gfx_ler_pixel(&g, 1, 0)); /* OU preserva o fundo */
    gfx_bitmap(&g, &b, 0, 0, GFX_E, 0);
    TEST_ASSERT_TRUE(gfx_ler_pixel(&g, 0, 0));
    TEST_ASSERT_FALSE(gfx_ler_pixel(&g, 1, 0)); /* E apaga onde o bitmap é preto */
    gfx_limpar(&g);
    gfx_bitmap(&g, &b, 0, 0, GFX_COPIA, GFX_ESPELHAR);
    TEST_ASSERT_FALSE(gfx_ler_pixel(&g, 0, 0));
    TEST_ASSERT_TRUE(gfx_ler_pixel(&g, 1, 0));
    gfx_limpar(&g);
    gfx_bitmap(&g, &b, 0, 0, GFX_COPIA, GFX_ESCALA2);
    TEST_ASSERT_TRUE(gfx_ler_pixel(&g, 1, 1));
    TEST_ASSERT_FALSE(gfx_ler_pixel(&g, 2, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pixel_formato_paginas);
    RUN_TEST(test_recorte);
    RUN_TEST(test_utf8);
    RUN_TEST(test_glyph_acentuado_existe);
    RUN_TEST(test_bitmap_modos);
    return UNITY_END();
}
