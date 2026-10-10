/*
 * Conectar: o sub-menu do menu principal (Visível, Buscar Guará e Conhecidos)
 * e a busca, que lista os Guarás por perto (ESP-NOW), troca itens e desafia
 * para combates. A busca abre por Conectar > Buscar Guará ou Arena > Combate
 * (A desafia o Guará escolhido), por um conhecido (desafia ou envia o item
 * quando ele aparecer) ou por Inventário > Compartilhar (A envia o item). Enquanto a tela está
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
#include "known.h"
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
    uint32_t target_uid;        /* conhecido a desafiar (ou a quem dar o item) assim que aparecer (0 = nenhum) */
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
    known_save(p->uid, p->name, p->level);
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
    /* Conhecido escolhido no sub-menu: desafia ou oferece o item assim que ele aparecer */
    if (meet.target_uid && !meet.waiting && !meet.prompt_open) {
        for (int i = 0; i < meet.n_peers; i++) {
            if (meet.peers[i].profile.uid == meet.target_uid) {
                meet.target_uid = 0;
                start_waiting(&meet.peers[i], meet.item_id >= 0 ? LINK_OFFER : LINK_CHALLENGE);
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
/* Sub-menu Conectar (menu principal): Visível, Buscar Guará e Conhecidos     */
/* ------------------------------------------------------------------------- */

#define MENU_VISIBLE 0
#define MENU_SEARCH 1
#define MENU_KNOWN 2
#define MENU_LINES 3

static struct {
    known_t connections[KNOWN_MAX];
    int count;
    ui_list_t list;
    int known;                /* conhecido escolhido: posição na lista salva */
    int items[COUNT_ITEMS];   /* itens que podem ser dados (id), na ordem do catálogo */
    int n_items, item_id;
} menu;

static void open_known(void);
static void open_known_actions(void);

/* Relê os conhecidos (podem ter mudado numa busca). */
static void menu_reload(void)
{
    menu.count = known_list(menu.connections);
    menu.list.total = MENU_LINES;
}

/* Visível (ON/OFF), Buscar Guará e Conhecidos (com a quantidade). */
static void menu_item(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    if (i == MENU_VISIBLE) {
        it->icon = "on_off";
        snprintf(it->text, sizeof(it->text), "Visível");
        snprintf(it->info, sizeof(it->info), "%s", radio_link_visible() ? "ON" : "OFF");
    } else if (i == MENU_SEARCH) {
        it->icon = "search";
        snprintf(it->text, sizeof(it->text), "Buscar Guará");
    } else {
        it->icon = "save";
        snprintf(it->text, sizeof(it->text), "Conhecidos");
        snprintf(it->info, sizeof(it->info), "%d", menu.count);
    }
}

/* Conhecido selecionado na lista salva. */
static const known_t *known(void)
{
    return &menu.connections[menu.known];
}

/* Linha do modal de conhecidos: nome e se está por perto agora, do mais recente para o mais antigo. */
static void known_item(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    const known_t *c = &menu.connections[menu.count - 1 - i];
    snprintf(it->text, sizeof(it->text), "%s", c->name);
    snprintf(it->info, sizeof(it->info), "%s", radio_link_seen(c->uid) ? "online" : "offline");
}

/* Escolheu um conhecido: abre as ações dele. */
static void known_chosen(int r, void *ctx)
{
    (void)ctx;
    if (r < 0) {
        return;
    }
    menu.known = menu.count - 1 - r;
    open_known_actions();
}

/* Modal com os conhecidos (sem ícones nem rodapé); vazio, só avisa. */
static void open_known(void)
{
    menu_reload();
    if (menu.count == 0) {
        ui_alert("CONHECIDOS", "save", "Troque itens ou lute com alguém para conhecê-lo.", "OK", NULL, NULL);
        return;
    }
    ui_choice_list("CONHECIDOS", menu.count, known_item, NULL, known_chosen, NULL);
}

/* Confirmou excluir o conhecido: volta à lista (se ainda houver alguém). */
static void delete_answer(int r, void *ctx)
{
    (void)ctx;
    if (r != 1) {
        open_known_actions();
        return;
    }
    known_forget(menu.known);
    menu_reload();
    if (menu.count > 0) {
        open_known();
    }
}

/* Abre a busca e desafia o conhecido assim que ele aparecer. */
static void challenge_known(void)
{
    const char *icon, *reason = arena_cannot_fight(&icon);
    if (reason) {
        ui_alert("COMBATE", icon, reason, "OK", NULL, NULL);
        return;
    }
    screens_open_meet(-1, 0);
    meet.target_uid = known()->uid;
    snprintf(meet.target_name, sizeof(meet.target_name), "%s", known()->name);
}

static void open_share(void);

/* Quantidade escolhida: abre a busca e envia o item ao conhecido assim que ele aparecer; B volta aos itens. */
static void share_count(int count, void *ctx)
{
    (void)ctx;
    if (count <= 0) {
        open_share();
        return;
    }
    screens_open_meet(menu.item_id, count);
    meet.target_uid = known()->uid;
    snprintf(meet.target_name, sizeof(meet.target_name), "%s", known()->name);
}

/* Linha da lista de itens: nome e quantidade no inventário. */
static void share_item(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    item_name(menu.items[i], it->text, sizeof(it->text));
    snprintf(it->info, sizeof(it->info), "%d", g_guara.inventory[menu.items[i]]);
}

/* Escolheu o item: pergunta a quantidade; B volta às ações do conhecido. */
static void share_chosen(int r, void *ctx)
{
    (void)ctx;
    if (r < 0) {
        open_known_actions();
        return;
    }
    menu.item_id = menu.items[r];
    ui_quantity_picker("COMPARTILHAR", 1, g_guara.inventory[menu.item_id], 1, share_count, NULL);
}

/* Lista os itens do inventário que podem ser dados ao conhecido. */
static void open_share(void)
{
    menu.n_items = 0;
    for (int id = 0; id < COUNT_ITEMS; id++) {
        if (g_guara.inventory[id] > 0) {
            menu.items[menu.n_items++] = id;
        }
    }
    if (menu.n_items == 0) {
        ui_alert("COMPARTILHAR", "inventory", "Nenhum item para dar.", "OK", NULL, NULL);
        return;
    }
    ui_choice_list("COMPARTILHAR", menu.n_items, share_item, NULL, share_chosen, NULL);
}

/* Ação escolhida para o conhecido: desafiar, compartilhar ou excluir; B volta à lista. */
static void known_action(int r, void *ctx)
{
    (void)ctx;
    if (r == 0) {
        challenge_known();
    } else if (r == 1) {
        open_share();
    } else if (r == 2) {
        char msg[48];
        snprintf(msg, sizeof(msg), "Excluir %s?", known()->name);
        ui_confirm("CONHECIDOS", "save", msg, "NAO", "SIM", delete_answer, NULL);
    } else {
        open_known();
    }
}

/* Modal com as ações do conhecido escolhido. */
static void open_known_actions(void)
{
    static const char *const options[] = {"Desafiar", "Compartilhar", "Excluir"};
    ui_choice(known()->name, options, 3, known_action, NULL);
}

/* A alterna Visível (o app liga ou desliga o rádio em até 1 s), abre a busca ou os conhecidos; B volta. */
static void menu_key(const key_event_t *ev)
{
    if (ui_list_key(&menu.list, ev, UI_LINES_VISIBLE) || ev->type != KEY_PRESSED) {
        return;
    }
    if (ev->key == KEY_B) {
        ui_back();
    } else if (ev->key == KEY_A) {
        if (menu.list.sel == MENU_VISIBLE) {
            radio_link_set_visible(!radio_link_visible());
        } else if (menu.list.sel == MENU_SEARCH) {
            screens_open_meet(-1, 0);
        } else {
            open_known();
        }
    }
}

/* Título e lista. */
static void menu_draw(gfx_t *g, uint32_t now)
{
    ui_title(g, "CONECTAR", now);
    ui_list_draw(g, &menu.list, UI_LIST_Y, UI_LINES_VISIBLE, menu_item, NULL, now);
}

/* Abre com a seleção no topo e o rádio escutando (conhecidos online), sem anunciar. */
static void menu_open(void *param)
{
    (void)param;
    menu.list = (ui_list_t){0};
    menu_reload();
    radio_link_watch(true);
}

/* Fechou o sub-menu: para de escutar (o segundo plano continua, se Visível estiver em ON). */
static void menu_close(void)
{
    radio_link_watch(false);
}

const screen_t SCREEN_CONNECT = {
    .name = "conectar",
    .on_open = menu_open,
    .on_close = menu_close,
    .on_resume = menu_reload,
    .on_key = menu_key,
    .on_draw = menu_draw,
};
