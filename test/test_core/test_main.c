#include <string.h>
#include <unity.h>

#include "../common/test_storage.h"
#include "combat.h"
#include "link_proto.h"
#include "guara.h"
#include "rules.h"
#include "save.h"

static rng_t rng;

void setUp(void) { rng_seed(&rng, 1234); }
void tearDown(void) {}

#define REQUIRE_STORAGE() \
    if (!load_test_storage()) TEST_IGNORE_MESSAGE("rode tools/storage-packer/pack_storage.py")

/* ---------------- Storage ---------------- */

static void test_storage_catalog_and_vrom(void)
{
    REQUIRE_STORAGE();
    TEST_ASSERT_EQUAL_INT(2, catalog_record_count(CAT_ITEMS));
    catalog_record_t apple;
    TEST_ASSERT_TRUE(catalog_read(CAT_ITEMS, 0, &apple));
    TEST_ASSERT_EQUAL_STRING("test_food", apple.slug);
    TEST_ASSERT_EQUAL_STRING("Comida de teste", apple.name);
    TEST_ASSERT_EQUAL_INT(3, apple.n_effects);
    TEST_ASSERT_EQUAL_INT(VITAL_SATIETY, apple.effects[0].target);
    TEST_ASSERT_EQUAL_INT(15, apple.effects[0].value);
    TEST_ASSERT_EQUAL_INT(1, catalog_find_slug(CAT_ITEMS, "test_potion"));

    gfx_bitmap_t b;
    TEST_ASSERT_TRUE(vrom_lookup(apple.icon, &b));
    TEST_ASSERT_EQUAL_INT(16, b.width);
    TEST_ASSERT_TRUE(vrom_lookup("bitart_logo", &b));
    TEST_ASSERT_EQUAL_INT(112, b.width);
    TEST_ASSERT_FALSE(vrom_lookup("nao_existe", &b));
    TEST_ASSERT_FALSE(vrom_lookup("main_menu", &b)); /* mockups ficam fora da VROM */
}

/* ---------------- Save ---------------- */

static void test_save_round_trip(void)
{
    guara_t g, bytes_read;
    guara_birth(&g, "Tico", &rng, 1700000000);
    g.gauges[GAUGE_ANGER] = -1234;
    g.inventory[63] = 200;
    g.owned_skills = 0x8000000000000001ull;
    uint8_t block[SAVE_SIZE];
    uint32_t counter;
    save_serialize(&g, 77, block);
    TEST_ASSERT_TRUE(save_deserialize(block, &bytes_read, &counter));
    TEST_ASSERT_EQUAL_UINT32(77, counter);
    TEST_ASSERT_EQUAL_MEMORY(&g, &bytes_read, sizeof(g));

    block[100] ^= 1;
    TEST_ASSERT_FALSE(save_deserialize(block, &bytes_read, &counter));
}

typedef struct {
    uint8_t mem[RING_SECTOR * RING_SECTORS];
    int erases[RING_SECTORS];
} fake_flash_t;

static bool flash_read(void *ctx, uint32_t off, void *buf, size_t n)
{
    memcpy(buf, ((fake_flash_t *)ctx)->mem + off, n);
    return true;
}

static bool flash_write(void *ctx, uint32_t off, const void *buf, size_t n)
{
    /* NOR flash: só transforma 1 em 0 */
    for (size_t i = 0; i < n; i++) {
        ((fake_flash_t *)ctx)->mem[off + i] &= ((const uint8_t *)buf)[i];
    }
    return true;
}

static bool flash_erase_sector(void *ctx, uint32_t off)
{
    fake_flash_t *f = ctx;
    memset(f->mem + off, 0xFF, RING_SECTOR);
    f->erases[off / RING_SECTOR]++;
    return true;
}

