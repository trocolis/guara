/*
 * Protocolo do Conectar (little-endian). Cabeçalho de 23 bytes em toda
 * mensagem: 0-1 "GL", 2 versão, 3 tipo, 4-7 UID, 8-17 nome (10 bytes, sem o
 * '\0'), 18 nível, 19 estágio, 20 natureza, 21 cor, 22 cabeça. Depois, a carga do
 * tipo (ver link_encode). Também a tabela de vizinhos e o registro de ofertas.
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

/* Cursor de escrita e leitura (falha ao passar do fim). */
typedef struct {
    uint8_t *b;
    int pos, size;
    bool ok;
} cursor_t;

static void w8(cursor_t *c, uint32_t v)
{
    if (c->pos < c->size) {
        c->b[c->pos] = (uint8_t)v;
    } else {
        c->ok = false;
    }
    c->pos++;
}
static void w16(cursor_t *c, uint32_t v)
{
    w8(c, v);
    w8(c, v >> 8);
}
static void w32(cursor_t *c, uint32_t v)
{
    w16(c, v);
    w16(c, v >> 16);
}
static void wbytes(cursor_t *c, const void *data, int n)
{
    for (int i = 0; i < n; i++) {
        w8(c, ((const uint8_t *)data)[i]);
    }
}
static uint32_t r8(cursor_t *c)
{
    if (c->pos >= c->size) {
        c->ok = false;
        return 0;
    }
    return c->b[c->pos++];
}
static uint32_t r16(cursor_t *c)
{
    uint32_t v = r8(c);
    return v | r8(c) << 8;
}
static uint32_t r32(cursor_t *c)
{
    uint32_t v = r16(c);
    return v | r16(c) << 16;
}
static void rbytes(cursor_t *c, void *data, int n)
{
    for (int i = 0; i < n; i++) {
        ((uint8_t *)data)[i] = (uint8_t)r8(c);
    }
}

/* Lutador completo (START): 47 bytes. Os espaços de poção do oponente gerado não viajam. */
static void put_fighter(cursor_t *c, const fighter_t *f)
{
    wbytes(c, f->name, SIZE_NAME);
    w8(c, f->nature);
    w8(c, f->color);
    w8(c, f->head);
    w8(c, f->stage);
    w8(c, f->level);
    w16(c, (uint32_t)f->hp);
    w16(c, (uint32_t)f->hp_max);
    w16(c, (uint32_t)f->mp);
    w16(c, (uint32_t)f->mp_max);
    for (int i = 0; i < ATTRIBUTE_COUNT; i++) {
        w16(c, (uint32_t)f->attributes[i]);
    }
    w32(c, f->states);
    wbytes(c, f->skills, COUNT_SKILL_ACTIVE);
    w16(c, (uint32_t)f->atb_ms);
    w16(c, (uint32_t)f->atb_total);
}
static void get_fighter(cursor_t *c, fighter_t *f)
{
    memset(f, 0, sizeof(*f));
    rbytes(c, f->name, SIZE_NAME);
    f->name[SIZE_NAME] = '\0';
    f->nature = (uint8_t)r8(c);
    f->color = (uint8_t)r8(c);
    f->head = (uint8_t)r8(c);
    f->stage = (uint8_t)r8(c);
    f->level = (uint8_t)r8(c);
    f->hp = (int16_t)r16(c);
    f->hp_max = (int16_t)r16(c);
    f->mp = (int16_t)r16(c);
    f->mp_max = (int16_t)r16(c);
    for (int i = 0; i < ATTRIBUTE_COUNT; i++) {
        f->attributes[i] = (int16_t)r16(c);
    }
    f->states = r32(c);
    rbytes(c, f->skills, COUNT_SKILL_ACTIVE);
    f->atb_ms = (int16_t)r16(c);
    f->atb_total = (int16_t)r16(c);
}

