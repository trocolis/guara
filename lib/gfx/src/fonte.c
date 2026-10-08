#include "fonte.h"

const uint8_t *fonte_glyph(const fonte_t *fonte, uint32_t codigo)
{
    int ini = 0, fim = fonte->quantidade - 1;
    while (ini <= fim) {
        int meio = (ini + fim) / 2;
        if (fonte->codigos[meio] == codigo) {
            return fonte->colunas[meio];
        }
        if (fonte->codigos[meio] < codigo) {
            ini = meio + 1;
        } else {
            fim = meio - 1;
        }
    }
    return codigo == '?' ? fonte->colunas[0] : fonte_glyph(fonte, '?');
}

uint32_t utf8_proximo(const char **texto)
{
    const uint8_t *p = (const uint8_t *)*texto;
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
        *texto += 1;
        return '?';
    }
    p++;
    while (extra--) {
        if ((*p & 0xC0) != 0x80) {
            *texto = (const char *)p;
            return '?';
        }
        c = (c << 6) | (*p++ & 0x3F);
    }
    *texto = (const char *)p;
    return c;
}

int utf8_tamanho(const char *texto)
{
    int n = 0;
    while (utf8_proximo(&texto)) {
        n++;
    }
    return n;
}

const char *utf8_avancar(const char *texto, int n)
{
    while (n-- > 0 && utf8_proximo(&texto)) {
    }
    return texto;
}
