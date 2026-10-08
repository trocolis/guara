/*
 * Motor de combate por barra de tempo (ATB): fórmulas do README, multiplicador
 * elemental com neutralização das naturezas compostas, estados e IA do oponente.
 * Não depende de hardware: é testado no ambiente native.
 */
#include "combat.h"

#include <stdio.h>
#include <string.h>

#include "rules.h"

/* Limita `v` ao intervalo [min, max]. */
static int clamp(int v, int min, int max)
{
    return v < min ? min : (v > max ? max : v);
}

/* Verdadeiro se o lutador está com o estado `s`. */
static bool has(const fighter_t *l, state_t e)
{
    return (l->states & STATE_BIT(e)) != 0;
}

/* Tempo (ms) para encher a barra ATB: 6 s - AGI x 50 ms, com mínimo de 1,5 s. */
int combat_atb_time(int agility)
{
    int t = ATB_BASE_MS - agility * ATB_PER_AGI_MS;
    return t < ATB_MIN_MS ? ATB_MIN_MS : t;
}

/* Água vence Fogo, Terra vence Água, Ar vence Terra, Fogo vence Ar. */
static unsigned beats(unsigned elements)
{
    unsigned v = 0;
    if (elements & (1u << NAT_WATER)) v |= 1u << NAT_FIRE;
    if (elements & (1u << NAT_EARTH)) v |= 1u << NAT_WATER;
    if (elements & (1u << NAT_AIR)) v |= 1u << NAT_EARTH;
    if (elements & (1u << NAT_FIRE)) v |= 1u << NAT_AIR;
    return v;
}

/* Multiplicador elemental (%) de um ataque da natureza `attack` contra `defender`. */
int combat_multiplier(nature_t attack, nature_t defender)
{
    if ((attack == NAT_LIGHT && defender == NAT_SHADOW) || (attack == NAT_SHADOW && defender == NAT_LIGHT)) {
        return MULT_ADVANTAGE;
    }
    unsigned a = nature_elements(attack), d = nature_elements(defender);
    if (!a || !d) {
        return 100;
    }
    /* Fraqueza do defensor é neutralizada se ele também contém o elemento atacante. */
    bool advantage = (beats(a & ~d) & d) != 0;
    bool disadvantage = (beats(d & ~a) & a) != 0;
    if (advantage && !disadvantage) {
        return MULT_ADVANTAGE;
    }
    if (disadvantage && !advantage) {
        return MULT_DISADVANTAGE;
    }
    return 100;
}

/* Copia do Guará para um lutador os valores usados em combate (atributos já com equipamentos). */
void combat_fighter_from_guara(const guara_t *g, fighter_t *l)
{
    memset(l, 0, sizeof(*l));
    memcpy(l->name, g->name, sizeof(l->name));
    l->nature = g->nature;
    l->color = g->color;
    l->head = g->head;
    l->stage = g->stage;
    l->level = g->level;
    l->hp = g->hp;
    l->hp_max = g->hp_max;
    l->mp = g->mp;
    l->mp_max = g->mp_max;
    for (int i = 0; i < ATTRIBUTE_COUNT; i++) {
        l->attributes[i] = guara_effective_attribute(g, (effect_target_t)(ATTR_ATK + i));
    }
    l->states = g->states;
    memcpy(l->skills, g->active_skills, sizeof(l->skills));
}

