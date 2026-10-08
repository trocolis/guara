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
#define SAVE_SIZE 512
#define SAVE_VERSION 1

void save_serialize(const guara_t *g, uint32_t counter, uint8_t output[SAVE_SIZE]);
/* Valida magic, versão e CRC. Retorna false se o bloco for inválido. */
bool save_deserialize(const uint8_t data[SAVE_SIZE], guara_t *g, uint32_t *counter);

/*
 * Anel de gravação (Pilar 3): 2 setores de 4 KB com 8 slots de 512 bytes.
 * Cada gravação usa o próximo slot; um setor só é apagado quando o anel
 * entra nele. As operações de flash são injetadas para permitir testes.
 */
#define RING_SECTOR 4096
#define RING_SECTORS 2
#define RING_SLOTS ((RING_SECTOR / SAVE_SIZE) * RING_SECTORS)

typedef struct {
    void *ctx;
    bool (*read)(void *ctx, uint32_t offset, void *buf, size_t size);
    bool (*write)(void *ctx, uint32_t offset, const void *buf, size_t size);
    bool (*erase_sector)(void *ctx, uint32_t offset);
} flash_ops_t;

typedef struct {
    flash_ops_t ops;
    int last_slot;    /* -1 se vazio */
    uint32_t counter; /* contador do último save válido */
} ring_t;

void ring_init(ring_t *a, const flash_ops_t *ops);
/* Procura o save válido mais recente. Retorna false se não houver nenhum. */
bool ring_load(ring_t *a, guara_t *g);
bool ring_save(ring_t *a, const guara_t *g);
/* Apaga os dois setores (Factory Rebirth). */
bool ring_erase(ring_t *a);

#endif
