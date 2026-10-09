#ifndef GUARA_TYPES_H
#define GUARA_TYPES_H

/*
 * Enumerações compartilhadas entre o firmware e o storage-packer.
 * Manter em sincronia com as listas de tools/storage-packer/pack_storage.py.
 */

/* Alvos de efeito de itens e equipamentos: vitais (0..5) e atributos (6..11). */
typedef enum {
    VITAL_HP,
    VITAL_MP,
    VITAL_ENERGY,
    VITAL_MOOD,
    VITAL_SATIETY,
    VITAL_HEALTH,
    ATTR_ATK,
    ATTR_DEF,
    ATTR_STR,
    ATTR_AGI,
    ATTR_INT,
    ATTR_LUK,
} effect_target_t;

#define ATTRIBUTE_COUNT 6
#define ATTR_INDEX(a) ((int)(a) - (int)ATTR_ATK)

typedef enum {
    NAT_FIRE,
    NAT_EARTH,
    NAT_WATER,
    NAT_AIR,
    NAT_NEUTRAL,
    NAT_ELECTRICITY,
    NAT_PLANT,
    NAT_ICE,
    NAT_STEAM,
    NAT_CRYSTAL,
    NAT_SHADOW,
    NAT_LIGHT,
    NAT_PSYCHIC,
    NAT_LAVA,
    COUNT_NATURES,
} nature_t;

/* As afinidades usam os 5 primeiros valores de nature_t. */
#define COUNT_AFFINITIES 5

/* Estados: bit N do campo `estados` do Guará. */
typedef enum {
    ST_SLEEPING = 0,
    ST_FAINTED = 1,
    ST_POISONED = 2,
    ST_PARALYZED = 3,
    ST_BURNED = 4,
    ST_FROZEN = 5,
    ST_CONFUSED = 6,
    ST_CURSED = 7,
    ST_TIRED = 8,
    ST_HUNGRY = 9,
    ST_RECOVERING = 10, /* interno, sem ícone: recuperação de HP ligada (Ciclo de vida > Desgaste) */
    ST_HAPPY = 16,
    ST_SAD = 17,
    ST_ANGRY = 18,
    ST_BORED = 19,
    ST_ALL = 255, /* só no catálogo: "cura todos os estados" */
} state_t;

#define STATE_BIT(e) (1u << (e))
#define MASK_PHYSICAL 0x0000FFFFu
#define MASK_EMOTIONAL 0xFFFF0000u

typedef enum { ITEM_FOOD, ITEM_DRINK, ITEM_POTION } item_type_t;

typedef enum { EQ_HEAD, EQ_BODY, EQ_HAND_ATK, EQ_HAND_DEF, EQ_FEET, EQ_ACCESSORY } equipment_type_t;

/* Os 7 slots da tela de Equipamentos (dois de acessório). */
#define COUNT_SLOTS_EQUIP 7

typedef enum { SKILL_ATTACK, SKILL_HEAL, SKILL_EFFECT } skill_type_t;
typedef enum { TARGET_USER, TARGET_OPPONENT } skill_target_t;

typedef enum { COLOR_BLACK = 0, COLOR_WHITE = 1 } pet_color_t;

/* Estágios de evolução (sprites baby, yong_a, yong_b, yong_c, adult_a). */
typedef enum { STAGE_BABY, STAGE_YOUNG_A, STAGE_YOUNG_B, STAGE_YOUNG_C, STAGE_ADULT, COUNT_STAGES } stage_t;

/* Medidores ocultos (positivo = primeiro lado do nome). */
typedef enum { GAUGE_VICTORIES, GAUGE_CARE, GAUGE_ANGER, GAUGE_DAY, COUNT_GAUGES } gauge_t;

#endif
