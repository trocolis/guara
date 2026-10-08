/*
 * Modelo do Guará e regras do jogo: nascimento, ciclo de vida minuto a minuto,
 * recuperação temporal, itens, afinidades, experiência, evolução e equipamentos.
 * Os números de balanceamento estão em rules.h. Não depende de hardware.
 */
#include "guara.h"

#include <string.h>

#include "rules.h"

static const uint8_t LEVEL_STAGE[COUNT_STAGES] = {1, 5, 15, 30, 50};

/* ------------------------------------------------------------------------- */
/* Tabelas de nomes e ícones                                                 */
/* ------------------------------------------------------------------------- */

static const char *const NAMES_NATURE[COUNT_NATURES] = {
    "Fogo", "Terra", "Água", "Ar", "Neutra", "Eletricidade", "Planta",
    "Gelo", "Vapor", "Cristal", "Sombra", "Luz", "Psíquica", "Lava",
};

static const char *const ICONS_NATURE[COUNT_NATURES] = {
    "fire", "earth", "water", "air", "neutral", "electricity", "plant",
    "ice", "steam", "crystal", "shadow", "light", "psychic", "lava",
};

const char *nature_name(nature_t n) { return n < COUNT_NATURES ? NAMES_NATURE[n] : "?"; }
const char *nature_icon(nature_t n) { return n < COUNT_NATURES ? ICONS_NATURE[n] : "neutral"; }

/* Nome do estado para exibição na interface. */
const char *state_name(state_t e)
{
    switch (e) {
    case ST_SLEEPING: return "Dormindo";
    case ST_FAINTED: return "Desmaiado";
    case ST_POISONED: return "Envenenado";
    case ST_PARALYZED: return "Paralisado";
    case ST_BURNED: return "Queimado";
    case ST_FROZEN: return "Congelado";
    case ST_CONFUSED: return "Confuso";
    case ST_CURSED: return "Amaldiçoado";
    case ST_TIRED: return "Cansado";
    case ST_HUNGRY: return "Faminto";
    case ST_HAPPY: return "Feliz";
    case ST_SAD: return "Triste";
    case ST_ANGRY: return "Zangado";
    case ST_BORED: return "Entediado";
    default: return "Normal";
    }
}

/* Nome do ícone (VROM) do estado, ou NULL se não houver. */
const char *state_icon(state_t e)
{
    switch (e) {
    case ST_SLEEPING: return "sleeping";
    case ST_FAINTED: return "fainted";
    case ST_POISONED: return "poisoned";
    case ST_PARALYZED: return "paralyzed";
    case ST_BURNED: return "burning";
    case ST_FROZEN: return "frozen";
    case ST_CONFUSED: return "confused";
    case ST_CURSED: return "cursed";
    case ST_TIRED: return "tired";
    case ST_HUNGRY: return "hungry";
    case ST_HAPPY: return "happy";
    case ST_SAD: return "sad";
    case ST_ANGRY: return "angry";
    case ST_BORED: return "bored";
    default: return NULL;
    }
}

#define E_FIRE (1u << NAT_FIRE)
#define E_EARTH (1u << NAT_EARTH)
#define E_WATER (1u << NAT_WATER)
#define E_AIR (1u << NAT_AIR)

/* Elementos primários que compõem a natureza (bits de NAT_FIRE..NAT_AIR). */
unsigned nature_elements(nature_t n)
{
    switch (n) {
    case NAT_FIRE: return E_FIRE;
    case NAT_EARTH: return E_EARTH;
    case NAT_WATER: return E_WATER;
    case NAT_AIR: return E_AIR;
    case NAT_LAVA: return E_FIRE | E_EARTH;
    case NAT_ELECTRICITY: return E_FIRE | E_AIR;
    case NAT_STEAM: return E_FIRE | E_WATER;
    case NAT_CRYSTAL: return E_EARTH | E_AIR;
    case NAT_PLANT: return E_EARTH | E_WATER;
    case NAT_ICE: return E_AIR | E_WATER;
    default: return 0;
    }
}

/* Natureza secundária formada pela fusão de duas naturezas primárias. */
static nature_t fusion(nature_t a, nature_t b)
{
    unsigned e = nature_elements(a) | nature_elements(b);
    for (int n = NAT_ELECTRICITY; n < COUNT_NATURES; n++) {
        if (nature_elements((nature_t)n) == e) {
            return (nature_t)n;
        }
    }
    return a;
}

