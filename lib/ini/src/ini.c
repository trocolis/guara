#include "ini.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const char *pular_espacos(const char *p, const char *fim)
{
    while (p < fim && (*p == ' ' || *p == '\t')) {
        p++;
    }
    return p;
}

static const char *aparar_fim(const char *ini, const char *fim)
{
    while (fim > ini && isspace((unsigned char)fim[-1])) {
        fim--;
    }
    return fim;
}

static bool igual_ci(const char *a, int n, const char *b)
{
    if ((int)strlen(b) != n) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return false;
        }
    }
    return true;
}

bool ini_texto(const char *conteudo, const char *secao, const char *chave, char *saida, int tamanho)
{
    bool na_secao = secao == NULL || *secao == '\0';
    const char *p = conteudo;
    while (p && *p) {
        const char *fim_linha = strchr(p, '\n');
        const char *fim = fim_linha ? fim_linha : p + strlen(p);
        const char *l = pular_espacos(p, fim);
        const char *e = aparar_fim(l, fim);
        if (l < e && *l == '[') {
            const char *f = memchr(l, ']', (size_t)(e - l));
            if (f) {
                na_secao = secao && igual_ci(l + 1, (int)(f - l - 1), secao);
            }
        } else if (na_secao && l < e && *l != ';' && *l != '#') {
            const char *igual = memchr(l, '=', (size_t)(e - l));
            if (igual) {
                const char *fim_chave = aparar_fim(l, igual);
                if (igual_ci(l, (int)(fim_chave - l), chave)) {
                    const char *v = pular_espacos(igual + 1, e);
                    int n = (int)(e - v);
                    if (n >= tamanho) {
                        n = tamanho - 1;
                    }
                    memcpy(saida, v, (size_t)n);
                    saida[n] = '\0';
                    return true;
                }
            }
        }
        p = fim_linha ? fim_linha + 1 : NULL;
    }
    return false;
}

int ini_inteiro(const char *conteudo, const char *secao, const char *chave, int padrao)
{
    char buf[24];
    return ini_texto(conteudo, secao, chave, buf, sizeof(buf)) ? atoi(buf) : padrao;
}