/* Gera um oponente de treino com o nível do Guará e atributos variando ±10%. */
void combat_generate_opponent(const guara_t *ref, rng_t *rng, fighter_t *l)
{
    static const char *const names[] = {"Tupa", "Jaci", "Iara", "Caipora", "Saci", "Boitata", "Curupira", "Guaraci"};
    memset(l, 0, sizeof(*l));
    snprintf(l->name, sizeof(l->name), "%s", names[rng_range(rng, 0, 7)]);
    l->nature = (uint8_t)rng_range(rng, NAT_FIRE, NAT_AIR);
    l->color = (uint8_t)rng_range(rng, 0, 1);
    l->head = (uint8_t)rng_range(rng, 1, 9);
    l->stage = ref->stage;
    l->level = ref->level;
    for (int i = 0; i < ATTRIBUTE_COUNT; i++) {
        int base = guara_effective_attribute(ref, (effect_target_t)(ATTR_ATK + i));
        l->attributes[i] = clamp(base * rng_range(rng, 90, 110) / 100, 1, 999);
    }
    l->hp_max = clamp(ref->hp_max * rng_range(rng, 90, 110) / 100, 1, 9999);
    l->mp_max = ref->mp_max;
    l->hp = l->hp_max;
    l->mp = l->mp_max;
    memset(l->skills, SLOT_EMPTY, sizeof(l->skills));
    /* Investida + até três habilidades compatíveis do catálogo */
    catalog_record_t h;
    int n = 0;
    for (int id = 0; id < catalog_record_count(CAT_SKILLS) && n < COUNT_SKILL_ACTIVE; id++) {
        if (catalog_read(CAT_SKILLS, id, &h) && (h.nature == NAT_NEUTRAL || h.nature == l->nature)) {
            l->skills[n++] = (uint8_t)id;
        }
    }
}

/* Inicia o combate: copia os lutadores, limpa estados que não valem em luta e zera as barras. */
void combat_start(combat_t *c, const fighter_t *player, const fighter_t *opponent, rng_t *rng)
{
    memset(c, 0, sizeof(*c));
    c->side[0] = *player;
    c->side[1] = *opponent;
    c->winner = -1;
    c->rng = rng;
    for (int i = 0; i < 2; i++) {
        fighter_t *l = &c->side[i];
        /* Estados de vida que não valem no combate */
        l->states &= ~(STATE_BIT(ST_HUNGRY) | STATE_BIT(ST_SLEEPING));
        l->atb_total = combat_atb_time(l->attributes[ATTR_INDEX(ATTR_AGI)]);
        l->atb_ms = 0;
    }
}

/* Avança as barras ATB em `dt_ms`. Retorna a máscara dos lados prontos para agir. */
unsigned combat_advance(combat_t *c, int dt_ms)
{
    unsigned ready_mask = 0;
    if (c->finished) {
        return 0;
    }
    for (int i = 0; i < 2; i++) {
        fighter_t *l = &c->side[i];
        if (l->atb_ms < l->atb_total) {
            l->atb_ms = clamp(l->atb_ms + dt_ms, 0, l->atb_total);
        }
        if (l->atb_ms >= l->atb_total) {
            ready_mask |= 1u << i;
        }
    }
    return ready_mask;
}

/* Verdadeiro se o lado está com a barra cheia e o combate não terminou. */
bool combat_ready(const combat_t *c, int side)
{
    return !c->finished && c->side[side].atb_ms >= c->side[side].atb_total;
}

/* Atributo do lutador com os efeitos de Zangado (ATK x1,5 e DEF x0,5). */
static int attribute(const fighter_t *l, effect_target_t a)
{
    int v = l->attributes[ATTR_INDEX(a)];
    if (has(l, ST_ANGRY)) {
        if (a == ATTR_ATK) {
            v = v * 150 / 100;
        } else if (a == ATTR_DEF) {
            v = v * 50 / 100;
        }
    }
    return v;
}

/* Encerra o combate se algum lado ficou sem HP. */
static void check_combat_end(combat_t *c)
{
    for (int i = 0; i < 2; i++) {
        if (c->side[i].hp <= 0) {
            c->side[i].hp = 0;
            c->finished = true;
            c->winner = 1 - i;
            return;
        }
    }
}

/* Sorteia a esquiva: 0,3% por ponto de AGI a mais do defensor, até 35%. */
static bool dodged(combat_t *c, const fighter_t *atk, const fighter_t *def)
{
    int diff = attribute(def, ATTR_AGI) - attribute(atk, ATTR_AGI);
    if (diff <= 0) {
        return false;
    }
    int chance = clamp(diff * DODGE_PER_AGI_X10, 0, DODGE_MAX_X10);
    return rng_range(c->rng, 0, 999) < chance;
}

/* Sorteia o crítico: 5% + 0,4% por ponto de SOR, até 50%. */
static bool critical(combat_t *c, const fighter_t *atk)
{
    int chance = clamp(CRITICAL_BASE_X10 + attribute(atk, ATTR_LUK) * CRITICAL_PER_LUK_X10, 0, CRITICAL_MAX_X10);
    return rng_range(c->rng, 0, 999) < chance;
}

