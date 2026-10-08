/*
 * Leitor mínimo de arquivos INI (game.ini, book.ini).
 */
#include "ini.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Pula espaços e tabulações. */
static const char *skip_spaces(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t')) {
        p++;
    }
    return p;
}

/* Recua `end` até o último caractere que não é espaço. */
static const char *trim_end(const char *ini, const char *end)
{
    while (end > ini && isspace((unsigned char)end[-1])) {
        end--;
    }
    return end;
}

/* Compara `n` caracteres de `a` com `b`, sem diferenciar maiúsculas. */
static bool equal_ci(const char *a, int n, const char *b)
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

/* Procura `key` na seção `section` e copia o valor para `output`. */
bool ini_text(const char *content, const char *section, const char *key, char *output, int size)
{
    bool in_section = section == NULL || *section == '\0';
    const char *p = content;
    while (p && *p) {
        const char *line_end = strchr(p, '\n');
        const char *end = line_end ? line_end : p + strlen(p);
        const char *l = skip_spaces(p, end);
        const char *e = trim_end(l, end);
        if (l < e && *l == '[') {
            const char *f = memchr(l, ']', (size_t)(e - l));
            if (f) {
                in_section = section && equal_ci(l + 1, (int)(f - l - 1), section);
            }
        } else if (in_section && l < e && *l != ';' && *l != '#') {
            const char *equal = memchr(l, '=', (size_t)(e - l));
            if (equal) {
                const char *key_end = trim_end(l, equal);
                if (equal_ci(l, (int)(key_end - l), key)) {
                    const char *v = skip_spaces(equal + 1, e);
                    int n = (int)(e - v);
                    if (n >= size) {
                        n = size - 1;
                    }
                    memcpy(output, v, (size_t)n);
                    output[n] = '\0';
                    return true;
                }
            }
        }
        p = line_end ? line_end + 1 : NULL;
    }
    return false;
}

/* Lê um valor inteiro, ou devolve `fallback` se a chave não existir. */
int ini_integer(const char *content, const char *section, const char *key, int fallback)
{
    char buf[24];
    return ini_text(content, section, key, buf, sizeof(buf)) ? atoi(buf) : fallback;
}
