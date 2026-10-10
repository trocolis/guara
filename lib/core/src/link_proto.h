/*
 * Protocolo de comunicação entre dois Guarás (Conectar): cabeçalho fixo com o
 * perfil de quem envia e uma carga por tipo (troca de itens e combate), tabela
 * de vizinhos com expiração e registro das ofertas já respondidas.
 * Não depende do rádio: o firmware transporta os bytes pelo ESP-NOW.
 */
#ifndef GUARA_LINK_PROTO_H
#define GUARA_LINK_PROTO_H

#include <stdbool.h>
#include <stdint.h>

#include "combat.h"
#include "guara.h"

#define LINK_VERSION 1
#define LINK_HEADER_SIZE 23
#define LINK_MAX_SIZE 160 /* maior mensagem (START); o ESP-NOW aceita 250 */
#define LINK_MAX_PEERS 8
#define LINK_PEER_TIMEOUT_MS 3500 /* some da lista sem presença por esse tempo */
#define LINK_TX_LOG 8              /* ofertas respondidas lembradas (para não somar em dobro) */

typedef enum {
    LINK_HELLO = 1, /* presença, em broadcast a cada segundo */
    LINK_OFFER,     /* quer dar `count` unidades de `item_id` (repetida até a resposta) */
    LINK_ACCEPT,    /* aceitou a oferta ou o desafio `tx` */
    LINK_DECLINE,   /* recusou a oferta ou o desafio `tx` */
    LINK_CHALLENGE, /* convida para um combate (repetido até a resposta) */
    LINK_START,     /* anfitrião → convidado: os dois lutadores no início */
    LINK_STATE,     /* anfitrião → convidado: barras, HP, MP e estados (a cada 100 ms) */
    LINK_WINDUP,    /* anfitrião → convidado: um lado começou o preparo de um golpe */
    LINK_RESULT,    /* anfitrião → convidado: uma ação resolvida */
    LINK_INPUT,     /* convidado → anfitrião: ação escolhida ou desistência */
    LINK_REACT,     /* convidado → anfitrião: apertou A no preparo (no tempo ou não) */
    LINK_BYE,       /* saiu do combate */
} link_type_t;

/* Quem envia: vai em toda mensagem, para a outra ponta mostrar nome e nível. */
typedef struct {
    uint32_t uid;
    char name[SIZE_NAME + 1];
    uint8_t level, stage, nature, color, head;
} link_profile_t;

/* O que muda durante o combate em cada lado (STATE e RESULT). */
typedef struct {
    int hp, mp;
    uint32_t states;
    int atb_ms, atb_total;
} link_vitals_t;

typedef struct {
    link_type_t type;
    link_profile_t from;
    /* Troca e desafio */
    uint32_t tx; /* número da oferta ou do desafio */
    uint8_t item_id, count;
    /* Combate: sempre do ponto de vista do anfitrião (lado 0 = anfitrião) */
    uint16_t seq;                /* número da mensagem do combate (descarta repetidas) */
    fighter_t fighters[2];       /* START */
    link_vitals_t vitals[2];     /* STATE e RESULT */
    bool finished;               /* STATE e RESULT */
    int8_t winner;               /* STATE e RESULT */
    bool guest_pending;          /* STATE: o anfitrião já tem a ação do convidado */
    uint8_t side;                /* WINDUP */
    combat_action_t action;      /* WINDUP e INPUT (tipo e posição) */
    bool hit;                    /* REACT: dentro da janela */
    combat_result_t result;      /* RESULT */
} link_msg_t;

/* Perfil do Guará atual. */
void link_profile_from_guara(const guara_t *g, link_profile_t *p);
/* Codifica em `buf` (ao menos LINK_MAX_SIZE bytes). Retorna o tamanho. */
int link_encode(const link_msg_t *m, uint8_t *buf);
/* Decodifica; falso se não for uma mensagem do Guará desta versão. */
bool link_decode(const uint8_t *buf, int length, link_msg_t *m);
/* Vitais atuais de um lutador. */
void link_vitals_from(const fighter_t *f, link_vitals_t *v);
/* Aplica os vitais recebidos a um lutador. */
void link_vitals_apply(const link_vitals_t *v, fighter_t *f);

/* Vizinhos vistos recentemente, na ordem em que apareceram. */
typedef struct {
    uint8_t mac[6];
    link_profile_t profile;
    uint32_t seen_ms;
} link_peer_t;

typedef struct {
    link_peer_t peers[LINK_MAX_PEERS];
    int count;
} link_peers_t;

/* Registra a presença de `mac` (novo vizinho ou atualização). */
void link_peers_seen(link_peers_t *t, const uint8_t mac[6], const link_profile_t *p, uint32_t now_ms);
/* Remove quem não dá sinal há LINK_PEER_TIMEOUT_MS. */
void link_peers_expire(link_peers_t *t, uint32_t now_ms);

/* Ofertas respondidas: uma oferta repetida (resposta perdida) não soma de novo. */
typedef struct {
    uint32_t uid[LINK_TX_LOG], tx[LINK_TX_LOG];
    int next;
} link_tx_log_t;

bool link_tx_log_has(const link_tx_log_t *log, uint32_t uid, uint32_t tx);
void link_tx_log_add(link_tx_log_t *log, uint32_t uid, uint32_t tx);

#endif