/* ------------------------------------------------------------------------- */
/* Utilidades                                                                */
/* ------------------------------------------------------------------------- */

/* Limita `v` ao intervalo [min, max]. */
static int clamp(int v, int min, int max)
{
    return v < min ? min : (v > max ? max : v);
}

/* Soma `delta` a um vital percentual mantendo-o entre 0 e 100%. */
static void add_to_vital(uint16_t *v, int delta)
{
    *v = (uint16_t)clamp(*v + delta, 0, VITAL_MAX);
}

/* Raiz quadrada inteira (evita depender da libm). */
static uint32_t root(uint32_t n)
{
    uint32_t r = 0;
    while ((r + 1) * (r + 1) <= n) {
        r++;
    }
    return r;
}

/* Liga ou desliga um estado do Guará. */
void guara_set_state(guara_t *g, state_t e, bool enabled)
{
    if (enabled) {
        g->states |= STATE_BIT(e);
    } else {
        g->states &= ~STATE_BIT(e);
    }
}

/* Move um medidor oculto mantendo-o na faixa ±10000. */
void guara_add_gauge(guara_t *g, gauge_t m, int delta)
{
    g->gauges[m] = (int16_t)clamp(g->gauges[m] + delta, -GAUGE_MAX, GAUGE_MAX);
}

/* Estágio de evolução correspondente a um nível. */
stage_t guara_stage_for_level(uint8_t level)
{
    stage_t e = STAGE_BABY;
    for (int i = 0; i < COUNT_STAGES; i++) {
        if (level >= LEVEL_STAGE[i]) {
            e = (stage_t)i;
        }
    }
    return e;
}

/* Recalcula HP e MP máximos a partir do nível e dos atributos. */
static void recalc_max(guara_t *g)
{
    g->hp_max = (uint16_t)(HP_BASE + HP_PER_LEVEL * g->level + g->attributes[ATTR_INDEX(ATTR_DEF)]);
    g->mp_max = (uint16_t)(MP_BASE + MP_PER_LEVEL * g->level + g->attributes[ATTR_INDEX(ATTR_INT)] / 2);
    if (g->hp > g->hp_max) {
        g->hp = g->hp_max;
    }
    if (g->mp > g->mp_max) {
        g->mp = g->mp_max;
    }
}

/* Atributos favorecidos por cada natureza primária. */
static void nature_attributes(nature_t n, effect_target_t *a, effect_target_t *b)
{
    switch (n) {
    case NAT_FIRE: *a = ATTR_ATK; *b = ATTR_STR; break;
    case NAT_EARTH: *a = ATTR_DEF; *b = ATTR_STR; break;
    case NAT_WATER: *a = ATTR_INT; *b = ATTR_DEF; break;
    case NAT_AIR: *a = ATTR_AGI; *b = ATTR_LUK; break;
    default: *a = ATTR_INT; *b = ATTR_LUK; break;
    }
}

/* ------------------------------------------------------------------------- */
/* Habilidades                                                               */
/* ------------------------------------------------------------------------- */

/* Verdadeiro se a habilidade combina com a natureza do Guará. */
static bool skill_compatible(const guara_t *g, const catalog_record_t *h)
{
    if (h->nature == NAT_NEUTRAL || h->nature == g->nature) {
        return true;
    }
    return (nature_elements((nature_t)h->nature) & nature_elements((nature_t)g->nature)) != 0;
}

/* Coloca a habilidade num slot ativo livre (se ainda não estiver ativa). */
static void activate_skill(guara_t *g, int id)
{
    for (int i = 0; i < COUNT_SKILL_ACTIVE; i++) {
        if (g->active_skills[i] == id) {
            return;
        }
    }
    for (int i = 0; i < COUNT_SKILL_ACTIVE; i++) {
        if (g->active_skills[i] == SLOT_EMPTY) {
            g->active_skills[i] = (uint8_t)id;
            return;
        }
    }
}

