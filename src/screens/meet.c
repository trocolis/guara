/*
 * Conectar: o sub-menu do menu principal (Buscar Guará, Visível e as conexões
 * aceitas) e a busca, que lista os Guarás por perto (ESP-NOW), troca itens e desafia
 * para combates. A busca abre por Conectar > Buscar Guará ou Arena > Combate
 * (A desafia o Guará escolhido), por uma conexão aceita (desafia quando ela
 * aparecer) ou por Inventário > Compartilhar (A envia o item). Enquanto a tela está
 * aberta, o rádio fica ligado e o aparelho não entra em modo Ocioso. Com
 * Visível em ON, o rádio também fica ligado fora dela enquanto o aparelho está
 * em uso (app.c), e outros Guarás podem encontrá-lo e mandar ofertas e desafios.
 *
 * Troca segura: quem envia repete a oferta até a resposta e só desconta o
 * item ao receber a aceitação; quem recebe soma uma vez por oferta (UID + tx)
 * e responde de novo às repetições.
 */
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "audio.h"
#include "config.h"
#include "esp_random.h"
#include "link_proto.h"
#include "modal.h"
#include "radio_link.h"
#include "screens.h"
#include "storage.h"
#include "widgets.h"

#define OFFER_RETRY_MS 700     /* repete a oferta até a resposta */
#define OFFER_TIMEOUT_MS 30000 /* desiste sem resposta */
#define RESULT_LINES 2         /* resultados visíveis no modal da busca, acima do rodapé */
#define RESULT_LINE_H 11

static struct {
    bool started;
    link_peer_t peers[LINK_MAX_PEERS];
    int n_peers;
    ui_list_t list;
    /* Envio (Inventário > Compartilhar); item -1 = só recebe */
    int item_id, count;
    bool waiting, applied;
    link_type_t waiting_for; /* LINK_OFFER (item) ou LINK_CHALLENGE (combate) */
    uint8_t to_mac[6];
    uint32_t to_uid, tx, sent_at, last_try;
    char to_name[SIZE_NAME + 1];
    bool open; /* a busca está na pilha de telas */
    /* Oferta recebida aguardando a resposta do usuário */
    bool prompt_open;
    uint8_t prompt_mac[6];
    link_msg_t prompt;
    link_peer_t challenge_peer; /* escolhido para o desafio, à espera da confirmação */
    uint32_t target_uid;        /* conexão aceita a desafiar assim que aparecer (0 = nenhuma) */
    char target_name[SIZE_NAME + 1];
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

/* OK no aviso do fim da troca: fecha a busca e volta para onde se estava. */
static void close_after_alert(int r, void *ctx)
{
    (void)r;
    (void)ctx;
    screens_close_meet();
}

/* Guarda quem aceitou ou teve a oferta/desafio aceito na lista de conexões. */
static void remember(const link_profile_t *p)
{
    config_save_connection(p->uid, p->name, p->level);
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
        screens_close_meet(); /* volta para onde se estava antes da oferta */
        return;
    }
    /* O inventário guarda até 255 de cada item: o que passar disso se perde */
    int have = g_guara.inventory[o->item_id];
    int added = have + o->count > 255 ? 255 - have : o->count;
    g_guara.inventory[o->item_id] = (uint8_t)(have + added);
    app_mark_changed();
    app_save();
    link_tx_log_add(&accepted, o->from.uid, o->tx);
    remember(&o->from);
    reply(meet.prompt_mac, LINK_ACCEPT, o->tx);
    audio_level_up();
    char name[32], msg[64];
    item_name(o->item_id, name, sizeof(name));
    snprintf(msg, sizeof(msg), "Recebeu %d %s de %s!", o->count, name, o->from.name);
    ui_alert("CONECTAR", "inventory", msg, "OK", close_after_alert, NULL);
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
    ui_confirm("CONECTAR", r.icon, msg, "NAO", "SIM", offer_answer, NULL);
}

/* Resposta do usuário ao desafio recebido: aceitar abre o combate como convidado. */
static void challenge_answer(int r, void *ctx)
{
    (void)ctx;
    meet.prompt_open = false;
    link_msg_t *c = &meet.prompt;
    if (r != 1) {
        link_tx_log_add(&declined, c->from.uid, c->tx);
        reply(meet.prompt_mac, LINK_DECLINE, c->tx);
        screens_close_meet(); /* volta para onde se estava antes do desafio */
        return;
    }
    link_tx_log_add(&accepted, c->from.uid, c->tx);
    remember(&c->from);
    reply(meet.prompt_mac, LINK_ACCEPT, c->tx);
    arena_open_versus(meet.prompt_mac, c->from.uid, c->from.name, false);
}

