#ifndef GUARA_INI_H
#define GUARA_INI_H

#include <stdbool.h>

/* Leitor mínimo de INI (game.ini, book.ini): [SECAO] e chave=valor, ';' ou '#' comentam. */
bool ini_texto(const char *conteudo, const char *secao, const char *chave, char *saida, int tamanho);
int ini_inteiro(const char *conteudo, const char *secao, const char *chave, int padrao);

#endif