static void test_ring_rotates_and_wear_levels(void)
{
    static fake_flash_t flash;
    memset(&flash, 0, sizeof(flash)); /* lixo: nenhum save válido */
    flash_ops_t ops = {&flash, flash_read, flash_write, flash_erase_sector};
    ring_t ring;
    guara_t g, bytes_read;
    guara_birth(&g, "Anel", &rng, 1);

    ring_init(&ring, &ops);
    TEST_ASSERT_FALSE(ring_load(&ring, &bytes_read));
    for (int i = 0; i < 40; i++) {
        g.xp = (uint32_t)i;
        TEST_ASSERT_TRUE(ring_save(&ring, &g));
    }
    /* 40 gravações: 5 voltas por setor */
    TEST_ASSERT_EQUAL_INT(3, flash.erases[0]);
    TEST_ASSERT_EQUAL_INT(2, flash.erases[1]);

    ring_t other;
    ring_init(&other, &ops);
    TEST_ASSERT_TRUE(ring_load(&other, &bytes_read));
    TEST_ASSERT_EQUAL_UINT32(39, bytes_read.xp);
    TEST_ASSERT_EQUAL_UINT32(40, other.counter);
}

/* ---------------- Ciclo de vida ---------------- */

static void test_birth(void)
{
    guara_t g;
    guara_birth(&g, "NomeMuitoComprido", &rng, 1);
    TEST_ASSERT_EQUAL_STRING("NomeMuitoC", g.name);
    TEST_ASSERT_EQUAL_INT(1, g.level);
    TEST_ASSERT_TRUE(g.nature <= NAT_AIR);
    TEST_ASSERT_TRUE(g.head >= 1 && g.head <= 9);
    TEST_ASSERT_EQUAL_INT(g.hp_max, g.hp);
}

static void test_initial_skills(void)
{
    REQUIRE_STORAGE();
    guara_t g;
    guara_birth(&g, "Hab", &rng, 1);
    TEST_ASSERT_TRUE(g.owned_skills & 1u); /* primeira compatível (neutra) */
    TEST_ASSERT_EQUAL_INT(0, g.active_skills[0]);
    TEST_ASSERT_EQUAL_INT(g.nature == NAT_FIRE, (int)((g.owned_skills >> 1) & 1u));
    for (int i = 0; i < COUNT_ITEMS; i++) {
        TEST_ASSERT_EQUAL_INT(0, g.inventory[i]); /* sem kit inicial definido */
    }
}

static void test_hunger_and_faint(void)
{
    guara_t g;
    guara_birth(&g, "Fome", &rng, 1);
    clock_ctx_t clock_ctx = {-1, 20 * 60, 8 * 60};
    g.satiety = CENTI(10);
    guara_tick_minute(&g, &clock_ctx, false);
    TEST_ASSERT_TRUE(guara_has_state(&g, ST_HUNGRY));
    TEST_ASSERT_TRUE(g.hp < g.hp_max);
    for (int i = 0; i < 200 && !guara_has_state(&g, ST_FAINTED); i++) {
        guara_tick_minute(&g, &clock_ctx, false);
    }
    TEST_ASSERT_TRUE(guara_has_state(&g, ST_FAINTED));
    TEST_ASSERT_EQUAL_INT(0, g.hp);
}

