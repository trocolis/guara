#ifndef GUARA_COMBAT_H
#define GUARA_COMBAT_H

#include <stdbool.h>
#include <stdint.h>

#include "guara.h"
#include "rng.h"
#include "storage.h"

/* Motor de combate por barra de tempo (ATB). Lado 0 = jogador, 1 = oponente. */

#define COMBAT_MAX_ITEMS 3

typedef struct {
    char name[SIZE_NAME + 1];
    uint8_t nature, color, head, stage, level;
    int hp, hp_max, mp, mp_max;
    int attributes[ATTRIBUTE_COUNT];
    uint32_t states;
    uint8_t skills[COUNT_SKILL_ACTIVE];
    int atb_ms;    /* tempo acumulado */
    int atb_total; /* tempo para encher a barra */
    /* Poções do oponente gerado (o jogador usa o próprio inventário); quantidade 0 = vazio */
    uint8_t item_ids[COMBAT_MAX_ITEMS];
    uint8_t item_counts[COMBAT_MAX_ITEMS];
} fighter_t;

typedef enum { ACTION_ATTACK, ACTION_SKILL, ACTION_ITEM, ACTION_FORFEIT } combat_action_type_t;

typedef struct {
    combat_action_type_t type;
    int position; /* slot da habilidade (0..3) ou id do item */
    unsigned reaction; /* COMBAT_TIMED_STRIKE e/ou COMBAT_GUARD, apertados na janela */
} combat_action_t;

/* Reações na janela de A durante o preparo de um golpe. */
#define COMBAT_TIMED_STRIKE 1u /* quem ataca acertou o tempo: mais crítico e chance de ataque duplo */
#define COMBAT_GUARD        2u /* quem é atacado se defendeu: menos dano e mais chance de erro */

typedef struct {
    int actor, target;
    combat_action_type_t type;
    bool failed; /* não conseguiu agir (paralisado, congelado, sem MP, maldição) */
    bool missed; /* confusão */
    bool dodged;
    bool critical;
    bool timed;   /* golpe no tempo certo */
    bool guarded; /* alvo se defendeu */
    int hits;     /* 2 no ataque duplo */
    bool self_hit;    /* confuso atingiu a si mesmo */
    int damage;
    int heal;
    int status_damage;    /* dano de veneno/queimadura/gelo no início do turno */
    int applied_state;/* state_t aplicado no alvo, ou -1 */
    int multiplier; /* % elemental aplicado */
    char icon[32];  /* ícone da habilidade/item usado */
} combat_result_t;

typedef struct {
    fighter_t side[2];
    bool finished;
    int winner; /* 0, 1 ou -1 */
    rng_t *rng;
} combat_t;

int combat_atb_time(int agility);
int combat_multiplier(nature_t attack, nature_t defender);

void combat_fighter_from_guara(const guara_t *g, fighter_t *l);
void combat_generate_opponent(const guara_t *reference, rng_t *rng, fighter_t *l);

void combat_start(combat_t *c, const fighter_t *player, const fighter_t *opponent, rng_t *rng);
/* Avança as barras ATB. Retorna máscara de lados prontos (bit 0 jogador, bit 1 oponente). */
unsigned combat_advance(combat_t *c, int dt_ms);
bool combat_ready(const combat_t *c, int side);
/* Executa a ação de um lado pronto. `item` é necessário para ACTION_ITEM. */
void combat_execute(combat_t *c, int side, combat_action_t action, const catalog_record_t *item, combat_result_t *r);
/* Verdadeiro se a ação é um golpe contra o outro lado, com janela de reação. */
bool combat_has_window(const combat_t *c, int side, combat_action_t action);
/* Escolha automática do oponente. */
combat_action_t combat_ai(combat_t *c, int side);
/* Lê o item da ação (ACTION_ITEM) do catálogo. Retorna falso se não for item ou não existir. */
bool combat_action_item(combat_action_t action, catalog_record_t *item);

uint32_t combat_xp(const combat_t *c, int winning_side);
/* Sorteia um item dropado (id) ou -1. */
int combat_drop(const combat_t *c, int winning_side);

#endif
