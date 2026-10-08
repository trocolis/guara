#ifndef GUARA_GUARA_H
#define GUARA_GUARA_H

#include <stdbool.h>
#include <stdint.h>

#include "rng.h"
#include "storage.h"
#include "tipos.h"

#define TAM_NOME 10
#define QTD_ITENS 64
#define QTD_HAB_ATIVAS 4
#define SLOT_VAZIO 0xFF

/* Estado completo do Guará: é o que vai para o Save State (ver save.h). */
typedef struct {
    /* Identidade */
    char nome[TAM_NOME + 1];
    uint32_t uid;
    uint8_t cabeca;               /* 1..9 */
    uint8_t cor;                  /* cor_guara_t */
    uint8_t natureza_nascimento;  /* natureza primária sorteada */
    uint8_t natureza;             /* natureza atual */
    uint8_t estagio;              /* estagio_t */

    /* Vitais */
    uint16_t hp, hp_max, mp, mp_max;
    uint16_t energia, humor, alimentacao, saude; /* 0..10000 */

    /* Progresso */
    uint8_t nivel;
    uint32_t xp;
    uint32_t idade_min;
    uint16_t atributos[QTD_ATRIBUTOS];
    int16_t medidores[QTD_MEDIDORES];
    uint16_t afinidades[QTD_AFINIDADES]; /* 0..10000 */

    /* Posses */
    uint8_t inventario[QTD_ITENS];
    uint64_t equip_possui;
    uint8_t equipado[QTD_SLOTS_EQUIP];
    uint64_t hab_possui;
    uint8_t hab_ativas[QTD_HAB_ATIVAS];

    /* Estados */
    uint32_t estados;
    uint16_t desmaio_min;
    uint16_t ocioso_min;
    uint32_t timestamp; /* epoch da última atualização; 0 = desconhecido */
} guara_t;

/* Contexto de relógio para o ciclo de vida. */
typedef struct {
    int minuto_do_dia;  /* 0..1439, ou -1 se a hora não for conhecida */
    int dormir_min;     /* hora de dormir (min. do dia) */
    int acordar_min;    /* hora de acordar (min. do dia) */
} relogio_ctx_t;

/* Resultado do uso de um item. */
typedef enum {
    USO_OK,
    USO_BLOQUEADO_DESMAIADO,
    USO_BLOQUEADO_MALDICAO,
    USO_SEM_ESTOQUE,
    USO_INVALIDO,
} uso_t;

void guara_nascer(guara_t *g, const char *nome, rng_t *rng, uint32_t agora);

static inline bool guara_tem(const guara_t *g, estado_t e) { return (g->estados & BIT_ESTADO(e)) != 0; }
void guara_por_estado(guara_t *g, estado_t e, bool ativo);

/* Avança um minuto do ciclo de vida. `protegido` aplica as travas do Δt. */
void guara_minuto(guara_t *g, const relogio_ctx_t *rel, bool protegido);
/* Simula o tempo passado (Δt) com as travas de proteção. Retorna os minutos simulados. */
uint32_t guara_recuperar_tempo(guara_t *g, uint32_t agora, int minuto_do_dia_agora, int dormir_min, int acordar_min);

/* Registra uma interação do usuário (zera o ócio, move Dia×Noite). */
void guara_interagir(guara_t *g, const relogio_ctx_t *rel);

/* Versões de desenvolvimento (0.x.x) começam com todo o catálogo liberado. */
bool guara_versao_desenvolvimento(const char *versao);
/* 255 unidades de cada item e posse de todos os equipamentos e habilidades do catálogo. */
void guara_liberar_catalogo(guara_t *g);

uso_t guara_pode_usar_item(const guara_t *g, const registro_t *item);
uso_t guara_usar_item(guara_t *g, const registro_t *item, rng_t *rng);

/* Experiência e nível. Retorna quantos níveis subiu. */
uint32_t guara_xp_proximo(uint8_t nivel);
int guara_ganhar_xp(guara_t *g, uint32_t pontos, rng_t *rng);
/* Multiplicador de XP por idade, em %. */
int guara_bonus_idade(const guara_t *g);

/* Afinidades: soma pontos respeitando os opostos e a natureza de nascimento. */
void guara_afinidade(guara_t *g, natureza_t elemento, int centi);
bool guara_afinidade_ativa(const guara_t *g);

void guara_medidor(guara_t *g, medidor_t m, int delta);

/* Atributo com equipamentos e estados aplicados. */
int guara_atributo(const guara_t *g, alvo_efeito_t atr);

/* Equipamentos */
bool guara_equipar(guara_t *g, int slot, int equip_id);
void guara_desequipar(guara_t *g, int slot);
int guara_tipo_do_slot(int slot); /* tipo_equip_t aceito pelo slot */

/* Estágio de evolução para um nível. */
estagio_t guara_estagio_do_nivel(uint8_t nivel);
/* Elementos primários que compõem uma natureza (bits de NAT_FOGO..NAT_AR). */
unsigned natureza_elementos(natureza_t n);
const char *natureza_nome(natureza_t n);
const char *natureza_icone(natureza_t n);
const char *estado_nome(estado_t e);
const char *estado_icone(estado_t e);

/* Emoção predominante para os olhos e o balão (um dos EST_* emocionais, ou -1). */
int guara_emocao(const guara_t *g);
/* Estado físico mais importante para exibir (ou -1 para normal). */
int guara_estado_principal(const guara_t *g);

#endif