static void test_hp_recovery_burns_energy(void)
{
    guara_t g;
    guara_birth(&g, "Cura", &rng, 1);
    clock_ctx_t clock_ctx = {-1, 20 * 60, 8 * 60};
    g.satiety = g.health = g.mood = CENTI(90);
    g.energy = CENTI(60);
    g.hp = 1;
    guara_tick_minute(&g, &clock_ctx, false);
    TEST_ASSERT_TRUE(guara_has_state(&g, ST_RECOVERING));
    TEST_ASSERT_TRUE(g.hp > 1);
    /* Abaixo de 50% de energia continua até a energia chegar a 10% */
    g.energy = CENTI(30);
    g.hp = 1;
    guara_tick_minute(&g, &clock_ctx, false);
    TEST_ASSERT_TRUE(g.hp > 1);
    TEST_ASSERT_TRUE(g.energy < CENTI(30) - DECAY_ENERGY);
    g.energy = CENTI(10);
    uint16_t hp = g.hp;
    guara_tick_minute(&g, &clock_ctx, false);
    TEST_ASSERT_FALSE(guara_has_state(&g, ST_RECOVERING));
    TEST_ASSERT_EQUAL_UINT16(hp, g.hp);
    /* Dormindo fica em pausa, sem perder o estado */
    g.energy = CENTI(60);
    guara_tick_minute(&g, &clock_ctx, false);
    TEST_ASSERT_TRUE(guara_has_state(&g, ST_RECOVERING));
    hp = g.hp;
    guara_set_state(&g, ST_SLEEPING, true);
    guara_tick_minute(&g, &clock_ctx, false);
    TEST_ASSERT_EQUAL_UINT16(hp, g.hp);
    TEST_ASSERT_TRUE(guara_has_state(&g, ST_RECOVERING));
    guara_set_state(&g, ST_SLEEPING, false);
    g.energy = CENTI(10);
    guara_tick_minute(&g, &clock_ctx, false);
    /* Desligada, não volta só com a energia em 30% */
    g.energy = CENTI(30);
    guara_tick_minute(&g, &clock_ctx, false);
    TEST_ASSERT_FALSE(guara_has_state(&g, ST_RECOVERING));
}

static void test_recovery_has_locks(void)
{
    guara_t g;
    guara_birth(&g, "Longe", &rng, 1000);
    uint32_t yes = guara_catch_up_time(&g, 1000 + 3 * 24 * 3600, 12 * 60, 20 * 60, 8 * 60);
    TEST_ASSERT_EQUAL_UINT32(3 * 24 * 60, yes);
    TEST_ASSERT_FALSE(guara_has_state(&g, ST_FAINTED));
    TEST_ASSERT_TRUE(g.hp >= g.hp_max * LOCK_HP_PCT / 100);
    TEST_ASSERT_TRUE(g.satiety >= LOCK_VITAL);
    TEST_ASSERT_EQUAL_UINT32(1000 + 3 * 24 * 3600, g.timestamp);
}

static void test_sleeps_at_bedtime_and_wakes(void)
{
    guara_t g;
    guara_birth(&g, "Sono", &rng, 1);
    g.satiety = VITAL_MAX;
    clock_ctx_t clock_ctx = {20 * 60, 20 * 60, 8 * 60};
    guara_tick_minute(&g, &clock_ctx, false);
    TEST_ASSERT_TRUE(guara_has_state(&g, ST_SLEEPING));
    g.energy = CENTI(60);
    clock_ctx.minute_of_day = 8 * 60;
    guara_tick_minute(&g, &clock_ctx, false);
    TEST_ASSERT_FALSE(guara_has_state(&g, ST_SLEEPING));
}

