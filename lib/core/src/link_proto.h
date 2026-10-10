/*
 * Protocolo de comunicação entre dois Guarás (Conectar): mensagens de 30
 * bytes, tabela de vizinhos com expiração e registro das trocas já aceitas.
 * Não depende do rádio: o firmware transporta os bytes pelo ESP-NOW.
 */
#ifndef GUARA_LINK_PROTO_H
#define GUARA_LINK_PROTO_H

#include <stdbool.h>
#include <stdint.h>

#include "guara.h"

#define LINK_VERSION 1
#define LINK_MESSAGE_SIZE 30
#define LINK_MAX_PEERS 8
#define LINK_PEER_TIMEOUT_MS 3500 /* some da lista sem presença por esse tempo */
#define LINK_TX_LOG 8              /* trocas aceitas lembradas (para não somar em dobro) */

typedef enum {
    LINK_HELLO = 1, /* presença, em broadcast a cada segundo */
    LINK_OFFER,     /* quer dar `count` unidades de `item_id` (repetida até a resposta) */
    LINK_ACCEPT,    /* aceitou a oferta `tx` (os itens já foram somados) */
    LINK_DECLINE,   /* recusou a oferta `tx` */
} link_type_t;

/* Quem envia: vai em toda mensagem, para a outra ponta mostrar nome e nível. */
typedef struct {
    uint32_t uid;
    char name[SIZE_NAME + 1];
    uint8_t level, stage, nature, color, head;
} link_profile_t;

typedef struct {
    link_type_t type;
    link_profile_t from;
    uint32_t tx; /* número da troca (OFFER, ACCEPT, DECLINE) */
    uint8_t item_id, count;
} link_msg_t;

/* Perfil do Guará atual. */
void link_profile_from_guara(const guara_t *g, link_profile_t *p);
/* Codifica em `buf` (ao menos LINK_MESSAGE_SIZE bytes). Retorna o tamanho. */
int link_encode(const link_msg_t *m, uint8_t *buf);
/* Decodifica; falso se não for uma mensagem do Guará desta versão. */
bool link_decode(const uint8_t *buf, int length, link_msg_t *m);

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

/* Trocas aceitas: uma oferta repetida (resposta perdida) não soma de novo. */
typedef struct {
    uint32_t uid[LINK_TX_LOG], tx[LINK_TX_LOG];
    int next;
} link_tx_log_t;

bool link_tx_log_has(const link_tx_log_t *log, uint32_t uid, uint32_t tx);
void link_tx_log_add(link_tx_log_t *log, uint32_t uid, uint32_t tx);

#endif
