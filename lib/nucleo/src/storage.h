#ifndef GUARA_STORAGE_H
#define GUARA_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

/*
 * Leitura da imagem da partição 'storage' (formato v2, gerada por
 * tools/storage-packer/pack_storage.py). A imagem fica mapeada na memória e
 * todos os ponteiros devolvidos apontam para dentro dela.
 */

typedef enum {
    CAT_ITENS,
    CAT_EQUIPAMENTOS,
    CAT_HABILIDADES,
    QTD_CATALOGOS,
} catalogo_t;

typedef struct {
    uint8_t id;
    uint8_t tipo;      /* tipo_item_t, tipo_equip_t ou tipo_hab_t */
    uint8_t natureza;  /* afinidade (itens/equip.) ou natureza (habilidades) */
    uint8_t alvo;      /* alvo_hab_t */
    uint8_t poder;
    uint8_t custo_mp;
    uint8_t n_efeitos;
    uint8_t n_sobre;
    struct {
        uint8_t alvo;   /* alvo_efeito_t */
        int16_t valor;
    } efeitos[4];
    struct {
        uint8_t estado; /* estado_t */
        int8_t valor;   /* % ; -100 = cura */
    } sobre[2];
    const char *slug;
    const char *icone;
    const char *nome;
    const char *descricao;
} registro_t;

typedef enum {
    STORAGE_OK,
    STORAGE_ERRO_MAGIC,
    STORAGE_ERRO_VERSAO,
    STORAGE_ERRO_CRC,
    STORAGE_ERRO_LIMITES,
} storage_status_t;

storage_status_t storage_abrir(const uint8_t *imagem, size_t tamanho);
bool storage_aberto(void);

/* Busca um bitmap pelo nome (sem extensão). Retorna false se não existir. */
bool vrom_buscar(const char *nome, gfx_bitmap_t *saida);
/* Como vrom_buscar, mas devolve um ponteiro estático (ou NULL). Útil para desenho imediato. */
const gfx_bitmap_t *vrom(const char *nome);
int vrom_quantidade(void);

int catalogo_quantidade(catalogo_t cat);
bool catalogo_ler(catalogo_t cat, int id, registro_t *saida);
/* Busca pelo slug; retorna o id ou -1. */
int catalogo_buscar_slug(catalogo_t cat, const char *slug);

#endif