static void test_respects_the_night(void)
{
    guara_t g;
    guara_birth(&g, "Noite", &rng, 1);
    g.satiety = g.health = g.mood = VITAL_MAX;
    g.energy = CENTI(90);
    clock_ctx_t clock_ctx = {20 * 60 + 3, 20 * 60, 8 * 60};
    /* Minuto da hora de dormir pulado: à noite, dorme depois de alguns minutos sem interação */
    g.idle_minutes = NIGHT_IDLE_SLEEP_MIN;
    guara_tick_minute(&g, &clock_ctx, false);
    TEST_ASSERT_TRUE(guara_has_state(&g, ST_SLEEPING));
    /* Energia cheia no meio da noite: continua dormindo */
    g.energy = VITAL_MAX;
    g.satiety = VITAL_MAX;
    clock_ctx.minute_of_day = 23 * 60;
    guara_tick_minute(&g, &clock_ctx, false);
    TEST_ASSERT_TRUE(guara_has_state(&g, ST_SLEEPING));
    clock_ctx.minute_of_day = 2 * 60;
    guara_tick_minute(&g, &clock_ctx, false);
    TEST_ASSERT_TRUE(guara_has_state(&g, ST_SLEEPING));
    /* Acordado para comer à noite: volta a dormir sem interação, não na hora */
    guara_set_state(&g, ST_SLEEPING, false);
    g.idle_minutes = 0;
    guara_tick_minute(&g, &clock_ctx, false);
    TEST_ASSERT_FALSE(guara_has_state(&g, ST_SLEEPING));
    g.idle_minutes = NIGHT_IDLE_SLEEP_MIN;
    guara_tick_minute(&g, &clock_ctx, false);
    TEST_ASSERT_TRUE(guara_has_state(&g, ST_SLEEPING));
    /* De manhã acorda; de dia, sem interação, continua acordado */
    clock_ctx.minute_of_day = 8 * 60 + 10;
    guara_tick_minute(&g, &clock_ctx, false);
    TEST_ASSERT_FALSE(guara_has_state(&g, ST_SLEEPING));
    g.idle_minutes = 60;
    clock_ctx.minute_of_day = 14 * 60;
    guara_tick_minute(&g, &clock_ctx, false);
    TEST_ASSERT_FALSE(guara_has_state(&g, ST_SLEEPING));
}

static void test_affinity_opposites_and_birth_nature(void)
{
    guara_t g;
    guara_birth(&g, "Afin", &rng, 1);
    g.nature = g.birth_nature = NAT_FIRE;
    guara_add_affinity(&g, NAT_WATER, CENTI(30));
    guara_add_affinity(&g, NAT_FIRE, CENTI(10)); /* vai para o neutro */
    TEST_ASSERT_EQUAL_INT(CENTI(30), g.affinities[NAT_WATER]);
    TEST_ASSERT_EQUAL_INT(0, g.affinities[NAT_FIRE]);
    TEST_ASSERT_EQUAL_INT(CENTI(10), g.affinities[NAT_NEUTRAL]);
    guara_add_affinity(&g, NAT_EARTH, CENTI(5));
    guara_add_affinity(&g, NAT_AIR, CENTI(2));
    TEST_ASSERT_EQUAL_INT(CENTI(3), g.affinities[NAT_EARTH]);
}

static void test_evolution_steam(void)
{
    guara_t g;
    guara_birth(&g, "Evo", &rng, 1);
    g.nature = g.birth_nature = NAT_FIRE;
    guara_add_affinity(&g, NAT_WATER, CENTI(60));
    uint32_t total = 0;
    for (int n = 1; n < 15; n++) {
        total += guara_xp_to_next_level((uint8_t)n);
    }
    g.age_minutes = 10 * 1440; /* sem bônus de idade */
    guara_gain_xp(&g, total, &rng);
    TEST_ASSERT_EQUAL_INT(15, g.level);
    TEST_ASSERT_EQUAL_INT(STAGE_YOUNG_B, g.stage);
    TEST_ASSERT_EQUAL_INT(NAT_STEAM, g.nature);
    TEST_ASSERT_FALSE(guara_affinity_enabled(&g));
}

