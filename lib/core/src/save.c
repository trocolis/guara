/*
 * Save State: serialização do Guará num bloco de 512 bytes com CRC32 e o anel
 * de 16 slots com nivelamento de desgaste (Pilar 3 do README).
 */
#include "save.h"

#include <string.h>

#include "crc32.h"

/* Offsets dos domínios no bloco de 512 bytes. */
enum {
    OFF_HEADER = 0,      /* 16: magic[5] versao u8 flags u16 timestamp u32 contador u32 */
    OFF_IDENTITY = 16,   /* 20: nome[11] uid u32 cabeca cor nat_nasc natureza estagio */
    OFF_VITALS = 36,     /* 16: hp hp_max mp mp_max energia humor alimentacao saude (u16) */
    OFF_PROGRESS = 52,   /* 21: nivel u8 xp u32 age_minutes u32 atributos[6] u16 */
    OFF_GAUGES = 73,     /*  8: 4 x i16 */
    OFF_AFFINITIES = 81, /* 10: 5 x u16 */
    OFF_INVENTORY = 91,  /* 64: quantidade por item */
    OFF_EQUIPMENT = 155,/* 15: possui u64 + 7 slots */
    OFF_SKILLS = 170,/* 12: possui u64 + 4 ativas */
    OFF_STATES = 182,    /*  8: estados u32 minutes_fainted u16 idle_minutes u16 */
    OFF_END_DATA = 190,
    OFF_CRC = SAVE_SIZE - 4,
};

static void p16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void p32(uint8_t *p, uint32_t v) { p16(p, (uint16_t)v); p16(p + 2, (uint16_t)(v >> 16)); }
static void p64(uint8_t *p, uint64_t v) { p32(p, (uint32_t)v); p32(p + 4, (uint32_t)(v >> 32)); }
static uint16_t l16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t l32(const uint8_t *p) { return l16(p) | ((uint32_t)l16(p + 2) << 16); }
static uint64_t l64(const uint8_t *p) { return l32(p) | ((uint64_t)l32(p + 4) << 32); }

/* Grava o Guará no bloco de 512 bytes (domínios em offsets fixos + CRC32). */
void save_serialize(const guara_t *g, uint32_t counter, uint8_t s[SAVE_SIZE])
{
    memset(s, 0, SAVE_SIZE);
    memcpy(s + OFF_HEADER, "GUARA", 5);
    s[OFF_HEADER + 5] = SAVE_VERSION;
    p32(s + OFF_HEADER + 8, g->timestamp);
    p32(s + OFF_HEADER + 12, counter);

    uint8_t *p = s + OFF_IDENTITY;
    memcpy(p, g->name, SIZE_NAME);
    p32(p + 11, g->uid);
    p[15] = g->head;
    p[16] = g->color;
    p[17] = g->birth_nature;
    p[18] = g->nature;
    p[19] = g->stage;

    p = s + OFF_VITALS;
    const uint16_t vitals[8] = {g->hp, g->hp_max, g->mp, g->mp_max, g->energy, g->mood, g->satiety, g->health};
    for (int i = 0; i < 8; i++) {
        p16(p + 2 * i, vitals[i]);
    }

    p = s + OFF_PROGRESS;
    p[0] = g->level;
    p32(p + 1, g->xp);
    p32(p + 5, g->age_minutes);
    for (int i = 0; i < ATTRIBUTE_COUNT; i++) {
        p16(p + 9 + 2 * i, g->attributes[i]);
    }
    for (int i = 0; i < COUNT_GAUGES; i++) {
        p16(s + OFF_GAUGES + 2 * i, (uint16_t)g->gauges[i]);
    }
    for (int i = 0; i < COUNT_AFFINITIES; i++) {
        p16(s + OFF_AFFINITIES + 2 * i, g->affinities[i]);
    }
    memcpy(s + OFF_INVENTORY, g->inventory, COUNT_ITEMS);
    p64(s + OFF_EQUIPMENT, g->owned_equipment);
    memcpy(s + OFF_EQUIPMENT + 8, g->equipped, COUNT_SLOTS_EQUIP);
    p64(s + OFF_SKILLS, g->owned_skills);
    memcpy(s + OFF_SKILLS + 8, g->active_skills, COUNT_SKILL_ACTIVE);
    p32(s + OFF_STATES, g->states);
    p16(s + OFF_STATES + 4, g->minutes_fainted);
    p16(s + OFF_STATES + 6, g->idle_minutes);

    p32(s + OFF_CRC, guara_crc32(0, s, OFF_CRC));
}

