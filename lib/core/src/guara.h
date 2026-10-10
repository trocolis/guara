#ifndef GUARA_GUARA_H
#define GUARA_GUARA_H

#include <stdbool.h>
#include <stdint.h>

#include "rng.h"
#include "storage.h"
#include "types.h"

#define SIZE_NAME 10
#define COUNT_ITEMS 64
#define COUNT_SKILL_ACTIVE 4
#define SLOT_EMPTY 0xFF

/* Estado completo do Guará: é o que vai para o Save State (ver save.h). */
typedef struct {
    /* Identidade */
    char name[SIZE_NAME + 1];
    uint32_t uid;
    uint8_t head;         /* 1..9 */
    uint8_t color;        /* pet_color_t */
    uint8_t birth_nature; /* natureza primária sorteada */
    uint8_t nature;       /* natureza atual */
    uint8_t stage;        /* stage_t */

    /* Vitais */
    uint16_t hp, hp_max, mp, mp_max;
    uint16_t energy, mood, satiety, health; /* 0..10000 */

    /* Progresso */
    uint8_t level;
    uint32_t xp;
    uint32_t age_minutes;
    uint16_t attributes[ATTRIBUTE_COUNT];
    int16_t gauges[COUNT_GAUGES];
    uint16_t affinities[COUNT_AFFINITIES]; /* 0..10000 */

    /* Posses */
    uint8_t inventory[COUNT_ITEMS];
    uint64_t owned_equipment;
    uint8_t equipped[COUNT_SLOTS_EQUIP];
    uint64_t owned_skills;
    uint8_t active_skills[COUNT_SKILL_ACTIVE];

    /* Estados */
    uint32_t states;
    uint16_t minutes_fainted;
    uint16_t idle_minutes;
    uint32_t timestamp; /* epoch da última atualização; 0 = desconhecido */
} guara_t;

/* Contexto de relógio para o ciclo de vida. */
typedef struct {
    int minute_of_day;  /* 0..1439, ou -1 se a hora não for conhecida */
    int bedtime_minute; /* hora de dormir (min. do dia) */
    int wake_minute;    /* hora de acordar (min. do dia) */
} clock_ctx_t;

/* Resultado do uso de um item. */
typedef enum {
    USE_OK,
    USE_BLOCKED_FAINTED,
    USE_BLOCKED_CURSE,
    USE_NO_STOCK,
    USE_INVALID,
    USE_FULL, /* não faria efeito: alimentação cheia (comida/bebida) ou nada a recuperar (poção) */
} item_use_result_t;

void guara_birth(guara_t *g, const char *name, rng_t *rng, uint32_t now);

static inline bool guara_has_state(const guara_t *g, state_t e) { return (g->states & STATE_BIT(e)) != 0; }
void guara_set_state(guara_t *g, state_t e, bool enabled);

/* Avança um minuto do ciclo de vida. `protegido` aplica as travas do Δt. */
void guara_tick_minute(guara_t *g, const clock_ctx_t *clock_ctx, bool protected);
/* Simula o tempo passado (Δt) com as travas de proteção. Retorna os minutos simulados. */
uint32_t guara_catch_up_time(guara_t *g, uint32_t now, int minute_of_day_now, int bedtime_minute, int wake_minute);

/* Registra uma interação do usuário (zera o ócio, move Dia×Noite). */
void guara_interact(guara_t *g, const clock_ctx_t *clock_ctx);

/* Versões de desenvolvimento (0.x.x) começam com todo o catálogo liberado. */
bool guara_is_development_version(const char *version);
/* 255 unidades de cada item e posse de todos os equipamentos e habilidades do catálogo. */
void guara_unlock_catalog(guara_t *g);

item_use_result_t guara_can_use_item(const guara_t *g, const catalog_record_t *item);
item_use_result_t guara_use_item(guara_t *g, const catalog_record_t *item, rng_t *rng);

/* Experiência e nível. Retorna quantos níveis subiu. */
uint32_t guara_xp_to_next_level(uint8_t level);
int guara_gain_xp(guara_t *g, uint32_t points, rng_t *rng);
/* Multiplicador de XP por idade, em %. */
int guara_age_bonus(const guara_t *g);

/* Afinidades: soma pontos respeitando os opostos e a natureza de nascimento. */
void guara_add_affinity(guara_t *g, nature_t element, int centi);
bool guara_affinity_enabled(const guara_t *g);

void guara_add_gauge(guara_t *g, gauge_t m, int delta);

/* Atributo com equipamentos e estados aplicados. */
int guara_effective_attribute(const guara_t *g, effect_target_t attr);

/* Equipamentos */
bool guara_equip(guara_t *g, int slot, int equip_id);
void guara_unequip(guara_t *g, int slot);
int guara_slot_type(int slot); /* equipment_type_t aceito pelo slot */

/* Estágio de evolução para um nível. */
stage_t guara_stage_for_level(uint8_t level);
/* Elementos primários que compõem uma natureza (bits de NAT_FIRE..NAT_AIR). */
unsigned nature_elements(nature_t n);
const char *nature_name(nature_t n);
const char *nature_icon(nature_t n);
const char *state_name(state_t e);
const char *state_icon(state_t e);

/* Emoção predominante para os olhos e o balão (um dos EST_* emocionais, ou -1). */
int guara_main_emotion(const guara_t *g);
/* Estado físico mais importante para exibir (ou -1 para normal). */
int guara_main_state(const guara_t *g);

#endif