static void test_item_heal_and_blocks(void)
{
    REQUIRE_STORAGE();
    guara_t g;
    catalog_record_t potion, apple;
    guara_birth(&g, "Item", &rng, 1);
    catalog_read(CAT_ITEMS, catalog_find_slug(CAT_ITEMS, "test_potion"), &potion);
    catalog_read(CAT_ITEMS, 0, &apple);
    g.inventory[potion.id] = 1;
    g.inventory[apple.id] = 1;
    /* Cheio: a poção (só HP) e a comida não fazem efeito e não são gastas */
    g.hp = g.hp_max;
    g.satiety = 10000;
    TEST_ASSERT_EQUAL_INT(USE_FULL, guara_use_item(&g, &potion, &rng));
    TEST_ASSERT_EQUAL_INT(USE_FULL, guara_use_item(&g, &apple, &rng));
    TEST_ASSERT_EQUAL_INT(1, g.inventory[potion.id]);
    TEST_ASSERT_EQUAL_INT(1, g.inventory[apple.id]);
    g.satiety = 9000;
    TEST_ASSERT_EQUAL_INT(USE_OK, guara_can_use_item(&g, &apple));
    g.hp = 1;
    TEST_ASSERT_EQUAL_INT(USE_OK, guara_use_item(&g, &potion, &rng));
    TEST_ASSERT_EQUAL_INT(g.hp_max < 31 ? g.hp_max : 31, g.hp);
    TEST_ASSERT_EQUAL_INT(USE_NO_STOCK, guara_use_item(&g, &potion, &rng));

    guara_set_state(&g, ST_CURSED, true);
    TEST_ASSERT_EQUAL_INT(USE_BLOCKED_CURSE, guara_can_use_item(&g, &apple));
    guara_set_state(&g, ST_CURSED, false);
    guara_set_state(&g, ST_FAINTED, true);
    g.minutes_fainted = 5;
    TEST_ASSERT_EQUAL_INT(USE_BLOCKED_FAINTED, guara_can_use_item(&g, &apple));
}

static void test_development_version_rule(void)
{
    REQUIRE_STORAGE();
    TEST_ASSERT_TRUE(guara_is_development_version("0.1.0"));
    TEST_ASSERT_TRUE(guara_is_development_version("0.12.3"));
    TEST_ASSERT_FALSE(guara_is_development_version("1.0.0"));
    TEST_ASSERT_FALSE(guara_is_development_version("10.0.0"));
    guara_t g;
    guara_birth(&g, "Dev", &rng, 1);
    guara_unlock_catalog(&g);
    TEST_ASSERT_EQUAL_INT(255, g.inventory[0]);
    TEST_ASSERT_EQUAL_INT(255, g.inventory[1]);
    TEST_ASSERT_EQUAL_INT(0, g.inventory[2]); /* fora do catálogo de teste */
    TEST_ASSERT_EQUAL_HEX64(0x1ull, g.owned_equipment);
    TEST_ASSERT_EQUAL_HEX64(0x3ull, g.owned_skills);
}

/* ---------------- Combate ---------------- */

static void test_combat_formulas(void)
{
    TEST_ASSERT_EQUAL_INT(5750, combat_atb_time(5));
    TEST_ASSERT_EQUAL_INT(1500, combat_atb_time(90));
    TEST_ASSERT_EQUAL_INT(MULT_ADVANTAGE, combat_multiplier(NAT_WATER, NAT_FIRE));
    TEST_ASSERT_EQUAL_INT(MULT_DISADVANTAGE, combat_multiplier(NAT_FIRE, NAT_WATER));
    TEST_ASSERT_EQUAL_INT(MULT_ADVANTAGE, combat_multiplier(NAT_FIRE, NAT_AIR));
    /* Vapor (Fogo + Água) não sofre a fraqueza do Fogo contra Água */
    TEST_ASSERT_EQUAL_INT(100, combat_multiplier(NAT_WATER, NAT_STEAM));
    TEST_ASSERT_EQUAL_INT(100, combat_multiplier(NAT_NEUTRAL, NAT_FIRE));
    TEST_ASSERT_EQUAL_INT(MULT_ADVANTAGE, combat_multiplier(NAT_LIGHT, NAT_SHADOW));
}

