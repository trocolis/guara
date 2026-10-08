#ifndef GUARA_INI_H
#define GUARA_INI_H

#include <stdbool.h>

/* Leitor mínimo de INI (game.ini, book.ini): [SECAO] e chave=valor, ';' ou '#' comentam. */
bool ini_text(const char *content, const char *section, const char *key, char *output, int size);
int ini_integer(const char *content, const char *section, const char *key, int fallback);

#endif