/* Dano físico do README: ATK x 1,2 + FOR x 0,8 - DEF x 0,7 (mínimo 1). */
static int physical_damage(const fighter_t *atk, const fighter_t *def)
{
    int d = (attribute(atk, ATTR_ATK) * 12 + attribute(atk, ATTR_STR) * 8 - attribute(def, ATTR_DEF) * 7) / 10;
    return d < 1 ? 1 : d;
}

/* Dano de habilidade: poder + INT x 0,8 + ATK x 0,4 - DEF x 0,5 (mínimo 1). */
static int skill_damage(const fighter_t *atk, const fighter_t *def, int power)
{
    int d = (power * 10 + attribute(atk, ATTR_INT) * 8 + attribute(atk, ATTR_ATK) * 4 - attribute(def, ATTR_DEF) * 5) / 10;
    return d < 1 ? 1 : d;
}

/* Aplica o estado no alvo com a chance (%) indicada e registra no resultado. */
static void apply_state(combat_t *c, fighter_t *target, uint8_t state, int chance, combat_result_t *r)
{
    if (state < 32 && chance > 0 && rng_chance(c->rng, chance)) {
        target->states |= STATE_BIT(state);
        r->applied_state = state;
    }
}

/* Executa a ação de um lado pronto e descreve o que aconteceu em `r` para a animação. */
void combat_execute(combat_t *c, int side, combat_action_t action, const catalog_record_t *item, combat_result_t *r)
{
    memset(r, 0, sizeof(*r));
    r->actor = side;
    r->target = 1 - side;
    r->type = action.type;
    r->applied_state = -1;
    r->multiplier = 100;
    if (c->finished) {
        return;
    }
    fighter_t *me = &c->side[side], *other = &c->side[1 - side];
    me->atb_ms = 0;

    if (action.type == ACTION_FORFEIT) {
        c->finished = true;
        c->winner = 1 - side;
        return;
    }

    /* Dano dos estados corrosivos no início do turno */
    static const state_t corrosive[] = {ST_POISONED, ST_BURNED, ST_FROZEN};
    for (unsigned i = 0; i < 3; i++) {
        if (has(me, corrosive[i])) {
            int d = me->hp_max * DECAY_HP_STATE_TURN_PCT / 100;
            r->status_damage += d < 1 ? 1 : d;
        }
    }
    me->hp -= r->status_damage;
    check_combat_end(c);
    if (c->finished) {
        return;
    }

    if (has(me, ST_PARALYZED) || has(me, ST_FROZEN)) {
        r->failed = true;
        return;
    }

    if (action.type == ACTION_ITEM) {
        if (!item || has(me, ST_CURSED)) {
            r->failed = true;
            return;
        }
        snprintf(r->icon, sizeof(r->icon), "%s", item->icon);
        r->target = side;
        for (int i = 0; i < item->n_effects; i++) {
            if (item->effects[i].target == VITAL_HP) {
                int before = me->hp;
                me->hp = clamp(me->hp + item->effects[i].value, 0, me->hp_max);
                r->heal += me->hp - before;
            } else if (item->effects[i].target == VITAL_MP) {
                me->mp = clamp(me->mp + item->effects[i].value, 0, me->mp_max);
            }
        }
        for (int i = 0; i < item->n_status; i++) {
            if (item->status[i].state == ST_ALL && item->status[i].value <= -100) {
                me->states &= STATE_BIT(ST_ANGRY);
            } else if (item->status[i].state < 32 && item->status[i].value < 0 && rng_chance(c->rng, -item->status[i].value)) {
                me->states &= ~STATE_BIT(item->status[i].state);
            }
        }
        return;
    }

    /* Confusão: 50% de errar; ao errar, 50% de atingir a si mesmo */
    if (has(me, ST_CONFUSED) && rng_chance(c->rng, 50)) {
        r->missed = true;
        if (rng_chance(c->rng, 50)) {
            r->self_hit = true;
            r->target = side;
            r->damage = physical_damage(me, me);
            me->hp -= r->damage;
            check_combat_end(c);
        }
        return;
    }

    if (action.type == ACTION_ATTACK) {
        snprintf(r->icon, sizeof(r->icon), "attack");
        if (dodged(c, me, other)) {
            r->dodged = true;
            return;
        }
        r->damage = physical_damage(me, other);
        if (critical(c, me)) {
            r->critical = true;
            r->damage = r->damage * 15 / 10;
        }
        other->hp -= r->damage;
        check_combat_end(c);
        return;
    }

    /* Habilidade */
    catalog_record_t h;
    if (has(me, ST_CURSED) || action.position < 0 || action.position >= COUNT_SKILL_ACTIVE ||
        me->skills[action.position] == SLOT_EMPTY || !catalog_read(CAT_SKILLS, me->skills[action.position], &h)) {
        r->failed = true;
        return;
    }
    int cost = clamp(h.mp_cost - attribute(me, ATTR_INT) / 20, 1, 255);
    if (me->mp < cost) {
        r->failed = true;
        return;
    }
    me->mp -= cost;
    snprintf(r->icon, sizeof(r->icon), "%s", h.icon);

    if (h.type == SKILL_HEAL) {
        fighter_t *target = h.target == TARGET_USER ? me : other;
        r->target = h.target == TARGET_USER ? side : 1 - side;
        int before = target->hp;
        target->hp = clamp(target->hp + h.power + attribute(me, ATTR_INT) / 2, 0, target->hp_max);
        r->heal = target->hp - before;
        return;
    }

    fighter_t *target = h.target == TARGET_USER ? me : other;
    r->target = h.target == TARGET_USER ? side : 1 - side;
    if (target != me && dodged(c, me, target)) {
        r->dodged = true;
        return;
    }
    if (h.type == SKILL_ATTACK) {
        r->multiplier = combat_multiplier((nature_t)h.nature, (nature_t)target->nature);
        r->damage = skill_damage(me, target, h.power) * r->multiplier / 100;
        if (r->damage < 1) {
            r->damage = 1;
        }
        if (critical(c, me)) {
            r->critical = true;
            r->damage = r->damage * 15 / 10;
        }
        target->hp -= r->damage;
    }
    for (int i = 0; i < h.n_status; i++) {
        apply_state(c, target, h.status[i].state, h.status[i].value, r);
    }
    check_combat_end(c);
}

