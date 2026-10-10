/*
 * Encontro: lista os Guarás por perto (ESP-NOW) e troca itens entre eles.
 * Aberto por Ajustes > Encontro (só recebe) ou por Inventário > Compartilhar
 * (envia o item escolhido para quem for selecionado). Enquanto a tela está
 * aberta, o rádio fica ligado e o aparelho não entra em modo Ocioso.
 *
 * Troca segura: quem envia repete a oferta até a resposta e só desconta o
 * item ao receber a aceitação; quem recebe soma uma vez por oferta (UID + tx)
 * e responde de novo às repetições.
 */
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "audio.h"
#include "esp_random.h"
#include "link_proto.h"
#include "modal.h"
#include "radio_link.h"
#include "screens.h"
#include "storage.h"
#include "widgets.h"

#define OFFER_RETRY_MS 700     /* repete a oferta até a resposta */
#define OFFER_TIMEOUT_MS 30000 /* desiste sem resposta */

static struct {
    bool started;
    link_peer_t peers[LINK_MAX_PEERS];
    int n_peers;
    ui_list_t list;
    /* Envio (Inventário > Compartilhar); item -1 = só recebe */
    int item_id, count;
    bool waiting, applied;
    uint8_t to_mac[6];
    uint32_t to_uid, tx, sent_at, last_try;
    char to_name[SIZE_NAME + 1];
    /* Oferta recebida aguardando a resposta do usuário */
    bool prompt_open;
    uint8_t prompt_mac[6];
    link_msg_t prompt;
} meet;

/* Ofertas respondidas: repetidas, recebem a mesma resposta (sobrevivem entre aberturas). */
static link_tx_log_t accepted, declined;

/* Nome do item no catálogo (ou "item"). */
static void item_name(int id, char *buf, int size)
{
    catalog_record_t r;
    snprintf(buf, (size_t)size, "%s", catalog_read(CAT_ITEMS, id, &r) ? r.name : "item");
}

/* Responde a uma oferta. */
static void reply(const uint8_t mac[6], link_type_t type, uint32_t tx)
{
    link_msg_t m = {.type = type, .tx = tx};
    radio_link_send(mac, &m);
}

/* Resposta do usuário à oferta recebida: soma os itens e aceita, ou recusa. */
static void offer_answer(int r, void *ctx)
{
    (void)ctx;
    meet.prompt_open = false;
    link_msg_t *o = &meet.prompt;
    if (r != 1) {
        link_tx_log_add(&declined, o->from.uid, o->tx);
        reply(meet.prompt_mac, LINK_DECLINE, o->tx);
        return;
    }
    /* O inventário guarda até 255 de cada item: o que passar disso se perde */
    int have = g_guara.inventory[o->item_id];
    int added = have + o->count > 255 ? 255 - have : o->count;
    g_guara.inventory[o->item_id] = (uint8_t)(have + added);
    app_mark_changed();
    app_save();
    link_tx_log_add(&accepted, o->from.uid, o->tx);
    reply(meet.prompt_mac, LINK_ACCEPT, o->tx);
    audio_level_up();
    char name[32], msg[64];
    item_name(o->item_id, name, sizeof(name));
    snprintf(msg, sizeof(msg), "Recebeu %d %s de %s!", o->count, name, o->from.name);
    ui_alert("ENCONTRO", "inventory", msg, "OK", NULL, NULL);
}

