#ifndef GUARA_REGRAS_H
#define GUARA_REGRAS_H

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
#define DESG_ALIMENTACAO       15   /* -0,15%/min acordado: ~11 h de 100% a 0% */
#define DESG_ENERGIA           10   /* -0,10%/min acordado: ~16 h */
#define RECUP_ENERGIA_SONO     50   /* +0,50%/min dormindo: ~3 h */
#define DESG_HUMOR             20   /* quando energia ou alimentação < 50% */
#define RECUP_HUMOR            10   /* quando energia e alimentação > 40% */
#define DESG_HUMOR_OCIOSO      10   /* sem interação há mais de LIMITE_OCIOSO_MIN */
#define DESG_SAUDE             10   /* quando energia ou alimentação < 20% */
#define RECUP_SAUDE            10   /* quando alimentação, energia e humor > 50% */
#define DESG_HP_FOME_PCT        1   /* % do HP máximo por minuto faminto */
#define DESG_HP_ESTADO_PCT      2   /* % do HP máximo por minuto envenenado/queimado/congelado (README: "2%", por minuto) */
#define DESG_SAUDE_ESTADO      100  /* -1%/min de saúde nesses estados */
#define LIMITE_OCIOSO_MIN     120   /* minutos sem interação até ficar entediado */

/* --- Limiares do README (Ciclo de vida > Desgaste) --- */
#define LIM_HUMOR_CAI        CENTI(50)
#define LIM_SAUDE_CAI        CENTI(20)
#define LIM_FOME             CENTI(10)  /* alimentação <= 10%: Faminto */
#define LIM_CANSADO          CENTI(20)  /* energia < 20%: Cansado */
#define LIM_SAUDE_SOBE       CENTI(50)
#define LIM_HUMOR_SOBE       CENTI(40)
#define LIM_ACORDAR          CENTI(50)  /* só acorda com energia > 50% */
#define LIM_FELIZ            CENTI(80)  /* todos os vitais > 80%: Feliz */
#define LIM_HUMOR_BAIXO      CENTI(30)  /* abaixo disso surge emoção negativa (PROVISÓRIO) */

#define DESMAIO_BLOQUEIO_MIN  10   /* README */
#define DESMAIO_RECUPERA_MIN  60   /* PROVISÓRIO: acorda sozinho com 10% de HP */

/* --- Recuperação temporal (Δt) --- */
#define DELTA_MAX_MIN       (7 * 24 * 60) /* PROVISÓRIO: limite de minutos simulados */
#define TRAVA_HP_PCT        20  /* PROVISÓRIO: durante o Δt o HP não cai abaixo de 20% */
#define TRAVA_VITAL         CENTI(5)

/* --- Medidores ocultos (faixa ±10000) --- */
#define MEDIDOR_MAX         10000
#define MED_PASSO_CUIDADO   2
#define MED_PASSO_RAIVA     2
#define MED_PASSO_TRISTEZA  300  /* por derrota */
#define MED_PASSO_VITORIA   300
#define MED_PASSO_DIA       50   /* por interação na janela de dia/noite */
#define MED_LIMIAR_EMOCAO   2000 /* |Raiva×Tristeza| acima disso define triste/zangado */

/* --- Afinidades --- */
#define AFIN_POR_ITEM       CENTI(1)   /* PROVISÓRIO */

/* --- Progressão (PROVISÓRIO) --- */
#define NIVEL_MAX           99
#define HP_BASE             30
#define HP_POR_NIVEL        8
#define MP_BASE             10
#define MP_POR_NIVEL        3
#define ATR_INICIAL         5
#define ATR_BONUS_NATUREZA  2
#define NIVEIS_POR_HABILIDADE 5
/* Nível mínimo de cada estágio (bebê, jovem A, B, C, adulto) fica em guara.c. */

/* --- Evolução da natureza (PROVISÓRIO) --- */
#define EVO_AFIN_FUSAO      CENTI(40) /* afinidade mínima do elemento dominante */
#define EVO_DIA_NOITE       6000      /* |Dia×Noite| para Luz/Sombra */
#define EVO_INT_FATOR_X10   15        /* INT >= 1,5x o maior outro atributo: Psíquica */

/* --- Combate (README, Fórmulas do Sistema de Combate) --- */
#define ATB_MIN_MS          1500
#define ATB_BASE_MS         6000
#define ATB_POR_AGI_MS      50
#define MULT_VANTAGEM       125
#define MULT_DESVANTAGEM    75
#define CRITICO_BASE_X10    50   /* 5% */
#define CRITICO_POR_SOR_X10 4    /* 0,4% */
#define CRITICO_MAX_X10     500
#define ESQUIVA_POR_AGI_X10 3    /* 0,3% */
#define ESQUIVA_MAX_X10     350
#define DESG_HP_ESTADO_TURNO_PCT 5 /* PROVISÓRIO: veneno/queimadura/gelo por ação em combate */
#define DROP_BASE_PCT       10   /* PROVISÓRIO */

#endif