static void test_combat_ends(void)
{
    guara_t g;
    guara_birth(&g, "Luta", &rng, 1);
    fighter_t me, op;
    combat_fighter_from_guara(&g, &me);
    combat_generate_opponent(&g, &rng, &op);
    combat_t c;
    combat_start(&c, &me, &op, &rng);
    combat_result_t r;
    for (int step = 0; step < 10000 && !c.finished; step++) {
        unsigned ready_mask = combat_advance(&c, 100);
        for (int side = 0; side < 2; side++) {
            if (ready_mask & (1u << side)) {
                combat_action_t action = combat_ai(&c, side);
                catalog_record_t item;
                bool has_item = combat_action_item(action, &item);
                combat_execute(&c, side, action, has_item ? &item : NULL, &r);
            }
        }
    }
    TEST_ASSERT_TRUE(c.finished);
    TEST_ASSERT_TRUE(c.winner == 0 || c.winner == 1);
    TEST_ASSERT_EQUAL_INT(0, c.side[1 - c.winner].hp);
}

/* O oponente gerado leva a poção do catálogo de teste e a usa com HP baixo. */
static void test_opponent_potion(void)
{
    guara_t g;
    guara_birth(&g, "Pocao", &rng, 1);
    fighter_t me, op;
    combat_fighter_from_guara(&g, &me);
    combat_generate_opponent(&g, &rng, &op);
    TEST_ASSERT_EQUAL_INT(1, op.item_ids[0]); /* test_potion (HP+30) */
    TEST_ASSERT_TRUE(op.item_counts[0] >= 1 && op.item_counts[0] <= 2);
    int count = op.item_counts[0];
    combat_t c;
    combat_start(&c, &me, &op, &rng);
    c.side[1].hp = 1;
    memset(c.side[1].skills, SLOT_EMPTY, sizeof(c.side[1].skills)); /* sem habilidade de cura */
    combat_action_t action = combat_ai(&c, 1);
    TEST_ASSERT_EQUAL_INT(ACTION_ITEM, action.type);
    catalog_record_t item;
    TEST_ASSERT_TRUE(combat_action_item(action, &item));
    combat_result_t r;
    combat_execute(&c, 1, action, &item, &r);
    TEST_ASSERT_FALSE(r.failed);
    TEST_ASSERT_EQUAL_INT(31, c.side[1].hp);
    TEST_ASSERT_EQUAL_INT(count - 1, c.side[1].item_counts[0]);
    /* Sem poções, ataca */
    c.side[1].item_counts[0] = 0;
    c.side[1].hp = 1;
    TEST_ASSERT_EQUAL_INT(ACTION_ATTACK, combat_ai(&c, 1).type);
}

/* ---------------- Conectar (protocolo) ---------------- */

/* Codifica e decodifica uma oferta; rejeita assinatura, versão e tamanho errados. */
static void test_link_roundtrip(void)
{
    guara_t g;
    guara_birth(&g, "Aline", &rng, 1);
    link_msg_t m = {.type = LINK_OFFER, .tx = 0xA1B2C3D4u, .item_id = 42, .count = 3};
    link_profile_from_guara(&g, &m.from);
    uint8_t buf[LINK_MAX_SIZE];
    int size = link_encode(&m, buf);
    TEST_ASSERT_EQUAL_INT(LINK_HEADER_SIZE + 6, size);
    link_msg_t d;
    TEST_ASSERT_TRUE(link_decode(buf, size, &d));
    TEST_ASSERT_EQUAL_INT(LINK_OFFER, d.type);
    TEST_ASSERT_EQUAL_STRING("Aline", d.from.name);
    TEST_ASSERT_EQUAL_UINT32(g.uid, d.from.uid);
    TEST_ASSERT_EQUAL_UINT32(0xA1B2C3D4u, d.tx);
    TEST_ASSERT_EQUAL_INT(42, d.item_id);
    TEST_ASSERT_EQUAL_INT(3, d.count);
    TEST_ASSERT_FALSE(link_decode(buf, size - 1, &d));
    buf[2] = LINK_VERSION + 1;
    TEST_ASSERT_FALSE(link_decode(buf, size, &d));
    buf[2] = LINK_VERSION;
    buf[0] = 'X';
    TEST_ASSERT_FALSE(link_decode(buf, size, &d));
}

