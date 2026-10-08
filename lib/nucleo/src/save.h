#ifndef GUARA_SAVE_H
#define GUARA_SAVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "guara.h"

/*
 * Save State: bloco fixo de 512 bytes, little-endian, com domínios em
 * offsets fixos (tabela em README > Domínios do Save State). Os últimos
 * 4 bytes são o CRC32 dos 508 anteriores.
 */
#define SAVE_TAMANHO 512
#define SAVE_VERSAO 1

void save_serializar(const guara_t *g, uint32_t contador, uint8_t saida[SAVE_TAMANHO]);
/* Valida magic, versão e CRC. Retorna false se o bloco for inválido. */
bool save_desserializar(const uint8_t dados[SAVE_TAMANHO], guara_t *g, uint32_t *contador);

/*
 * Anel de gravação (Pilar 3): 2 setores de 4 KB com 8 slots de 512 bytes.
 * Cada gravação usa o próximo slot; um setor só é apagado quando o anel
 * entra nele. As operações de flash são injetadas para permitir testes.
 */
#define ANEL_SETOR 4096
#define ANEL_SETORES 2
#define ANEL_SLOTS ((ANEL_SETOR / SAVE_TAMANHO) * ANEL_SETORES)

typedef struct {
    void *ctx;
    bool (*ler)(void *ctx, uint32_t offset, void *buf, size_t tamanho);
    bool (*escrever)(void *ctx, uint32_t offset, const void *buf, size_t tamanho);
    bool (*apagar_setor)(void *ctx, uint32_t offset);
} flash_ops_t;

typedef struct {
    flash_ops_t ops;
    int ultimo_slot;    /* -1 se vazio */
    uint32_t contador;  /* contador do último save válido */
} anel_t;

void anel_iniciar(anel_t *a, const flash_ops_t *ops);
/* Procura o save válido mais recente. Retorna false se não houver nenhum. */
bool anel_carregar(anel_t *a, guara_t *g);
bool anel_salvar(anel_t *a, const guara_t *g);
/* Apaga os dois setores (Factory Rebirth). */
bool anel_apagar(anel_t *a);

#endif