/* Vitais dos dois lados e o fim do combate (STATE e RESULT): 26 bytes. */
static void put_vitals(cursor_t *c, const link_msg_t *m)
{
    for (int i = 0; i < 2; i++) {
        w16(c, (uint32_t)m->vitals[i].hp);
        w16(c, (uint32_t)m->vitals[i].mp);
        w32(c, m->vitals[i].states);
        w16(c, (uint32_t)m->vitals[i].atb_ms);
        w16(c, (uint32_t)m->vitals[i].atb_total);
    }
    w8(c, m->finished);
    w8(c, (uint8_t)m->winner);
}
static void get_vitals(cursor_t *c, link_msg_t *m)
{
    for (int i = 0; i < 2; i++) {
        m->vitals[i].hp = (int16_t)r16(c);
        m->vitals[i].mp = (int16_t)r16(c);
        m->vitals[i].states = r32(c);
        m->vitals[i].atb_ms = (int16_t)r16(c);
        m->vitals[i].atb_total = (int16_t)r16(c);
    }
    m->finished = r8(c) != 0;
    m->winner = (int8_t)r8(c);
}

/* Resultado de uma ação (RESULT). */
static void put_result(cursor_t *c, const combat_result_t *r)
{
    w8(c, (uint32_t)r->actor);
    w8(c, (uint32_t)r->target);
    w8(c, r->type);
    w8(c, (uint32_t)(r->failed | r->missed << 1 | r->dodged << 2 | r->critical << 3 | r->timed << 4 |
                     r->guarded << 5 | r->self_hit << 6));
    w8(c, (uint32_t)r->hits);
    w16(c, (uint32_t)r->damage);
    w16(c, (uint32_t)r->heal);
    w16(c, (uint32_t)r->status_damage);
    w8(c, (uint32_t)(int8_t)r->applied_state);
    w16(c, (uint32_t)r->multiplier);
    wbytes(c, r->icon, sizeof(r->icon));
}
static void get_result(cursor_t *c, combat_result_t *r)
{
    memset(r, 0, sizeof(*r));
    r->actor = (int)r8(c);
    r->target = (int)r8(c);
    r->type = (combat_action_type_t)r8(c);
    uint32_t flags = r8(c);
    r->failed = flags & 1;
    r->missed = (flags >> 1) & 1;
    r->dodged = (flags >> 2) & 1;
    r->critical = (flags >> 3) & 1;
    r->timed = (flags >> 4) & 1;
    r->guarded = (flags >> 5) & 1;
    r->self_hit = (flags >> 6) & 1;
    r->hits = (int)r8(c);
    r->damage = (int16_t)r16(c);
    r->heal = (int16_t)r16(c);
    r->status_damage = (int16_t)r16(c);
    r->applied_state = (int8_t)r8(c);
    r->multiplier = (int16_t)r16(c);
    rbytes(c, r->icon, sizeof(r->icon));
    r->icon[sizeof(r->icon) - 1] = '\0';
}

/* Codifica: cabeçalho e a carga do tipo. */
int link_encode(const link_msg_t *m, uint8_t *buf)
{
    cursor_t c = {buf, 0, LINK_MAX_SIZE, true};
    memset(buf, 0, LINK_MAX_SIZE);
    w8(&c, 'G');
    w8(&c, 'L');
    w8(&c, LINK_VERSION);
    w8(&c, m->type);
    w32(&c, m->from.uid);
    wbytes(&c, m->from.name, SIZE_NAME);
    w8(&c, m->from.level);
    w8(&c, m->from.stage);
    w8(&c, m->from.nature);
    w8(&c, m->from.color);
    w8(&c, m->from.head);
    switch (m->type) {
    case LINK_OFFER:
    case LINK_ACCEPT:
    case LINK_DECLINE:
    case LINK_CHALLENGE:
        w32(&c, m->tx);
        w8(&c, m->item_id);
        w8(&c, m->count);
        break;
    case LINK_START:
        w16(&c, m->seq);
        put_fighter(&c, &m->fighters[0]);
        put_fighter(&c, &m->fighters[1]);
        break;
    case LINK_STATE:
        w16(&c, m->seq);
        put_vitals(&c, m);
        w8(&c, m->guest_pending);
        break;
    case LINK_WINDUP:
        w16(&c, m->seq);
        w8(&c, m->side);
        w8(&c, m->action.type);
        w8(&c, (uint32_t)m->action.position);
        break;
    case LINK_RESULT:
        w16(&c, m->seq);
        put_result(&c, &m->result);
        put_vitals(&c, m);
        break;
    case LINK_INPUT:
        w16(&c, m->seq);
        w8(&c, m->action.type);
        w8(&c, (uint32_t)m->action.position);
        break;
    case LINK_REACT:
        w16(&c, m->seq);
        w8(&c, m->hit);
        break;
    default: /* HELLO e BYE: só o cabeçalho */
        break;
    }
    return c.ok ? c.pos : 0;
}