/* Mensagens do combate: lutadores no START e resultado com os vitais no RESULT. */
static void test_link_combat_messages(void)
{
    guara_t g;
    guara_birth(&g, "Luta", &rng, 1);
    fighter_t me, op;
    combat_fighter_from_guara(&g, &me);
    combat_generate_opponent(&g, &rng, &op);
    combat_t c;
    combat_start(&c, &me, &op, &rng);
    link_msg_t m = {.type = LINK_START, .seq = 7};
    m.fighters[0] = c.side[0];
    m.fighters[1] = c.side[1];
    uint8_t buf[LINK_MAX_SIZE];
    int size = link_encode(&m, buf);
    TEST_ASSERT_TRUE(size > 0 && size <= LINK_MAX_SIZE);
    link_msg_t d;
    TEST_ASSERT_TRUE(link_decode(buf, size, &d));
    TEST_ASSERT_EQUAL_UINT16(7, d.seq);
    TEST_ASSERT_EQUAL_STRING(c.side[1].name, d.fighters[1].name);
    TEST_ASSERT_EQUAL_INT(c.side[1].hp_max, d.fighters[1].hp_max);
    TEST_ASSERT_EQUAL_INT(c.side[0].atb_total, d.fighters[0].atb_total);
    TEST_ASSERT_EQUAL_INT_ARRAY(c.side[1].attributes, d.fighters[1].attributes, ATTRIBUTE_COUNT);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(c.side[0].skills, d.fighters[0].skills, COUNT_SKILL_ACTIVE);

    combat_result_t r;
    combat_execute(&c, 0, (combat_action_t){ACTION_ATTACK, 0, 0}, NULL, &r);
    m = (link_msg_t){.type = LINK_RESULT, .seq = 8, .result = r, .finished = c.finished, .winner = (int8_t)c.winner};
    link_vitals_from(&c.side[0], &m.vitals[0]);
    link_vitals_from(&c.side[1], &m.vitals[1]);
    size = link_encode(&m, buf);
    TEST_ASSERT_TRUE(link_decode(buf, size, &d));
    TEST_ASSERT_EQUAL_INT(r.damage, d.result.damage);
    TEST_ASSERT_EQUAL_INT(r.dodged, d.result.dodged);
    TEST_ASSERT_EQUAL_INT(r.critical, d.result.critical);
    TEST_ASSERT_EQUAL_INT(c.side[1].hp, d.vitals[1].hp);
    TEST_ASSERT_EQUAL_INT(c.winner, d.winner);
    TEST_ASSERT_FALSE(link_decode(buf, size - 1, &d)); /* carga cortada */
}