/* Escolhe a ação do oponente: cura com HP baixo, às vezes uma habilidade, senão ataca. */
combat_action_t combat_ai(combat_t *c, int side)
{
    fighter_t *me = &c->side[side];
    catalog_record_t h;
    combat_action_t action = {ACTION_ATTACK, 0};
    int heal = -1, attacks[COUNT_SKILL_ACTIVE], n_attacks = 0;
    for (int i = 0; i < COUNT_SKILL_ACTIVE; i++) {
        if (me->skills[i] == SLOT_EMPTY || !catalog_read(CAT_SKILLS, me->skills[i], &h)) {
            continue;
        }
        int cost = clamp(h.mp_cost - me->attributes[ATTR_INDEX(ATTR_INT)] / 20, 1, 255);
        if (cost > me->mp) {
            continue;
        }
        if (h.type == SKILL_HEAL) {
            heal = i;
        } else {
            attacks[n_attacks++] = i;
        }
    }
    if (heal >= 0 && me->hp * 100 < me->hp_max * 30) {
        action.type = ACTION_SKILL;
        action.position = heal;
    } else if (n_attacks && rng_chance(c->rng, 40)) {
        action.type = ACTION_SKILL;
        action.position = attacks[rng_range(c->rng, 0, n_attacks - 1)];
    }
    return action;
}

/* XP ganho pelo vencedor: 10 + 5 x nível do perdedor. */
uint32_t combat_xp(const combat_t *c, int winning_side)
{
    return 10u + 5u * c->side[1 - winning_side].level;
}

/* Sorteia um item dropado para o vencedor (10% + SOR/2). Retorna o id ou -1. */
int combat_drop(const combat_t *c, int winning_side)
{
    int chance = DROP_BASE_PCT + c->side[winning_side].attributes[ATTR_INDEX(ATTR_LUK)] / 2;
    int n = catalog_record_count(CAT_ITEMS);
    if (n == 0 || !rng_chance(c->rng, chance)) {
        return -1;
    }
    return rng_range(c->rng, 0, n - 1);
}