/* Decodifica e valida assinatura, versão, tipo e tamanho da carga. */
bool link_decode(const uint8_t *buf, int length, link_msg_t *m)
{
    if (length < LINK_HEADER_SIZE || buf[0] != 'G' || buf[1] != 'L' || buf[2] != LINK_VERSION ||
        buf[3] < LINK_HELLO || buf[3] > LINK_BYE) {
        return false;
    }
    cursor_t c = {(uint8_t *)buf, 4, length, true};
    memset(m, 0, sizeof(*m));
    m->type = (link_type_t)buf[3];
    m->from.uid = r32(&c);
    rbytes(&c, m->from.name, SIZE_NAME);
    m->from.name[SIZE_NAME] = '\0';
    m->from.level = (uint8_t)r8(&c);
    m->from.stage = (uint8_t)r8(&c);
    m->from.nature = (uint8_t)r8(&c);
    m->from.color = (uint8_t)r8(&c);
    m->from.head = (uint8_t)r8(&c);
    switch (m->type) {
    case LINK_OFFER:
    case LINK_ACCEPT:
    case LINK_DECLINE:
    case LINK_CHALLENGE:
        m->tx = r32(&c);
        m->item_id = (uint8_t)r8(&c);
        m->count = (uint8_t)r8(&c);
        break;
    case LINK_START:
        m->seq = (uint16_t)r16(&c);
        get_fighter(&c, &m->fighters[0]);
        get_fighter(&c, &m->fighters[1]);
        break;
    case LINK_STATE:
        m->seq = (uint16_t)r16(&c);
        get_vitals(&c, m);
        m->guest_pending = r8(&c) != 0;
        break;
    case LINK_WINDUP:
        m->seq = (uint16_t)r16(&c);
        m->side = (uint8_t)r8(&c);
        m->action.type = (combat_action_type_t)r8(&c);
        m->action.position = (int)r8(&c);
        break;
    case LINK_RESULT:
        m->seq = (uint16_t)r16(&c);
        get_result(&c, &m->result);
        get_vitals(&c, m);
        break;
    case LINK_INPUT:
        m->seq = (uint16_t)r16(&c);
        m->action.type = (combat_action_type_t)r8(&c);
        m->action.position = (int)r8(&c);
        break;
    case LINK_REACT:
        m->seq = (uint16_t)r16(&c);
        m->hit = r8(&c) != 0;
        break;
    default:
        break;
    }
    return c.ok;
}

/* Vitais atuais de um lutador. */
void link_vitals_from(const fighter_t *f, link_vitals_t *v)
{
    v->hp = f->hp;
    v->mp = f->mp;
    v->states = f->states;
    v->atb_ms = f->atb_ms;
    v->atb_total = f->atb_total;
}

/* Aplica os vitais recebidos a um lutador. */
void link_vitals_apply(const link_vitals_t *v, fighter_t *f)
{
    f->hp = v->hp;
    f->mp = v->mp;
    f->states = v->states;
    f->atb_ms = v->atb_ms;
    f->atb_total = v->atb_total;
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