/* Desafio recebido: sem condições de lutar, recusa sozinho; senão pergunta. */
static void handle_challenge(const uint8_t mac[6], const link_msg_t *m)
{
    if (link_tx_log_has(&declined, m->from.uid, m->tx)) {
        reply(mac, LINK_DECLINE, m->tx);
        return;
    }
    if (link_tx_log_has(&accepted, m->from.uid, m->tx)) {
        return; /* já está no combate (a tela de combate responde as repetições) */
    }
    if (meet.prompt_open || meet.waiting) {
        return;
    }
    const char *icon;
    if (arena_cannot_fight(&icon)) {
        link_tx_log_add(&declined, m->from.uid, m->tx);
        reply(mac, LINK_DECLINE, m->tx);
        return;
    }
    meet.prompt_open = true;
    meet.prompt = *m;
    memcpy(meet.prompt_mac, mac, 6);
    char msg[64];
    snprintf(msg, sizeof(msg), "%s (NV%d) te desafiou! Aceitar?", m->from.name, m->from.level);
    audio_beep();
    ui_confirm("COMBATE", "fight", msg, "NAO", "SIM", challenge_answer, NULL);
}

/* Fecha o Conectar depois do envio concluído (volta ao Inventário). */
static void sent_done(int r, void *ctx)
{
    (void)r;
    (void)ctx;
    screens_close_meet();
}

/* Resposta à oferta enviada: aceita desconta o item uma única vez. */
static void handle_answer(const uint8_t mac[6], const link_msg_t *m)
{
    if (m->tx != meet.tx || m->from.uid != meet.to_uid) {
        return;
    }
    if (meet.waiting_for == LINK_CHALLENGE) {
        if (!meet.waiting) {
            return;
        }
        meet.waiting = false;
        if (m->type == LINK_ACCEPT) {
            remember(&m->from);
            arena_open_versus(mac, meet.to_uid, meet.to_name, true); /* quem desafiou é o anfitrião */
        } else {
            char msg[48];
            audio_error();
            snprintf(msg, sizeof(msg), "%s recusou o desafio.", meet.to_name);
            ui_alert("CONECTAR", "fight", msg, "OK", NULL, NULL);
        }
        return;
    }
    char name[32], msg[64];
    item_name(meet.item_id, name, sizeof(name));
    if (m->type == LINK_ACCEPT && !meet.applied) {
        meet.applied = true;
        meet.waiting = false;
        remember(&m->from);
        int have = g_guara.inventory[meet.item_id];
        g_guara.inventory[meet.item_id] = (uint8_t)(have > meet.count ? have - meet.count : 0);
        app_mark_changed();
        app_save();
        audio_level_up();
        snprintf(msg, sizeof(msg), "%s recebeu %d %s!", meet.to_name, meet.count, name);
        ui_alert("CONECTAR", "inventory", msg, "OK", sent_done, NULL);
    } else if (m->type == LINK_DECLINE && meet.waiting) {
        meet.waiting = false;
        audio_error();
        snprintf(msg, sizeof(msg), "%s recusou.", meet.to_name);
        ui_alert("CONECTAR", "inventory", msg, "OK", NULL, NULL);
    }
}

/* Envia a oferta (ou a repete) para o Guará escolhido. */
static void send_offer(uint32_t now)
{
    link_msg_t m = {.type = meet.waiting_for, .tx = meet.tx};
    if (meet.waiting_for == LINK_OFFER) {
        m.item_id = (uint8_t)meet.item_id;
        m.count = (uint8_t)meet.count;
    }
    radio_link_send(meet.to_mac, &m);
    meet.last_try = now;
}

static void start_waiting(const link_peer_t *p, link_type_t what);

