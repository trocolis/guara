#ifndef GUARA_RULES_H
#define GUARA_RULES_H

/*
 * Números de balanceamento do jogo. Os marcados PROVISÓRIO não vêm do README
 * e estão documentados em docs/GAME_DESIGN.md para revisão.
 *
 * Vitais em percentual (Energia, Humor, Alimentação, Saúde) são guardados em
 * centésimos de ponto percentual: 10000 = 100%.
 */

#define CENTI(pct) ((pct) * 100)
#define VITAL_MAX 10000

/* --- Desgaste por minuto (PROVISÓRIO) --- */
#define DECAY_SATIETY       15       /* -0,15%/min acordado: ~11 h de 100% a 0% */
#define DECAY_ENERGY           10    /* -0,10%/min acordado: ~16 h */
#define REGEN_ENERGY_SLEEP     50    /* +0,50%/min dormindo: ~3 h */
#define DECAY_MOOD             20    /* quando energia ou alimentação < 50% */
#define REGEN_MOOD            10     /* quando energia e alimentação > 40% */
#define DECAY_MOOD_IDLE      10      /* sem interação há mais de LIMIT_IDLE_MIN */
#define DECAY_HEALTH             10  /* quando energia ou alimentação < 20% */
#define REGEN_HEALTH            10   /* quando alimentação, energia e humor > 50% */
#define DECAY_HP_HUNGER_PCT        1 /* % do HP máximo por minuto faminto */
#define DECAY_HP_STATE_PCT      2    /* % do HP máximo por minuto envenenado/queimado/congelado (README: "2%", por minuto) */
#define DECAY_HEALTH_STATE      100  /* -1%/min de saúde nesses estados */
#define LIMIT_IDLE_MIN     120       /* minutos sem interação até ficar entediado */

/* --- Limiares do README (Ciclo de vida > Desgaste) --- */
#define LIMIT_MOOD_DROPS        CENTI(50)
#define LIMIT_HEALTH_DROPS        CENTI(20)
#define LIMIT_HUNGER             CENTI(10) /* alimentação <= 10%: Faminto */
#define LIMIT_TIRED          CENTI(20)     /* energia < 20%: Cansado */
#define LIMIT_HEALTH_RISES       CENTI(50)
#define LIMIT_MOOD_RISES       CENTI(40)
#define LIMIT_WAKE          CENTI(50)    /* só acorda com energia > 50% */
#define LIMIT_HAPPY            CENTI(80) /* todos os vitais > 80%: Feliz */
#define LIMIT_LOW_MOOD      CENTI(30)    /* abaixo disso surge emoção negativa (PROVISÓRIO) */

#define FAINT_LOCKOUT_MIN  10 /* README */
#define FAINT_RECOVER_MIN  60 /* PROVISÓRIO: acorda sozinho com 10% de HP */

/* --- Recuperação temporal (Δt) --- */
#define DELTA_MAX_MIN       (7 * 24 * 60) /* PROVISÓRIO: limite de minutos simulados */
#define LOCK_HP_PCT        20             /* PROVISÓRIO: durante o Δt o HP não cai abaixo de 20% */
#define LOCK_VITAL         CENTI(5)

/* --- Medidores ocultos (faixa ±10000) --- */
#define GAUGE_MAX         10000
#define GAUGE_STEP_CARE   2
#define GAUGE_STEP_ANGER     2
#define GAUGE_STEP_SADNESS  300  /* por derrota */
#define GAUGE_STEP_VICTORY   300
#define GAUGE_STEP_DAY       50        /* por interação na janela de dia/noite */
#define GAUGE_THRESHOLD_EMOTION   2000 /* |Raiva×Tristeza| acima disso define triste/zangado */

/* --- Afinidades --- */
#define AFFINITY_PER_ITEM       CENTI(1)   /* PROVISÓRIO */

/* --- Progressão (PROVISÓRIO) --- */
#define LEVEL_MAX           99
#define HP_BASE             30
#define HP_PER_LEVEL        8
#define MP_BASE             10
#define MP_PER_LEVEL        3
#define ATTR_INITIAL         5
#define ATTR_NATURE_BONUS  2
#define LEVELS_PER_SKILL 5
/* Nível mínimo de cada estágio (bebê, jovem A, B, C, adulto) fica em guara.c. */

/* --- Evolução da natureza (PROVISÓRIO) --- */
#define EVO_AFFINITY_FUSION      CENTI(40) /* afinidade mínima do elemento dominante */
#define EVO_DAY_NIGHT       6000           /* |Dia×Noite| para Luz/Sombra */
#define EVO_INT_FACTOR_X10   15            /* INT >= 1,5x o maior outro atributo: Psíquica */

/* --- Combate (README, Fórmulas do Sistema de Combate) --- */
#define ATB_MIN_MS          1500
#define ATB_BASE_MS         6000
#define ATB_PER_AGI_MS      50
#define MULT_ADVANTAGE       125
#define MULT_DISADVANTAGE    75
#define CRITICAL_BASE_X10    50 /* 5% */
#define CRITICAL_PER_LUK_X10 4  /* 0,4% */
#define CRITICAL_MAX_X10     500
#define DODGE_PER_AGI_X10 3    /* 0,3% */
#define DODGE_MAX_X10     350
#define DECAY_HP_STATE_TURN_PCT 5 /* PROVISÓRIO: veneno/queimadura/gelo por ação em combate */
#define DROP_BASE_PCT       10    /* PROVISÓRIO */

/* --- Reação no tempo certo (PROVISÓRIO) --- */
#define REACTION_WINDUP_MS       600 /* preparo do golpe até o impacto */
#define REACTION_WINDOW_MS       250 /* janela de A no fim do preparo */
#define TIMED_CRITICAL_BONUS_X10 200 /* +20% de crítico para quem ataca */
#define TIMED_DOUBLE_PCT         30  /* chance de ataque duplo (só ataque físico) */
#define GUARD_DAMAGE_PCT         50  /* dano recebido ao se defender */
#define GUARD_MISS_X10           150 /* +15% de chance de erro do atacante */
#define AI_REACTION_PCT          25  /* chance do oponente acertar a janela */

#endif
