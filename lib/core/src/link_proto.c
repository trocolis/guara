/*
 * Protocolo do Conectar: formato fixo de 30 bytes (little-endian), tabela de
 * vizinhos e registro de trocas aceitas.
 *
 * Bytes: 0-1 "GL", 2 versão, 3 tipo, 4-7 UID, 8-18 nome (11, com o '\0'),
 * 19 nível, 20 estágio, 21 natureza, 22 cor, 23 cabeça, 24-27 tx,
 * 28 item, 29 quantidade.
 */
#include "link_proto.h"

#include <string.h>

/* Perfil do Guará atual. */
void link_profile_from_guara(const guara_t *g, link_profile_t *p)
{
    memset(p, 0, sizeof(*p));
    p->uid = g->uid;
    memcpy(p->name, g->name, sizeof(p->name));
    p->name[SIZE_NAME] = '\0';
    p->level = g->level;
    p->stage = g->stage;
    p->nature = g->nature;
    p->color = g->color;
    p->head = g->head;
}

/* Grava um u32 little-endian. */
static void put32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

/* Lê um u32 little-endian. */
static uint32_t get32(const uint8_t *b)
{
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

/* Codifica a mensagem no formato fixo. */
int link_encode(const link_msg_t *m, uint8_t *buf)
{
    memset(buf, 0, LINK_MESSAGE_SIZE);
    buf[0] = 'G';
    buf[1] = 'L';
    buf[2] = LINK_VERSION;
    buf[3] = (uint8_t)m->type;
    put32(buf + 4, m->from.uid);
    memcpy(buf + 8, m->from.name, SIZE_NAME);
    buf[19] = m->from.level;
    buf[20] = m->from.stage;
    buf[21] = m->from.nature;
    buf[22] = m->from.color;
    buf[23] = m->from.head;
    put32(buf + 24, m->tx);
    buf[28] = m->item_id;
    buf[29] = m->count;
    return LINK_MESSAGE_SIZE;
}

/* Decodifica e valida assinatura, versão e tipo. */
bool link_decode(const uint8_t *buf, int length, link_msg_t *m)
{
    if (length < LINK_MESSAGE_SIZE || buf[0] != 'G' || buf[1] != 'L' || buf[2] != LINK_VERSION ||
        buf[3] < LINK_HELLO || buf[3] > LINK_DECLINE) {
        return false;
    }
    memset(m, 0, sizeof(*m));
    m->type = (link_type_t)buf[3];
    m->from.uid = get32(buf + 4);
    memcpy(m->from.name, buf + 8, SIZE_NAME);
    m->from.name[SIZE_NAME] = '\0';
    m->from.level = buf[19];
    m->from.stage = buf[20];
    m->from.nature = buf[21];
    m->from.color = buf[22];
    m->from.head = buf[23];
    m->tx = get32(buf + 24);
    m->item_id = buf[28];
    m->count = buf[29];
    return true;
}

/* Novo vizinho entra no fim da lista; um conhecido só atualiza perfil e horário. */
void link_peers_seen(link_peers_t *t, const uint8_t mac[6], const link_profile_t *p, uint32_t now_ms)
{
    for (int i = 0; i < t->count; i++) {
        if (memcmp(t->peers[i].mac, mac, 6) == 0) {
            t->peers[i].profile = *p;
            t->peers[i].seen_ms = now_ms;
            return;
        }
    }
    if (t->count < LINK_MAX_PEERS) {
        link_peer_t *n = &t->peers[t->count++];
        memcpy(n->mac, mac, 6);
        n->profile = *p;
        n->seen_ms = now_ms;
    }
}

/* Remove os vizinhos silenciosos, mantendo a ordem dos outros. */
void link_peers_expire(link_peers_t *t, uint32_t now_ms)
{
    int kept = 0;
    for (int i = 0; i < t->count; i++) {
        if (now_ms - t->peers[i].seen_ms < LINK_PEER_TIMEOUT_MS) {
            t->peers[kept++] = t->peers[i];
        }
    }
    t->count = kept;
}

/* Verdadeiro se a troca `tx` de `uid` já foi aceita. */
bool link_tx_log_has(const link_tx_log_t *log, uint32_t uid, uint32_t tx)
{
    for (int i = 0; i < LINK_TX_LOG; i++) {
        if (log->tx[i] == tx && log->uid[i] == uid && tx != 0) {
            return true;
        }
    }
    return false;
}

/* Lembra a troca aceita (as mais antigas saem). */
void link_tx_log_add(link_tx_log_t *log, uint32_t uid, uint32_t tx)
{
    log->uid[log->next] = uid;
    log->tx[log->next] = tx;
    log->next = (log->next + 1) % LINK_TX_LOG;
}