/* Oferta recebida: repetição responde de novo; nova pergunta ao usuário. */
static void handle_offer(const uint8_t mac[6], const link_msg_t *m)
{
    catalog_record_t r;
    if (link_tx_log_has(&accepted, m->from.uid, m->tx)) {
        reply(mac, LINK_ACCEPT, m->tx);
        return;
    }
    if (link_tx_log_has(&declined, m->from.uid, m->tx)) {
        reply(mac, LINK_DECLINE, m->tx);
        return;
    }
    if (meet.prompt_open || meet.waiting) {
        return; /* ocupado: a oferta se repete e é atendida depois */
    }
    if (m->count == 0 || m->item_id >= COUNT_ITEMS || !catalog_read(CAT_ITEMS, m->item_id, &r)) {
        link_tx_log_add(&declined, m->from.uid, m->tx); /* item que este catálogo não tem */
        reply(mac, LINK_DECLINE, m->tx);
        return;
    }
    meet.prompt_open = true;
    meet.prompt = *m;
    memcpy(meet.prompt_mac, mac, 6);
    char msg[80];
    snprintf(msg, sizeof(msg), "%s quer te dar %d %s. Aceitar?", m->from.name, m->count, r.name);
    audio_beep();
    ui_confirm("ENCONTRO", r.icon, msg, "NAO", "SIM", offer_answer, NULL);
}

/* Fecha o Encontro depois do envio concluído (volta ao Inventário). */
static void sent_done(int r, void *ctx)
{
    (void)r;
    (void)ctx;
    ui_back();
}

/* Resposta à oferta enviada: aceita desconta o item uma única vez. */
static void handle_answer(const link_msg_t *m)
{
    if (meet.item_id < 0 || m->tx != meet.tx || m->from.uid != meet.to_uid) {
        return;
    }
    char name[32], msg[64];
    item_name(meet.item_id, name, sizeof(name));
    if (m->type == LINK_ACCEPT && !meet.applied) {
        meet.applied = true;
        meet.waiting = false;
        int have = g_guara.inventory[meet.item_id];
        g_guara.inventory[meet.item_id] = (uint8_t)(have > meet.count ? have - meet.count : 0);
        app_mark_changed();
        app_save();
        audio_level_up();
        snprintf(msg, sizeof(msg), "%s recebeu %d %s!", meet.to_name, meet.count, name);
        ui_alert("ENCONTRO", "inventory", msg, "OK", sent_done, NULL);
    } else if (m->type == LINK_DECLINE && meet.waiting) {
        meet.waiting = false;
        audio_error();
        snprintf(msg, sizeof(msg), "%s recusou.", meet.to_name);
        ui_alert("ENCONTRO", "inventory", msg, "OK", NULL, NULL);
    }
}

/* Envia a oferta (ou a repete) para o Guará escolhido. */
static void send_offer(uint32_t now)
{
    link_msg_t m = {.type = LINK_OFFER, .tx = meet.tx, .item_id = (uint8_t)meet.item_id, .count = (uint8_t)meet.count};
    radio_link_send(meet.to_mac, &m);
    meet.last_try = now;
}

/* Mensagens recebidas, repetição da oferta e tempo limite. */
static void poll(uint32_t now)
{
    uint8_t mac[6];
    link_msg_t m;
    while (radio_link_receive(mac, &m)) {
        if (m.type == LINK_OFFER) {
            handle_offer(mac, &m);
        } else {
            handle_answer(&m);
        }
    }
    meet.n_peers = radio_link_peers(meet.peers, LINK_MAX_PEERS);
    meet.list.total = meet.n_peers;
    if (meet.list.sel >= meet.list.total) {
        meet.list.sel = meet.list.total > 0 ? meet.list.total - 1 : 0;
    }
    if (meet.waiting) {
        if (now - meet.sent_at >= OFFER_TIMEOUT_MS) {
            meet.waiting = false;
            audio_error();
            char msg[48];
            snprintf(msg, sizeof(msg), "%s não respondeu.", meet.to_name);
            ui_alert("ENCONTRO", "inventory", msg, "OK", NULL, NULL);
        } else if (now - meet.last_try >= OFFER_RETRY_MS) {
            send_offer(now);
        }
    }
}

/* Linha da lista: natureza, nome e nível. */
static void peer_item(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    const link_profile_t *p = &meet.peers[i].profile;
    it->icon = nature_icon((nature_t)p->nature);
    snprintf(it->text, sizeof(it->text), "%s", p->name);
    snprintf(it->info, sizeof(it->info), "NV%d", p->level);
}

