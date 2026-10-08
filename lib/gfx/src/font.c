/*
 * Busca de glyphs na fonte bitmap e decodificação de texto UTF-8.
 */
#include "font.h"

/* Busca binária do glyph pelo codepoint; codepoints ausentes viram '?'. */
const uint8_t *font_glyph(const font_t *font, uint32_t code)
{
    int ini = 0, end = font->count - 1;
    while (ini <= end) {
        int mid = (ini + end) / 2;
        if (font->codes[mid] == code) {
            return font->columns[mid];
        }
        if (font->codes[mid] < code) {
            ini = mid + 1;
        } else {
            end = mid - 1;
        }
    }
    return code == '?' ? font->columns[0] : font_glyph(font, '?');
}

/* Decodifica o próximo codepoint UTF-8; sequências inválidas viram '?'. */
uint32_t utf8_next(const char **text)
{
    const uint8_t *p = (const uint8_t *)*text;
    uint32_t c = *p;
    int extra;

    if (c == 0) {
        return 0;
    }
    if (c < 0x80) {
        extra = 0;
    } else if ((c & 0xE0) == 0xC0) {
        c &= 0x1F;
        extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
        c &= 0x0F;
        extra = 2;
    } else if ((c & 0xF8) == 0xF0) {
        c &= 0x07;
        extra = 3;
    } else {
        *text += 1;
        return '?';
    }
    p++;
    while (extra--) {
        if ((*p & 0xC0) != 0x80) {
            *text = (const char *)p;
            return '?';
        }
        c = (c << 6) | (*p++ & 0x3F);
    }
    *text = (const char *)p;
    return c;
}

/* Quantidade de codepoints da string. */
int utf8_size(const char *text)
{
    int n = 0;
    while (utf8_next(&text)) {
        n++;
    }
    return n;
}

/* Avança `n` codepoints na string. */
const char *utf8_advance(const char *text, int n)
{
    while (n-- > 0 && utf8_next(&text)) {
    }
    return text;
}
