#ifndef GUARA_FONT_H
#define GUARA_FONT_H

#include <stdint.h>

/* Fonte bitmap: glyphs em colunas (bit 0 = topo), ordenados por codepoint. */
typedef struct {
    uint8_t width;       /* colunas por glyph */
    uint8_t height;      /* linhas por glyph */
    uint8_t advance;     /* avanço horizontal (largura + espaço) */
    uint8_t line_height; /* avanço vertical */
    uint16_t count;
    const uint16_t *codes;
    const uint8_t (*columns)[5];
} font_t;

extern const font_t FONT_5X8;

/* Retorna as colunas do glyph, ou as de '?' se o codepoint não existir. */
const uint8_t *font_glyph(const font_t *font, uint32_t code);

/* Decodifica um codepoint UTF-8 e avança *texto. Retorna 0 no fim da string. */
uint32_t utf8_next(const char **text);

/* Quantidade de caracteres (codepoints) da string UTF-8. */
int utf8_size(const char *text);

/* Ponteiro para o n-ésimo caractere (ou para o fim da string). */
const char *utf8_advance(const char *text, int n);

#endif