/* Mensagens recebidas, repetição da oferta e tempo limite. */
static void poll(uint32_t now)
{
    uint8_t mac[6];
    link_msg_t m;
    while (radio_link_receive(mac, &m)) {
        if (m.type == LINK_OFFER) {
            handle_offer(mac, &m);
        } else if (m.type == LINK_CHALLENGE) {
            handle_challenge(mac, &m);
        } else if (m.type == LINK_ACCEPT || m.type == LINK_DECLINE) {
            handle_answer(mac, &m);
        }
    }
    meet.n_peers = radio_link_peers(meet.peers, LINK_MAX_PEERS);
    meet.list.total = meet.n_peers;
    /* Conexão aceita escolhida no sub-menu: desafia assim que ela aparecer */
    if (meet.target_uid && !meet.waiting && !meet.prompt_open) {
        for (int i = 0; i < meet.n_peers; i++) {
            if (meet.peers[i].profile.uid == meet.target_uid) {
                meet.target_uid = 0;
                start_waiting(&meet.peers[i], LINK_CHALLENGE);
                break;
            }
        }
    }
    if (meet.list.sel >= meet.list.total) {
        meet.list.sel = meet.list.total > 0 ? meet.list.total - 1 : 0;
    }
    if (meet.waiting) {
        if (now - meet.sent_at >= OFFER_TIMEOUT_MS) {
            meet.waiting = false;
            audio_error();
            char msg[48];
            snprintf(msg, sizeof(msg), "%s não respondeu.", meet.to_name);
            ui_alert("CONECTAR", "inventory", msg, "OK", NULL, NULL);
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

/* Começa a repetir a oferta ou o desafio para `p` até a resposta. */
static void start_waiting(const link_peer_t *p, link_type_t what)
{
    memcpy(meet.to_mac, p->mac, 6);
    meet.to_uid = p->profile.uid;
    snprintf(meet.to_name, sizeof(meet.to_name), "%s", p->profile.name);
    meet.tx = esp_random() | 1u; /* 0 fica reservado para "sem troca" */
    meet.waiting_for = what;
    meet.waiting = true;
    meet.sent_at = app_ms();
    send_offer(meet.sent_at);
}

/* Confirmou o desafio: envia e espera a resposta. */
static void challenge_confirmed(int r, void *ctx)
{
    (void)ctx;
    if (r == 1) {
        start_waiting(&meet.challenge_peer, LINK_CHALLENGE);
    }
}

/* A envia a oferta (modo envio) ou desafia o Guará selecionado; B volta ou cancela a espera. */
static void on_key(const key_event_t *ev)
{
    if (!meet.waiting && ui_list_key(&meet.list, ev, RESULT_LINES)) {
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
    const link_peer_t *p = &meet.peers[meet.list.sel];
    if (meet.item_id < 0) {
        const char *icon, *reason = arena_cannot_fight(&icon);
        if (reason) {
            ui_alert("COMBATE", icon, reason, "OK", NULL, NULL);
            return;
        }
        meet.challenge_peer = *p;
        char msg[64];
        snprintf(msg, sizeof(msg), "Desafiar %s (NV%d) para um combate?", p->profile.name, p->profile.level);
        ui_confirm("COMBATE", "fight", msg, "NAO", "SIM", challenge_confirmed, NULL);
        return;
    }
    if (meet.applied || g_guara.inventory[meet.item_id] < meet.count) {
        ui_alert("CONECTAR", "inventory", "Sem estoque.", "OK", NULL, NULL);
        return;
    }
    start_waiting(p, LINK_OFFER);
}

/*
 * Ícone à esquerda e até duas linhas ao lado, centralizadas no corpo do modal
 * (busca, espera). Com `dots` em 1 ou 2, essa linha ganha as reticências
 * animadas, na posição do texto completo para não tremer.
 */
static void draw_status(gfx_t *g, const char *icon, const char *first, const char *second, int dots, uint32_t now)
{
    const int x = UI_MODAL_X + 24, width = UI_MODAL_W - 28;
    ui_icon(g, icon, UI_MODAL_X + 4, UI_MODAL_BODY_Y + 5);
    int y = second ? UI_MODAL_BODY_Y + 4 : UI_MODAL_BODY_Y + 9;
    const char *lines[2] = {first, second};
    for (int i = 0; i < 2 && lines[i]; i++, y += 9) {
        if (dots == i + 1) {
            ui_text_dots(g, x, width, y, lines[i], now, GFX_WHITE);
        } else {
            gfx_text_center(g, x, width, y, lines[i], GFX_WHITE);
        }
    }
}

/* Resultados da busca: até 2 linhas com nome e nível; seleção em retângulo arredondado. */
static void draw_results(gfx_t *g, uint32_t now)
{
    ui_list_adjust(&meet.list, RESULT_LINES);
    for (int i = 0; i < RESULT_LINES && meet.list.top + i < meet.n_peers; i++) {
        int idx = meet.list.top + i, y = UI_MODAL_BODY_Y + 2 + i * RESULT_LINE_H;
        ui_item_t it = {0};
        peer_item(idx, &it, NULL);
        bool sel = idx == meet.list.sel;
        ui_marquee_text(g, UI_MODAL_X + 6, y + 2, 72, it.text, GFX_WHITE, sel, &meet.list.marquee, now);
        gfx_text(g, UI_MODAL_X + UI_MODAL_W - 8 - gfx_text_width(it.info), y + 2, it.info, GFX_WHITE);
        if (sel) {
            gfx_rect_rounded(g, UI_MODAL_X + 2, y, UI_MODAL_W - 7, RESULT_LINE_H, 2, GFX_WHITE);
        }
    }
    if (meet.n_peers > RESULT_LINES) {
        int bar = RESULT_LINES * RESULT_LINE_H, length = bar * RESULT_LINES / meet.n_peers;
        int y = UI_MODAL_BODY_Y + 2 + (bar - length) * meet.list.top / (meet.n_peers - RESULT_LINES);
        gfx_line_v(g, UI_MODAL_X + UI_MODAL_W - 3, y, length, GFX_WHITE);
    }
}

/*
 * Modal da busca: "Buscando..." com o ícone até alguém aparecer, depois os
 * resultados; também a espera pela resposta e a busca de uma conexão aceita.
 */
static void on_draw(gfx_t *g, uint32_t now)
{
    poll(now);
    ui_modal_frame(g, meet.item_id >= 0 ? "COMPARTILHAR" : "BUSCAR GUARA", now);
    if (!meet.started) {
        draw_status(g, "connect", "Rádio não", "iniciou.", 0, now);
        ui_footer_modal(g, UI_MODAL_FOOTER, "SAIR", NULL, now);
        return;
    }
    if (meet.waiting) {
        draw_status(g, "connect", "Aguardando", meet.to_name, 2, now);
        ui_footer_modal(g, UI_MODAL_FOOTER, "SAIR", NULL, now);
        return;
    }
    if (meet.target_uid) {
        draw_status(g, "search", "Buscando", meet.target_name, 2, now);
        ui_footer_modal(g, UI_MODAL_FOOTER, "SAIR", NULL, now);
        return;
    }
    if (meet.n_peers == 0) {
        if (meet.item_id >= 0) {
            char name[32], line[48];
            item_name(meet.item_id, name, sizeof(name));
            snprintf(line, sizeof(line), "Dar %d %s", meet.count, name);
            draw_status(g, "search", "Buscando", line, 1, now);
        } else {
            draw_status(g, "search", "Buscando", NULL, 1, now);
        }
        ui_footer_modal(g, UI_MODAL_FOOTER, "SAIR", NULL, now);
        return;
    }
    draw_results(g, now);
    ui_footer_modal(g, UI_MODAL_FOOTER, "SAIR", meet.item_id >= 0 ? "DAR" : "LUTAR", now);
}

/* Liga o rádio do Conectar com o perfil do Guará e segura o sono. */
static void on_open(void *param)
{
    (void)param;
    link_profile_t me;
    link_profile_from_guara(&g_guara, &me);
    meet.started = radio_link_start(&me);
    meet.open = true;
    app_block_sleep(true);
}

/* Desliga o rádio e devolve o sono e a rede. */
static void on_close(void)
{
    radio_link_stop();
    meet.open = false;
    app_block_sleep(false);
}

static const screen_t SCREEN_MEET = {
    .name = "busca_guara",
    .modal = true, /* desenhada como modal sobre a tela que a abriu */
    .on_open = on_open,
    .on_close = on_close,
    .on_key = on_key,
    .on_draw = on_draw,
};

/* Fecha o que estiver sobre a busca e a própria busca, voltando à tela de antes dela. */
void screens_close_meet(void)
{
    if (!meet.open) {
        ui_back();
        return;
    }
    ui_back_until(&SCREEN_MEET);
    ui_back();
}

/* Abre o Conectar: `item_id` -1 só recebe; senão, envia `count` unidades do item. */
void screens_open_meet(int item_id, int count)
{
    memset(&meet, 0, sizeof(meet));
    meet.item_id = item_id;
    meet.count = count;
    ui_open(&SCREEN_MEET, NULL);
}

/* ------------------------------------------------------------------------- */
/* Sub-menu Conectar (menu principal): Buscar Guará e as conexões aceitas     */
/* ------------------------------------------------------------------------- */

static struct {
    saved_connection_t connections[CONFIG_MAX_CONNECTIONS];
    int count;
    ui_list_t list;
} menu;

#define MENU_SEARCH 0
#define MENU_VISIBLE 1
#define MENU_FIRST_CONNECTION 2

/* Relê as conexões aceitas (podem ter mudado numa busca). */
static void menu_reload(void)
{
    menu.count = config_connections(menu.connections);
    menu.list.total = MENU_FIRST_CONNECTION + menu.count;
    if (menu.list.sel >= menu.list.total) {
        menu.list.sel = menu.list.total - 1;
    }
    ui_list_adjust(&menu.list, UI_LINES_VISIBLE);
}

/* Buscar Guará, Visível e as conexões, da mais recente para a mais antiga. */
static void menu_item(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    if (i == MENU_SEARCH) {
        it->icon = "search";
        snprintf(it->text, sizeof(it->text), "Buscar Guará");
        return;
    }
    if (i == MENU_VISIBLE) {
        it->icon = "visible";
        snprintf(it->text, sizeof(it->text), "Visível");
        snprintf(it->info, sizeof(it->info), "%s", g_cfg.link_visible ? "ON" : "OFF");
        return;
    }
    const saved_connection_t *c = &menu.connections[menu.count + MENU_FIRST_CONNECTION - 1 - i];
    it->icon = "guara";
    snprintf(it->text, sizeof(it->text), "%s", c->name);
    snprintf(it->info, sizeof(it->info), "NV%d", c->level);
}

/* Posição na lista salva da conexão selecionada. */
static int menu_connection(void)
{
    return menu.count + MENU_FIRST_CONNECTION - 1 - menu.list.sel;
}

/* Confirmou esquecer a conexão. */
static void forget_answer(int r, void *ctx)
{
    (void)ctx;
    if (r == 1) {
        config_forget_connection(menu_connection());
        menu_reload();
    }
}

/* Abre a busca e desafia a conexão `c` assim que ela aparecer. */
static void challenge_connection(const saved_connection_t *c)
{
    const char *icon, *reason = arena_cannot_fight(&icon);
    if (reason) {
        ui_alert("COMBATE", icon, reason, "OK", NULL, NULL);
        return;
    }
    screens_open_meet(-1, 0);
    meet.target_uid = c->uid;
    snprintf(meet.target_name, sizeof(meet.target_name), "%s", c->name);
}

/* Ação escolhida para a conexão: desafiar ou esquecer. */
static void connection_action(int r, void *ctx)
{
    (void)ctx;
    const saved_connection_t *c = &menu.connections[menu_connection()];
    if (r == 0) {
        challenge_connection(c);
    } else if (r == 1) {
        char msg[48];
        snprintf(msg, sizeof(msg), "Esquecer %s?", c->name);
        ui_confirm("CONECTAR", "guara", msg, "NAO", "SIM", forget_answer, NULL);
    }
}

/* A abre a busca, alterna Visível (o app liga ou desliga o rádio em até 1 s) ou abre as ações da conexão; B volta. */
static void menu_key(const key_event_t *ev)
{
    if (ui_list_key(&menu.list, ev, UI_LINES_VISIBLE) || ev->type != KEY_PRESSED) {
        return;
    }
    if (ev->key == KEY_B) {
        ui_back();
    } else if (ev->key == KEY_A) {
        if (menu.list.sel == MENU_SEARCH) {
            screens_open_meet(-1, 0);
        } else if (menu.list.sel == MENU_VISIBLE) {
            g_cfg.link_visible = !g_cfg.link_visible;
            config_save();
        } else {
            static const char *const options[] = {"Desafiar", "Esquecer"};
            ui_choice(menu.connections[menu_connection()].name, options, 2, connection_action, NULL);
        }
    }
}

/* Título e lista. */
static void menu_draw(gfx_t *g, uint32_t now)
{
    ui_title(g, "CONECTAR", now);
    ui_list_draw(g, &menu.list, UI_LIST_Y, UI_LINES_VISIBLE, menu_item, NULL, now);
}

/* Abre com a seleção no topo. */
static void menu_open(void *param)
{
    (void)param;
    menu.list = (ui_list_t){0};
    menu_reload();
}

const screen_t SCREEN_CONNECT = {
    .name = "conectar",
    .on_open = menu_open,
    .on_resume = menu_reload,
    .on_key = menu_key,
    .on_draw = menu_draw,
};
