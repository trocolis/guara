#ifndef GUARA_COMBATE_H
#define GUARA_COMBATE_H

#include <stdbool.h>
#include <stdint.h>

#include "guara.h"
#include "rng.h"
#include "storage.h"

/* Motor de combate por barra de tempo (ATB). Lado 0 = jogador, 1 = oponente. */

typedef struct {
    char nome[TAM_NOME + 1];
    uint8_t natureza, cor, cabeca, estagio, nivel;
    int hp, hp_max, mp, mp_max;
    int atributos[QTD_ATRIBUTOS];
    uint32_t estados;
    uint8_t habilidades[QTD_HAB_ATIVAS];
    int atb_ms;     /* tempo acumulado */
    int atb_total;  /* tempo para encher a barra */
} lutador_t;

typedef enum { ACAO_ATACAR, ACAO_HABILIDADE, ACAO_ITEM, ACAO_DESISTIR } acao_tipo_t;

typedef struct {
    acao_tipo_t tipo;
    int indice; /* slot da habilidade (0..3) ou id do item */
} acao_t;

typedef struct {
    int ator, alvo;
    acao_tipo_t tipo;
    bool falhou;        /* não conseguiu agir (paralisado, congelado, sem MP, maldição) */
    bool errou;         /* confusão */
    bool esquivou;
    bool critico;
    bool auto_golpe;    /* confuso atingiu a si mesmo */
    int dano;
    int cura;
    int dano_estado;    /* dano de veneno/queimadura/gelo no início do turno */
    int estado_aplicado;/* estado_t aplicado no alvo, ou -1 */
    int multiplicador;  /* % elemental aplicado */
    char icone[32];     /* ícone da habilidade/item usado */
} resultado_t;

typedef struct {
    lutador_t lado[2];
    bool terminado;
    int vencedor; /* 0, 1 ou -1 */
    rng_t *rng;
} combate_t;

int combate_tempo_atb(int agilidade);
int combate_multiplicador(natureza_t ataque, natureza_t defensor);

void combate_lutador_de_guara(const guara_t *g, lutador_t *l);
void combate_gerar_oponente(const guara_t *referencia, rng_t *rng, lutador_t *l);

void combate_iniciar(combate_t *c, const lutador_t *jogador, const lutador_t *oponente, rng_t *rng);
/* Avança as barras ATB. Retorna máscara de lados prontos (bit 0 jogador, bit 1 oponente). */
unsigned combate_atualizar(combate_t *c, int dt_ms);
bool combate_pronto(const combate_t *c, int lado);
/* Executa a ação de um lado pronto. `item` é necessário para ACAO_ITEM. */
void combate_executar(combate_t *c, int lado, acao_t acao, const registro_t *item, resultado_t *r);
/* Escolha automática do oponente. */
acao_t combate_ia(combate_t *c, int lado);

uint32_t combate_xp(const combate_t *c, int lado_vencedor);
/* Sorteia um item dropado (id) ou -1. */
int combate_drop(const combate_t *c, int lado_vencedor);

#endif