/* A envia a oferta ao Guará selecionado (no modo envio); B volta ou cancela a espera. */
static void on_key(const key_event_t *ev)
{
    if (!meet.waiting && ui_list_key(&meet.list, ev, UI_LINES_VISIBLE)) {
        return;
    }
    if (ev->type != KEY_PRESSED) {
        return;
    }
    if (ev->key == KEY_B) {
        if (meet.waiting) {
            meet.waiting = false; /* uma aceitação que chegar depois ainda desconta o item */
        } else {
            ui_back();
        }
        return;
    }
    if (ev->key != KEY_A || meet.waiting || meet.n_peers == 0) {
        return;
    }
    if (meet.item_id < 0) {
        ui_alert("ENCONTRO", "inventory", "Para dar um item, use Inventário > Compartilhar.", "OK", NULL, NULL);
        return;
    }
    if (meet.applied || g_guara.inventory[meet.item_id] < meet.count) {
        ui_alert("ENCONTRO", "inventory", "Sem estoque.", "OK", NULL, NULL);
        return;
    }
    const link_peer_t *p = &meet.peers[meet.list.sel];
    memcpy(meet.to_mac, p->mac, 6);
    meet.to_uid = p->profile.uid;
    snprintf(meet.to_name, sizeof(meet.to_name), "%s", p->profile.name);
    meet.tx = esp_random() | 1u; /* 0 fica reservado para "sem troca" */
    meet.waiting = true;
    meet.sent_at = app_ms();
    send_offer(meet.sent_at);
}

/* Lista dos Guarás por perto, ou a busca, ou a espera pela resposta. */
static void on_draw(gfx_t *g, uint32_t now)
{
    poll(now);
    ui_title(g, "ENCONTRO", now);
    char line[48];
    if (!meet.started) {
        gfx_text_center(g, 0, GFX_WIDTH, 28, "Rádio não iniciou.", GFX_WHITE);
        return;
    }
    if (meet.waiting) {
        snprintf(line, sizeof(line), "Aguardando %s...", meet.to_name);
        ui_paragraph(g, 4, 24, GFX_WIDTH - 8, 2, line, true);
        ui_footer(g, "CANCELAR", NULL, now);
        return;
    }
    if (meet.n_peers == 0) {
        gfx_text_center(g, 0, GFX_WIDTH, 24, "Procurando Guarás...", GFX_WHITE);
        if (meet.item_id >= 0) {
            char name[32];
            item_name(meet.item_id, name, sizeof(name));
            snprintf(line, sizeof(line), "Dar %d %s", meet.count, name);
            gfx_text_center(g, 0, GFX_WIDTH, 38, line, GFX_WHITE);
        }
        return;
    }
    ui_list_draw(g, &meet.list, UI_LIST_Y, UI_LINES_VISIBLE, peer_item, NULL, now);
}

/* Liga o rádio do Encontro com o perfil do Guará e segura o sono. */
static void on_open(void *param)
{
    (void)param;
    link_profile_t me;
    link_profile_from_guara(&g_guara, &me);
    meet.started = radio_link_start(&me);
    app_block_sleep(true);
}

/* Desliga o rádio e devolve o sono e a rede. */
static void on_close(void)
{
    radio_link_stop();
    app_block_sleep(false);
}

static const screen_t SCREEN_MEET = {
    .name = "encontro",
    .on_open = on_open,
    .on_close = on_close,
    .on_key = on_key,
    .on_draw = on_draw,
};

/* Abre o Encontro: `item_id` -1 só recebe; senão, envia `count` unidades do item. */
void screens_open_meet(int item_id, int count)
{
    memset(&meet, 0, sizeof(meet));
    meet.item_id = item_id;
    meet.count = count;
    ui_open(&SCREEN_MEET, NULL);
}
