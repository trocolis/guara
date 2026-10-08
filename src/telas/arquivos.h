#ifndef GUARA_ARQUIVOS_H
#define GUARA_ARQUIVOS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

typedef struct {
    char nome[64];
    bool pasta;
    uint32_t tamanho;
} entrada_t;

typedef struct {
    int quantidade;
    entrada_t itens[];
} lista_arquivos_t;

/*
 * Lista uma pasta (pastas primeiro, ordem alfabética). `extensoes` é uma lista
 * terminada em NULL (ex.: {".bmp", NULL}); NULL aceita tudo. Liberar com free().
 */
lista_arquivos_t *arquivos_listar(const char *pasta, const char *const *extensoes, bool incluir_pastas, int maximo);
bool arquivos_tem_extensao(const char *nome, const char *ext);
/* Ícone da VROM para o tipo do arquivo (README > Cartão SD). */
const char *arquivos_icone(const entrada_t *e);
void arquivos_formatar_tamanho(uint64_t bytes, char *buf, int tam);
/* Nome amigável: sem extensão e com '_' trocado por espaço. */
void arquivos_nome_amigavel(const char *nome, char *buf, int tam);
/* Lê o arquivo inteiro para a memória (malloc). */
uint8_t *arquivos_ler(const char *caminho, size_t *tamanho, size_t maximo);
/* Carrega um BMP 1-bit do SD (malloc). Liberar com arquivos_liberar_bmp. */
bool arquivos_carregar_bmp(const char *caminho, gfx_bitmap_t *bmp);
void arquivos_liberar_bmp(gfx_bitmap_t *bmp);

#endif