/* Aprende a próxima habilidade compatível do catálogo. Retorna o id ou -1. */
static int learn_next_skill(guara_t *g)
{
    catalog_record_t h;
    for (int id = 0; id < catalog_record_count(CAT_SKILLS) && id < 64; id++) {
        if (g->owned_skills & (1ull << id)) {
            continue;
        }
        if (catalog_read(CAT_SKILLS, id, &h) && skill_compatible(g, &h)) {
            g->owned_skills |= 1ull << id;
            activate_skill(g, id);
            return id;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------------- */
/* Nascimento                                                                */
/* ------------------------------------------------------------------------- */

/* Cria um Guará novo: sorteia aparência e natureza, define atributos e habilidades iniciais. */
void guara_birth(guara_t *g, const char *name, rng_t *rng, uint32_t now)
{
    memset(g, 0, sizeof(*g));
    strncpy(g->name, name, SIZE_NAME);
    g->uid = rng_next(rng);
    g->head = (uint8_t)rng_range(rng, 1, 9);
    g->color = (uint8_t)rng_range(rng, COLOR_BLACK, COLOR_WHITE);
    g->birth_nature = (uint8_t)rng_range(rng, NAT_FIRE, NAT_AIR);
    g->nature = g->birth_nature;
    g->stage = STAGE_BABY;
    g->level = 1;

    for (int i = 0; i < ATTRIBUTE_COUNT; i++) {
        g->attributes[i] = ATTR_INITIAL;
    }
    effect_target_t a, b;
    nature_attributes((nature_t)g->nature, &a, &b);
    g->attributes[ATTR_INDEX(a)] += ATTR_NATURE_BONUS;
    g->attributes[ATTR_INDEX(b)] += ATTR_NATURE_BONUS;
    recalc_max(g);
    g->hp = g->hp_max;
    g->mp = g->mp_max;

    g->energy = VITAL_MAX;
    g->mood = CENTI(80);
    g->satiety = CENTI(70);
    g->health = VITAL_MAX;

    memset(g->equipped, SLOT_EMPTY, sizeof(g->equipped));
    memset(g->active_skills, SLOT_EMPTY, sizeof(g->active_skills));

    /* Kit inicial de itens: a definir no CATALOG.md (README > Reiniciar) */
    /* Primeira habilidade compatível do catálogo e o primeiro ataque da natureza */
    learn_next_skill(g);
    catalog_record_t h;
    for (int id = 0; id < catalog_record_count(CAT_SKILLS) && id < 64; id++) {
        if (catalog_read(CAT_SKILLS, id, &h) && h.nature == g->nature && h.type == SKILL_ATTACK) {
            g->owned_skills |= 1ull << id;
            activate_skill(g, id);
            break;
        }
    }

    g->timestamp = now;
}

/* ------------------------------------------------------------------------- */
/* Ciclo de vida                                                             */
/* ------------------------------------------------------------------------- */

/* Desmaia: zera o HP, cura os estados que acabam com o desmaio e penaliza o cuidado. */
static void faint(guara_t *g)
{
    g->hp = 0;
    g->states &= ~(STATE_BIT(ST_SLEEPING) | STATE_BIT(ST_POISONED) | STATE_BIT(ST_BURNED) |
                    STATE_BIT(ST_FROZEN) | STATE_BIT(ST_CURSED) | STATE_BIT(ST_PARALYZED) |
                    STATE_BIT(ST_CONFUSED));
    guara_set_state(g, ST_FAINTED, true);
    g->minutes_fainted = 0;
    guara_add_gauge(g, GAUGE_CARE, -500);
}

/* Tira `pct`% do HP máximo (pelo menos 1 ponto). */
static void lose_hp_percent(guara_t *g, int pct)
{
    int damage = g->hp_max * pct / 100;
    if (damage < 1) {
        damage = 1;
    }
    g->hp = (uint16_t)clamp(g->hp - damage, 0, g->hp_max);
}

/* Recalcula a emoção atual a partir dos vitais e do medidor Raiva x Tristeza. */
static void update_emotion(guara_t *g)
{
    g->states &= ~MASK_EMOTIONAL;
    if (guara_has_state(g, ST_FAINTED) || guara_has_state(g, ST_SLEEPING)) {
        return;
    }
    if (g->energy > LIMIT_HAPPY && g->mood > LIMIT_HAPPY && g->satiety > LIMIT_HAPPY && g->health > LIMIT_HAPPY) {
        guara_set_state(g, ST_HAPPY, true);
    } else if (g->mood < LIMIT_LOW_MOOD) {
        int anger = g->gauges[GAUGE_ANGER];
        if (anger > GAUGE_THRESHOLD_EMOTION) {
            guara_set_state(g, ST_ANGRY, true);
        } else if (anger < -GAUGE_THRESHOLD_EMOTION) {
            guara_set_state(g, ST_SAD, true);
        } else {
            guara_set_state(g, ST_BORED, true);
        }
    } else if (g->idle_minutes > LIMIT_IDLE_MIN) {
        guara_set_state(g, ST_BORED, true);
    }
}

/* Avança um minuto do ciclo de vida (desgaste, estados, sono, medidores, emoção). */
void guara_tick_minute(guara_t *g, const clock_ctx_t *clock_ctx, bool protected)
{
    g->age_minutes++;

    if (guara_has_state(g, ST_FAINTED)) {
        g->minutes_fainted++;
        if (g->minutes_fainted >= FAINT_RECOVER_MIN) {
            guara_set_state(g, ST_FAINTED, false);
            g->hp = (uint16_t)(g->hp_max / 10 ? g->hp_max / 10 : 1);
        }
        return;
    }

    bool sleeping = guara_has_state(g, ST_SLEEPING);

    /* Multiplicador de desgaste (%): saúde alta desacelera, tristeza/tédio aceleram. */
    int mult = 150 - g->health * 100 / VITAL_MAX;
    if (guara_has_state(g, ST_SAD)) {
        mult += 20;
    }
    if (guara_has_state(g, ST_BORED)) {
        mult += 5;
    }

    add_to_vital(&g->satiety, -(DECAY_SATIETY * mult / 100) / (sleeping ? 2 : 1));
    if (sleeping) {
        add_to_vital(&g->energy, REGEN_ENERGY_SLEEP);
    } else {
        add_to_vital(&g->energy, -(DECAY_ENERGY * mult / 100));
        g->idle_minutes = (uint16_t)clamp(g->idle_minutes + 1, 0, 60000);
    }

    if (g->energy < LIMIT_MOOD_DROPS || g->satiety < LIMIT_MOOD_DROPS) {
        add_to_vital(&g->mood, -(DECAY_MOOD * mult / 100));
    } else if (g->energy > LIMIT_MOOD_RISES && g->satiety > LIMIT_MOOD_RISES) {
        add_to_vital(&g->mood, REGEN_MOOD);
    }
    if (!sleeping && g->idle_minutes > LIMIT_IDLE_MIN) {
        add_to_vital(&g->mood, -DECAY_MOOD_IDLE);
    }

    if (g->energy < LIMIT_HEALTH_DROPS || g->satiety < LIMIT_HEALTH_DROPS) {
        add_to_vital(&g->health, -(DECAY_HEALTH * mult / 100));
    } else if (g->satiety > LIMIT_HEALTH_RISES && g->energy > LIMIT_HEALTH_RISES && g->mood > LIMIT_HEALTH_RISES) {
        add_to_vital(&g->health, REGEN_HEALTH * (guara_has_state(g, ST_HAPPY) ? 120 : 100) / 100);
    }

    /* Estados físicos derivados dos vitais */
    bool hungry = g->satiety <= LIMIT_HUNGER;
    guara_set_state(g, ST_HUNGRY, hungry);
    if (hungry) {
        lose_hp_percent(g, DECAY_HP_HUNGER_PCT);
    }
    static const state_t corrosive[] = {ST_POISONED, ST_BURNED, ST_FROZEN};
    for (unsigned i = 0; i < sizeof(corrosive) / sizeof(corrosive[0]); i++) {
        if (guara_has_state(g, corrosive[i])) {
            lose_hp_percent(g, DECAY_HP_STATE_PCT);
            add_to_vital(&g->health, -DECAY_HEALTH_STATE);
        }
    }

    /* Sono */
    if (sleeping) {
        bool is_wake_time = clock_ctx && clock_ctx->minute_of_day == clock_ctx->wake_minute && g->energy > LIMIT_WAKE;
        if (hungry || g->energy >= VITAL_MAX || is_wake_time) {
            guara_set_state(g, ST_SLEEPING, false);
        }
    } else if (!hungry) {
        bool is_bedtime = clock_ctx && clock_ctx->minute_of_day >= 0 && clock_ctx->minute_of_day == clock_ctx->bedtime_minute;
        if (g->energy == 0 || is_bedtime) {
            guara_set_state(g, ST_SLEEPING, true);
        }
    }
    sleeping = guara_has_state(g, ST_SLEEPING);
    guara_set_state(g, ST_TIRED, !sleeping && g->energy < LIMIT_TIRED);

    /* Travas da recuperação temporal */
    if (protected) {
        int floor_value = g->hp_max * LOCK_HP_PCT / 100;
        if (g->hp < floor_value) {
            g->hp = (uint16_t)(floor_value ? floor_value : 1);
        }
        uint16_t *vitals[] = {&g->energy, &g->mood, &g->satiety, &g->health};
        for (int i = 0; i < 4; i++) {
            if (*vitals[i] < LOCK_VITAL) {
                *vitals[i] = LOCK_VITAL;
            }
        }
    }

    if (g->hp == 0) {
        faint(g);
        return;
    }

    /* Cuidado × Negligência; o descaso também move para a Raiva */
    bool neglect = hungry || guara_has_state(g, ST_TIRED) || g->mood < CENTI(20) || g->health < CENTI(20);
    if (neglect) {
        guara_add_gauge(g, GAUGE_CARE, -GAUGE_STEP_CARE);
        guara_add_gauge(g, GAUGE_ANGER, GAUGE_STEP_ANGER);
    } else if (g->satiety > CENTI(50) && g->energy > CENTI(50) && g->mood > CENTI(50)) {
        guara_add_gauge(g, GAUGE_CARE, GAUGE_STEP_CARE);
    }

    update_emotion(g);
}

/* Simula os minutos passados desde o último registro, com as travas de proteção. */
uint32_t guara_catch_up_time(guara_t *g, uint32_t now, int minute_of_day_now, int bedtime_minute, int wake_minute)
{
    if (g->timestamp == 0 || now <= g->timestamp) {
        g->timestamp = now;
        return 0;
    }
    uint32_t delta = (now - g->timestamp) / 60;
    uint32_t simulate = delta > DELTA_MAX_MIN ? DELTA_MAX_MIN : delta;
    clock_ctx_t clock_ctx = {.minute_of_day = -1, .bedtime_minute = bedtime_minute, .wake_minute = wake_minute};
    for (uint32_t i = 0; i < simulate; i++) {
        if (minute_of_day_now >= 0) {
            int32_t m = minute_of_day_now - (int32_t)(simulate - 1 - i);
            clock_ctx.minute_of_day = (int)(((m % 1440) + 1440) % 1440);
        }
        guara_tick_minute(g, &clock_ctx, true);
    }
    g->timestamp += delta * 60;
    return simulate;
}

/* Verdadeiro se o minuto do dia está na janela [inicio, inicio + duracao), com virada da meia-noite. */
static bool in_time_window(int minute, int start, int duration)
{
    int d = ((minute - start) % 1440 + 1440) % 1440;
    return d < duration;
}

/* Registra uma interação: zera o ócio e move o medidor Dia x Noite conforme a hora. */
void guara_interact(guara_t *g, const clock_ctx_t *clock_ctx)
{
    g->idle_minutes = 0;
    if (!clock_ctx || clock_ctx->minute_of_day < 0) {
        return;
    }
    if (in_time_window(clock_ctx->minute_of_day, clock_ctx->wake_minute, 120)) {
        guara_add_gauge(g, GAUGE_DAY, GAUGE_STEP_DAY);
    } else if (in_time_window(clock_ctx->minute_of_day, clock_ctx->bedtime_minute - 120, 120)) {
        guara_add_gauge(g, GAUGE_DAY, -GAUGE_STEP_DAY);
    } else if (g->gauges[GAUGE_DAY] > 0) {
        guara_add_gauge(g, GAUGE_DAY, -(g->gauges[GAUGE_DAY] < GAUGE_STEP_DAY / 2 ? g->gauges[GAUGE_DAY] : GAUGE_STEP_DAY / 2));
    } else if (g->gauges[GAUGE_DAY] < 0) {
        guara_add_gauge(g, GAUGE_DAY, -g->gauges[GAUGE_DAY] < GAUGE_STEP_DAY / 2 ? -g->gauges[GAUGE_DAY] : GAUGE_STEP_DAY / 2);
    }
}

/* ------------------------------------------------------------------------- */
/* Itens                                                                     */
/* ------------------------------------------------------------------------- */

/* Verdadeiro para versões 0.x.x (regra de desenvolvimento do README). */
bool guara_is_development_version(const char *version)
{
    return version && version[0] == '0' && version[1] == '.';
}

/* Máscara com um bit para cada registro existente no catálogo. */
static uint64_t catalog_mask(catalog_t cat)
{
    int n = catalog_record_count(cat);
    return n >= 64 ? ~0ull : (1ull << n) - 1;
}

/* Libera todo o catálogo: 255 unidades de cada item e todos os equipamentos e habilidades. */
void guara_unlock_catalog(guara_t *g)
{
    int n = catalog_record_count(CAT_ITEMS);
    for (int id = 0; id < n && id < COUNT_ITEMS; id++) {
        g->inventory[id] = 255;
    }
    g->owned_equipment |= catalog_mask(CAT_EQUIPMENT);
    g->owned_skills |= catalog_mask(CAT_SKILLS);
}

/* Verdadeiro se o item recupera HP (único tipo aceito por um Guará desmaiado). */
static bool item_heal_hp(const catalog_record_t *item)
{
    for (int i = 0; i < item->n_effects; i++) {
        if (item->effects[i].target == VITAL_HP && item->effects[i].value > 0) {
            return true;
        }
    }
    return false;
}

/* Verifica se o item pode ser usado agora (estoque, maldição, desmaio). */
item_use_result_t guara_can_use_item(const guara_t *g, const catalog_record_t *item)
{
    if (item->id >= COUNT_ITEMS) {
        return USE_INVALID;
    }
    if (g->inventory[item->id] == 0) {
        return USE_NO_STOCK;
    }
    if (guara_has_state(g, ST_CURSED)) {
        return USE_BLOCKED_CURSE;
    }
    if (guara_has_state(g, ST_FAINTED) && (g->minutes_fainted < FAINT_LOCKOUT_MIN || !item_heal_hp(item))) {
        return USE_BLOCKED_FAINTED;
    }
    return USE_OK;
}

/* Aplica o sobre-efeito de um item: -100 cura, negativo é chance de curar, positivo de causar. */
static void apply_status_effect(guara_t *g, uint8_t state, int value, rng_t *rng)
{
    if (state == ST_ALL) {
        if (value <= -100) {
            g->states &= ~(MASK_PHYSICAL & ~(STATE_BIT(ST_SLEEPING) | STATE_BIT(ST_FAINTED)));
        }
        return;
    }
    if (state >= 32) {
        return;
    }
    if (value <= -100 || (value < 0 && rng_chance(rng, -value))) {
        guara_set_state(g, (state_t)state, false);
    } else if (value > 0 && rng_chance(rng, value)) {
        guara_set_state(g, (state_t)state, true);
    }
}

/* Usa um item: consome do inventário e aplica efeitos, sobre-efeitos e afinidade. */
item_use_result_t guara_use_item(guara_t *g, const catalog_record_t *item, rng_t *rng)
{
    item_use_result_t can = guara_can_use_item(g, item);
    if (can != USE_OK) {
        return can;
    }
    g->inventory[item->id]--;

    for (int i = 0; i < item->n_effects; i++) {
        int v = item->effects[i].value;
        switch (item->effects[i].target) {
        case VITAL_HP:
            g->hp = (uint16_t)clamp(g->hp + v, 0, g->hp_max);
            break;
        case VITAL_MP:
            g->mp = (uint16_t)clamp(g->mp + v, 0, g->mp_max);
            break;
        case VITAL_ENERGY:
            add_to_vital(&g->energy, CENTI(v));
            break;
        case VITAL_MOOD:
            add_to_vital(&g->mood, CENTI(v));
            break;
        case VITAL_SATIETY:
            add_to_vital(&g->satiety, CENTI(v));
            break;
        case VITAL_HEALTH:
            add_to_vital(&g->health, CENTI(v));
            break;
        default:
            break;
        }
    }
    for (int i = 0; i < item->n_status; i++) {
        apply_status_effect(g, item->status[i].state, item->status[i].value, rng);
    }

    if (guara_has_state(g, ST_FAINTED) && g->hp > 0) {
        guara_set_state(g, ST_FAINTED, false);
    }
    if (g->satiety > LIMIT_HUNGER) {
        guara_set_state(g, ST_HUNGRY, false);
    }
    /* Acordado para comer: volta a dormir se a energia ainda não passou de 50% */
    if (guara_has_state(g, ST_SLEEPING) && g->energy > LIMIT_WAKE) {
        guara_set_state(g, ST_SLEEPING, false);
    }
    if (item->type == ITEM_FOOD || item->type == ITEM_DRINK) {
        guara_add_affinity(g, (nature_t)item->nature, AFFINITY_PER_ITEM);
        guara_add_gauge(g, GAUGE_CARE, GAUGE_STEP_CARE * 50);
    }
    update_emotion(g);
    return USE_OK;
}

/* ------------------------------------------------------------------------- */
/* Afinidades e evolução                                                     */
/* ------------------------------------------------------------------------- */

/* O sistema de afinidade só vale enquanto o Guará mantém a natureza primária de nascimento. */
bool guara_affinity_enabled(const guara_t *g)
{
    return g->nature == g->birth_nature && g->nature <= NAT_AIR;
}

/* Soma pontos de afinidade; o elemento oposto perde o mesmo valor. */
void guara_add_affinity(guara_t *g, nature_t element, int centi)
{
    if (!guara_affinity_enabled(g) || element >= COUNT_AFFINITIES) {
        return;
    }
    if (element == g->birth_nature) {
        element = NAT_NEUTRAL; /* pontos da natureza de nascimento vão para o neutro */
    }
    add_to_vital(&g->affinities[element], centi);
    nature_t opposite;
    switch (element) {
    case NAT_FIRE: opposite = NAT_WATER; break;
    case NAT_WATER: opposite = NAT_FIRE; break;
    case NAT_EARTH: opposite = NAT_AIR; break;
    case NAT_AIR: opposite = NAT_EARTH; break;
    default: return;
    }
    add_to_vital(&g->affinities[opposite], -centi);
}

/* Decide a natureza secundária ao mudar de estágio (fusão, Luz, Sombra, Psíquica ou Neutra). */
static void evolve_nature(guara_t *g)
{
    if (!guara_affinity_enabled(g)) {
        return;
    }
    int dominant = NAT_NEUTRAL;
    for (int i = 0; i < COUNT_AFFINITIES; i++) {
        if (g->affinities[i] > g->affinities[dominant]) {
            dominant = i;
        }
    }
    if (dominant != NAT_NEUTRAL) {
        if (g->affinities[dominant] >= EVO_AFFINITY_FUSION && dominant != g->birth_nature) {
            g->nature = (uint8_t)fusion((nature_t)g->birth_nature, (nature_t)dominant);
        }
        return;
    }
    int day = g->gauges[GAUGE_DAY];
    if (day >= EVO_DAY_NIGHT) {
        g->nature = NAT_LIGHT;
        return;
    }
    if (day <= -EVO_DAY_NIGHT) {
        g->nature = NAT_SHADOW;
        return;
    }
    int intelligence = g->attributes[ATTR_INDEX(ATTR_INT)], other = 0;
    for (int i = 0; i < ATTRIBUTE_COUNT; i++) {
        if (i != ATTR_INDEX(ATTR_INT) && g->attributes[i] > other) {
            other = g->attributes[i];
        }
    }
    if (intelligence * 10 >= other * EVO_INT_FACTOR_X10) {
        g->nature = NAT_PSYCHIC;
    } else if (g->stage == STAGE_ADULT) {
        g->nature = NAT_NEUTRAL;
    }
}

/* ------------------------------------------------------------------------- */
/* Experiência                                                               */
/* ------------------------------------------------------------------------- */

/* XP necessário para passar do nível atual: 20 x nível^1,5. */
uint32_t guara_xp_to_next_level(uint8_t level)
{
    /* 20 * n^1,5 */
    return 20u * level * root(level * 100u) / 10u;
}

/* Bônus (%) de XP pela idade do Guará. */
int guara_age_bonus(const guara_t *g)
{
    uint32_t days = g->age_minutes / 1440;
    return days < 3 ? 125 : (days < 30 ? 100 : 90);
}

/* Sobe um nível: atributos, HP/MP, estágio, evolução de natureza e novas habilidades. */
static void raise_level(guara_t *g, rng_t *rng)
{
    g->level++;
    effect_target_t a, b;
    nature_attributes((nature_t)g->birth_nature, &a, &b);
    for (int i = 0; i < ATTRIBUTE_COUNT; i++) {
        int gain = rng_range(rng, 0, 1);
        if (i == ATTR_INDEX(a) || i == ATTR_INDEX(b)) {
            gain++;
        }
        g->attributes[i] = (uint16_t)clamp(g->attributes[i] + gain, 0, 999);
    }
    uint16_t hp_before = g->hp_max, mp_before = g->mp_max;
    recalc_max(g);
    g->hp = (uint16_t)clamp(g->hp + (g->hp_max - hp_before), 0, g->hp_max);
    g->mp = (uint16_t)clamp(g->mp + (g->mp_max - mp_before), 0, g->mp_max);

    stage_t e = guara_stage_for_level(g->level);
    if (e > g->stage) {
        g->stage = (uint8_t)e;
        if (e >= STAGE_YOUNG_B) {
            evolve_nature(g);
        }
    }
    if (g->level % LEVELS_PER_SKILL == 0) {
        learn_next_skill(g);
    }
}

/* Concede XP (com bônus de idade) e sobe quantos níveis couberem. Retorna os níveis ganhos. */
int guara_gain_xp(guara_t *g, uint32_t points, rng_t *rng)
{
    if (g->level >= LEVEL_MAX) {
        return 0;
    }
    g->xp += points * (uint32_t)guara_age_bonus(g) / 100u;
    int rose = 0;
    while (g->level < LEVEL_MAX && g->xp >= guara_xp_to_next_level(g->level)) {
        g->xp -= guara_xp_to_next_level(g->level);
        raise_level(g, rng);
        rose++;
    }
    if (g->level >= LEVEL_MAX) {
        g->xp = 0;
    }
    return rose;
}

/* ------------------------------------------------------------------------- */
/* Atributos e equipamentos                                                  */
/* ------------------------------------------------------------------------- */

/* Tipo de equipamento aceito por cada um dos 7 slots. */
int guara_slot_type(int slot)
{
    static const uint8_t types[COUNT_SLOTS_EQUIP] = {EQ_HEAD, EQ_BODY, EQ_HAND_ATK, EQ_HAND_DEF, EQ_FEET, EQ_ACCESSORY, EQ_ACCESSORY};
    return slot >= 0 && slot < COUNT_SLOTS_EQUIP ? types[slot] : -1;
}

/* Atributo com os bônus dos equipamentos e a penalidade de Cansado. */
int guara_effective_attribute(const guara_t *g, effect_target_t attr)
{
    int v = g->attributes[ATTR_INDEX(attr)];
    catalog_record_t eq;
    for (int s = 0; s < COUNT_SLOTS_EQUIP; s++) {
        if (g->equipped[s] == SLOT_EMPTY || !catalog_read(CAT_EQUIPMENT, g->equipped[s], &eq)) {
            continue;
        }
        for (int i = 0; i < eq.n_effects; i++) {
            if (eq.effects[i].target == attr) {
                v += eq.effects[i].value;
            }
        }
    }
    if (guara_has_state(g, ST_TIRED)) {
        v = v * 70 / 100;
    }
    return v < 1 ? 1 : v;
}

/* Equipa um equipamento possuído no slot (tira de outro slot se já estiver equipado). */
bool guara_equip(guara_t *g, int slot, int equip_id)
{
    catalog_record_t eq;
    if (slot < 0 || slot >= COUNT_SLOTS_EQUIP || equip_id < 0 || equip_id >= 64 || !(g->owned_equipment & (1ull << equip_id))) {
        return false;
    }
    if (catalog_read(CAT_EQUIPMENT, equip_id, &eq) && eq.type != guara_slot_type(slot)) {
        return false;
    }
    for (int s = 0; s < COUNT_SLOTS_EQUIP; s++) {
        if (g->equipped[s] == equip_id) {
            g->equipped[s] = SLOT_EMPTY;
        }
    }
    g->equipped[slot] = (uint8_t)equip_id;
    return true;
}

/* Esvazia o slot. */
void guara_unequip(guara_t *g, int slot)
{
    if (slot >= 0 && slot < COUNT_SLOTS_EQUIP) {
        g->equipped[slot] = SLOT_EMPTY;
    }
}

/* ------------------------------------------------------------------------- */
/* Exibição                                                                  */
/* ------------------------------------------------------------------------- */

/* Emoção atual do Guará (estado emocional), ou -1. */
int guara_main_emotion(const guara_t *g)
{
    static const state_t order[] = {ST_ANGRY, ST_SAD, ST_BORED, ST_HAPPY};
    for (unsigned i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        if (guara_has_state(g, order[i])) {
            return order[i];
        }
    }
    return -1;
}

/* Estado físico mais importante para exibir no balão, ou -1. */
int guara_main_state(const guara_t *g)
{
    static const state_t order[] = {ST_FAINTED, ST_SLEEPING, ST_HUNGRY, ST_POISONED, ST_BURNED,
                                     ST_FROZEN, ST_PARALYZED, ST_CONFUSED, ST_CURSED, ST_TIRED};
    for (unsigned i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        if (guara_has_state(g, order[i])) {
            return order[i];
        }
    }
    return -1;
}