/* Lê o bloco de 512 bytes; recusa magic, versão ou CRC inválidos. */
bool save_deserialize(const uint8_t s[SAVE_SIZE], guara_t *g, uint32_t *counter)
{
    if (memcmp(s, "GUARA", 5) != 0 || s[5] != SAVE_VERSION) {
        return false;
    }
    if (guara_crc32(0, s, OFF_CRC) != l32(s + OFF_CRC)) {
        return false;
    }
    memset(g, 0, sizeof(*g));
    g->timestamp = l32(s + OFF_HEADER + 8);
    if (counter) {
        *counter = l32(s + OFF_HEADER + 12);
    }

    const uint8_t *p = s + OFF_IDENTITY;
    memcpy(g->name, p, SIZE_NAME);
    g->name[SIZE_NAME] = '\0';
    g->uid = l32(p + 11);
    g->head = p[15];
    g->color = p[16];
    g->birth_nature = p[17];
    g->nature = p[18];
    g->stage = p[19];

    p = s + OFF_VITALS;
    uint16_t *vitals[8] = {&g->hp, &g->hp_max, &g->mp, &g->mp_max, &g->energy, &g->mood, &g->satiety, &g->health};
    for (int i = 0; i < 8; i++) {
        *vitals[i] = l16(p + 2 * i);
    }

    p = s + OFF_PROGRESS;
    g->level = p[0];
    g->xp = l32(p + 1);
    g->age_minutes = l32(p + 5);
    for (int i = 0; i < ATTRIBUTE_COUNT; i++) {
        g->attributes[i] = l16(p + 9 + 2 * i);
    }
    for (int i = 0; i < COUNT_GAUGES; i++) {
        g->gauges[i] = (int16_t)l16(s + OFF_GAUGES + 2 * i);
    }
    for (int i = 0; i < COUNT_AFFINITIES; i++) {
        g->affinities[i] = l16(s + OFF_AFFINITIES + 2 * i);
    }
    memcpy(g->inventory, s + OFF_INVENTORY, COUNT_ITEMS);
    g->owned_equipment = l64(s + OFF_EQUIPMENT);
    memcpy(g->equipped, s + OFF_EQUIPMENT + 8, COUNT_SLOTS_EQUIP);
    g->owned_skills = l64(s + OFF_SKILLS);
    memcpy(g->active_skills, s + OFF_SKILLS + 8, COUNT_SKILL_ACTIVE);
    g->states = l32(s + OFF_STATES);
    g->minutes_fainted = l16(s + OFF_STATES + 4);
    g->idle_minutes = l16(s + OFF_STATES + 6);
    return true;
}

/* ------------------------------------------------------------------------- */
/* Anel                                                                      */
/* ------------------------------------------------------------------------- */

#define SLOTS_PER_SECTOR (RING_SECTOR / SAVE_SIZE)

/* Prepara o anel com as operações de Flash (reais ou falsas, nos testes). */
void ring_init(ring_t *a, const flash_ops_t *ops)
{
    a->ops = *ops;
    a->last_slot = -1;
    a->counter = 0;
}

/* Procura nos 16 slots o save válido com o maior contador. */
bool ring_load(ring_t *a, guara_t *g)
{
    uint8_t block[SAVE_SIZE];
    guara_t tmp;
    bool found = false;
    a->last_slot = -1;
    for (int i = 0; i < RING_SLOTS; i++) {
        uint32_t counter;
        if (!a->ops.read(a->ops.ctx, (uint32_t)i * SAVE_SIZE, block, SAVE_SIZE)) {
            continue;
        }
        if (save_deserialize(block, &tmp, &counter) && (!found || counter > a->counter)) {
            found = true;
            a->counter = counter;
            a->last_slot = i;
            *g = tmp;
        }
    }
    return found;
}

/* Verdadeiro se o slot está apagado (todos os bytes 0xFF). */
static bool slot_blank(ring_t *a, int slot)
{
    uint8_t block[SAVE_SIZE];
    if (!a->ops.read(a->ops.ctx, (uint32_t)slot * SAVE_SIZE, block, SAVE_SIZE)) {
        return false;
    }
    for (int i = 0; i < SAVE_SIZE; i++) {
        if (block[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

/* Grava no próximo slot, apagando o setor só quando o anel entra nele. */
bool ring_save(ring_t *a, const guara_t *g)
{
    int slot = (a->last_slot + 1) % RING_SLOTS;
    bool at_sector_start = (slot % SLOTS_PER_SECTOR) == 0;
    if (!at_sector_start && !slot_blank(a, slot)) {
        /* Slot sujo no meio do setor (ex.: queda de energia): pula para o próximo setor. */
        slot = ((slot / SLOTS_PER_SECTOR + 1) % RING_SECTORS) * SLOTS_PER_SECTOR;
        at_sector_start = true;
    }
    if (at_sector_start && !a->ops.erase_sector(a->ops.ctx, (uint32_t)(slot / SLOTS_PER_SECTOR) * RING_SECTOR)) {
        return false;
    }
    uint8_t block[SAVE_SIZE];
    save_serialize(g, a->counter + 1, block);
    if (!a->ops.write(a->ops.ctx, (uint32_t)slot * SAVE_SIZE, block, SAVE_SIZE)) {
        return false;
    }
    a->counter++;
    a->last_slot = slot;
    return true;
}

/* Apaga os dois setores do anel (Factory Rebirth). */
bool ring_erase(ring_t *a)
{
    bool ok = true;
    for (int s = 0; s < RING_SECTORS; s++) {
        ok &= a->ops.erase_sector(a->ops.ctx, (uint32_t)s * RING_SECTOR);
    }
    a->last_slot = -1;
    a->counter = 0;
    return ok;
}
