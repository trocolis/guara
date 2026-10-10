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
    CAT_ITEMS,
    CAT_EQUIPMENT,
    CAT_SKILLS,
    COUNT_CATALOGS,
} catalog_t;

typedef struct {
    uint8_t id;
    uint8_t type;   /* item_type_t, equipment_type_t ou skill_type_t */
    uint8_t nature; /* afinidade (itens/equip.) ou natureza (habilidades) */
    uint8_t target; /* skill_target_t */
    uint8_t power;
    uint8_t mp_cost;
    uint8_t n_effects;
    uint8_t n_status;
    struct {
        uint8_t target;   /* effect_target_t */
        int16_t value;
    } effects[4];
    struct {
        uint8_t state; /* state_t */
        int8_t value;  /* % ; -100 = cura */
    } status[2];
    const char *slug;
    const char *icon;
    const char *name;
    const char *description;
} catalog_record_t;

typedef enum {
    STORAGE_OK,
    STORAGE_ERROR_MAGIC,
    STORAGE_ERROR_VERSION,
    STORAGE_ERROR_CRC,
    STORAGE_ERROR_BOUNDS,
} storage_status_t;

storage_status_t storage_open(const uint8_t *image, size_t size);
bool storage_is_open(void);
/* Pacote de certificados HTTPS (formato esp_crt_bundle do ESP-IDF), ou NULL se o storage não tiver. */
const uint8_t *storage_certs(size_t *size);

/* Busca um bitmap pelo nome (sem extensão). Retorna false se não existir. */
bool vrom_lookup(const char *name, gfx_bitmap_t *output);
/* Como vrom_lookup, mas devolve um ponteiro estático (ou NULL). Útil para desenho imediato. */
const gfx_bitmap_t *vrom(const char *name);
int vrom_count(void);

int catalog_record_count(catalog_t cat);
bool catalog_read(catalog_t cat, int id, catalog_record_t *output);
/* Busca pelo slug; retorna o id ou -1. */
int catalog_find_slug(catalog_t cat, const char *slug);

#endif
