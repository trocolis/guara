#ifndef GUARA_FONTE_H
#define GUARA_FONTE_H

#include <stdint.h>

/* Fonte bitmap: glyphs em colunas (bit 0 = topo), ordenados por codepoint. */
typedef struct {
    uint8_t largura;     /* colunas por glyph */
    uint8_t altura;      /* linhas por glyph */
    uint8_t avanco;      /* avanço horizontal (largura + espaço) */
    uint8_t entrelinha;  /* avanço vertical */
    uint16_t quantidade;
    const uint16_t *codigos;
    const uint8_t (*colunas)[5];
} fonte_t;

extern const fonte_t FONTE_5X8;

/* Retorna as colunas do glyph, ou as de '?' se o codepoint não existir. */
const uint8_t *fonte_glyph(const fonte_t *fonte, uint32_t codigo);

/* Decodifica um codepoint UTF-8 e avança *texto. Retorna 0 no fim da string. */
uint32_t utf8_proximo(const char **texto);

/* Quantidade de caracteres (codepoints) da string UTF-8. */
int utf8_tamanho(const char *texto);

/* Ponteiro para o n-ésimo caractere (ou para o fim da string). */
const char *utf8_avancar(const char *texto, int n);

#endif