/* Vizinhos entram na ordem, atualizam sem duplicar e expiram sem sinal. */
static void test_link_peers(void)
{
    link_peers_t t = {0};
    link_profile_t a = {1, "A", 1, 0, 0, 0, 1}, b = {2, "B", 2, 0, 0, 0, 1};
    const uint8_t mac_a[6] = {1, 1, 1, 1, 1, 1}, mac_b[6] = {2, 2, 2, 2, 2, 2};
    link_peers_seen(&t, mac_a, &a, 0);
    link_peers_seen(&t, mac_b, &b, 1000);
    link_peers_seen(&t, mac_a, &a, 2000);
    TEST_ASSERT_EQUAL_INT(2, t.count);
    TEST_ASSERT_EQUAL_UINT32(1, t.peers[0].profile.uid);
    link_peers_expire(&t, 1000 + LINK_PEER_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_INT(1, t.count); /* B sumiu, A foi visto em 2000 */
    TEST_ASSERT_EQUAL_UINT32(1, t.peers[0].profile.uid);
}

/* Uma oferta aceita é lembrada pelo par (UID, tx). */
static void test_link_tx_log(void)
{
    link_tx_log_t log = {0};
    TEST_ASSERT_FALSE(link_tx_log_has(&log, 7, 100));
    link_tx_log_add(&log, 7, 100);
    TEST_ASSERT_TRUE(link_tx_log_has(&log, 7, 100));
    TEST_ASSERT_FALSE(link_tx_log_has(&log, 8, 100));
    for (uint32_t i = 0; i < LINK_TX_LOG; i++) {
        link_tx_log_add(&log, 9, 200 + i);
    }
    TEST_ASSERT_FALSE(link_tx_log_has(&log, 7, 100)); /* a mais antiga saiu */
}

static void test_forfeit(void)
{
    guara_t g;
    guara_birth(&g, "Fuga", &rng, 1);
    fighter_t me, op;
    combat_fighter_from_guara(&g, &me);
    combat_generate_opponent(&g, &rng, &op);
    combat_t c;
    combat_result_t r;
    combat_start(&c, &me, &op, &rng);
    combat_execute(&c, 0, (combat_action_t){ACTION_FORFEIT, 0, 0}, NULL, &r);
    TEST_ASSERT_TRUE(c.finished);
    TEST_ASSERT_EQUAL_INT(1, c.winner);
}

/* Soma o dano de muitos ataques físicos com a reação indicada (HP restaurado a cada golpe). */
static long attack_total(unsigned reaction, int *criticals, int *doubles)
{
    guara_t g;
    rng_t local;
    rng_seed(&local, 1234);
    guara_birth(&g, "Golpe", &local, 1);
    fighter_t me, op;
    combat_fighter_from_guara(&g, &me);
    op = me; /* mesma AGI: sem esquiva */
    combat_t c;
    combat_start(&c, &me, &op, &local);
    combat_result_t r;
    long total = 0;
    *criticals = *doubles = 0;
    for (int i = 0; i < 2000; i++) {
        c.side[1].hp = c.side[1].hp_max = 30000;
        combat_execute(&c, 0, (combat_action_t){ACTION_ATTACK, 0, reaction}, NULL, &r);
        total += r.damage;
        *criticals += r.critical;
        *doubles += r.hits == 2;
    }
    return total;
}

static void test_combat_reaction(void)
{
    int crit_none, crit_timed, crit_guard, dbl_none, dbl_timed, dbl_guard;
    long none = attack_total(0, &crit_none, &dbl_none);
    long timed = attack_total(COMBAT_TIMED_STRIKE, &crit_timed, &dbl_timed);
    long guard = attack_total(COMBAT_GUARD, &crit_guard, &dbl_guard);
    TEST_ASSERT_EQUAL_INT(0, dbl_none);
    TEST_ASSERT_TRUE(dbl_timed > 0);
    TEST_ASSERT_TRUE(crit_timed > crit_none);
    TEST_ASSERT_TRUE(timed > none);
    TEST_ASSERT_TRUE(guard * 10 < none * 6); /* metade do dano e mais erros */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_storage_catalog_and_vrom);
    RUN_TEST(test_save_round_trip);
    RUN_TEST(test_ring_rotates_and_wear_levels);
    RUN_TEST(test_birth);
    RUN_TEST(test_initial_skills);
    RUN_TEST(test_hunger_and_faint);
    RUN_TEST(test_hp_recovery_burns_energy);
    RUN_TEST(test_recovery_has_locks);
    RUN_TEST(test_sleeps_at_bedtime_and_wakes);
    RUN_TEST(test_respects_the_night);
    RUN_TEST(test_affinity_opposites_and_birth_nature);
    RUN_TEST(test_evolution_steam);
    RUN_TEST(test_item_heal_and_blocks);
    RUN_TEST(test_development_version_rule);
    RUN_TEST(test_combat_formulas);
    RUN_TEST(test_combat_ends);
    RUN_TEST(test_opponent_potion);
    RUN_TEST(test_forfeit);
    RUN_TEST(test_link_roundtrip);
    RUN_TEST(test_link_combat_messages);
    RUN_TEST(test_link_peers);
    RUN_TEST(test_link_tx_log);
    RUN_TEST(test_combat_reaction);
    return UNITY_END();
}
