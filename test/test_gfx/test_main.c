#include <string.h>
#include <unity.h>

#include "gfx.h"

static gfx_t g;

void setUp(void) { gfx_init(&g); }
void tearDown(void) {}

static void test_pixel_page_format(void)
{
    gfx_pixel(&g, 5, 9, GFX_WHITE);
    TEST_ASSERT_EQUAL_HEX8(0x02, g.buf[1][5]);
    TEST_ASSERT_TRUE(gfx_read_pixel(&g, 5, 9));
    gfx_pixel(&g, 5, 9, GFX_INVERT);
    TEST_ASSERT_FALSE(gfx_read_pixel(&g, 5, 9));
}

static void test_clip(void)
{
    gfx_clip(&g, 10, 10, 20, 20);
    gfx_rect_filled(&g, 0, 0, 128, 64, GFX_WHITE);
    TEST_ASSERT_FALSE(gfx_read_pixel(&g, 9, 10));
    TEST_ASSERT_TRUE(gfx_read_pixel(&g, 10, 10));
    TEST_ASSERT_TRUE(gfx_read_pixel(&g, 19, 19));
    TEST_ASSERT_FALSE(gfx_read_pixel(&g, 20, 19));
}

static void test_utf8(void)
{
    const char *s = "Maçã";
    TEST_ASSERT_EQUAL_INT(4, utf8_size(s));
    TEST_ASSERT_EQUAL_UINT32('M', utf8_next(&s));
    TEST_ASSERT_EQUAL_UINT32('a', utf8_next(&s));
    TEST_ASSERT_EQUAL_UINT32(0xE7, utf8_next(&s));
    TEST_ASSERT_EQUAL_UINT32(0xE3, utf8_next(&s));
    TEST_ASSERT_EQUAL_UINT32(0, utf8_next(&s));
    TEST_ASSERT_EQUAL_INT(23, gfx_text_width("Maçã"));
}

static void test_glyph_accented_exists(void)
{
    const uint8_t *q = font_glyph(&FONT_5X8, '?');
    TEST_ASSERT_NOT_EQUAL(q, font_glyph(&FONT_5X8, 0xE7));   /* ç */
    TEST_ASSERT_EQUAL_PTR(q, font_glyph(&FONT_5X8, 0x4E00)); /* inexistente vira '?' */
}

static void test_bitmap_modes(void)
{
    /* 2x1: pixel 0 aceso, pixel 1 apagado */
    static const uint8_t data[] = {0x80};
    gfx_bitmap_t b = {2, 1, data};
    gfx_pixel(&g, 1, 0, GFX_WHITE);
    gfx_bitmap(&g, &b, 0, 0, GFX_OR, 0);
    TEST_ASSERT_TRUE(gfx_read_pixel(&g, 0, 0));
    TEST_ASSERT_TRUE(gfx_read_pixel(&g, 1, 0)); /* OU preserva o fundo */
    gfx_bitmap(&g, &b, 0, 0, GFX_E, 0);
    TEST_ASSERT_TRUE(gfx_read_pixel(&g, 0, 0));
    TEST_ASSERT_FALSE(gfx_read_pixel(&g, 1, 0)); /* E apaga onde o bitmap é preto */
    gfx_clear(&g);
    gfx_bitmap(&g, &b, 0, 0, GFX_COPY, GFX_MIRROR);
    TEST_ASSERT_FALSE(gfx_read_pixel(&g, 0, 0));
    TEST_ASSERT_TRUE(gfx_read_pixel(&g, 1, 0));
    gfx_clear(&g);
    gfx_bitmap(&g, &b, 0, 0, GFX_COPY, GFX_SCALE2X);
    TEST_ASSERT_TRUE(gfx_read_pixel(&g, 1, 1));
    TEST_ASSERT_FALSE(gfx_read_pixel(&g, 2, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pixel_page_format);
    RUN_TEST(test_clip);
    RUN_TEST(test_utf8);
    RUN_TEST(test_glyph_accented_exists);
    RUN_TEST(test_bitmap_modes);
    return UNITY_END();
}
