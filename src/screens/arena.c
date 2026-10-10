/*
 * Arena: menu (Treino e Combate) e a tela de combate, com barras ATB, barra de
 * ações, animações e a ficha de vitória. O combate roda em três modos: treino
 * (oponente gerado, com IA), anfitrião (contra outro Guará pelo Conectar: o
 * motor roda aqui e o lado 1 segue as escolhas do outro aparelho) e convidado
 * (sem motor: espelha o que o anfitrião envia, com o próprio Guará à esquerda).
 */
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "audio.h"
#include "combat.h"
#include "item_fx.h"
#include "link_proto.h"
#include "radio_link.h"
#include "widgets.h"
#include "modal.h"
#include "rules.h"
#include "sprite.h"
#include "submenu.h"
#include "screens.h"


#define X_PLAYER 30   /* esquerda do usuário */
#define X_OPPONENT 98 /* direita do usuário: espelhado para os dois se olharem */
#define BASE_Y 50
#define ANIM_MS 900
#define ITEM_ICON_DX 14 /* poção: ícone ao lado de quem bebe, voltado para o centro */
#define ITEM_ICON_Y (BASE_Y - 20)
#define ITEM_TEXT_Y 40 /* textos do efeito sobem a partir daqui, sobre quem bebeu */
#define INTRO_MS 1800 /* abertura "Fulano  VS  Siclano" */
#define WINDUP_RECOIL_MS 120 /* recuo rápido no início do preparo do golpe */
#define HIT_FRAMES 7
#define HIT_FRAME_MS 33   /* 30 FPS */
#define HIT_START_MS 150  /* começa no impacto, junto com o recuo */
#define HIT_SECOND_MS 450 /* segundo impacto do ataque duplo, no mesmo lugar do primeiro */
#define MENU_GRACE_MS 600 /* depois do impacto, A não mexe na barra de ações (toques atrasados da janela) */
#define ACTION_BAR_X 10 /* barra de ações: no lugar do ícone do A, ao lado da barra ATB */
#define ACTION_BAR_Y 53
#define ACTION_COUNT 3
#define VERSUS_STATE_MS 100   /* anfitrião: estado das barras para o convidado */
#define VERSUS_RESEND_MS 300  /* repete o que ainda não teve resposta (START, ação do convidado) */
#define VERSUS_STUCK_MS 3000  /* convidado: preparo sem resultado (mensagem perdida) é abandonado */
#define VERSUS_ALIVE_MS 300   /* presença direta (unicast, com reenvio do rádio) para o outro aparelho */
#define VERSUS_TIMEOUT_MS 6000 /* sem nenhuma notícia do outro por esse tempo: conexão perdida */

static combat_t combat;
static fighter_t player, opponent;
static combat_result_t last;
static bool animating;
static uint32_t anim_start;
static uint32_t anim_ms = ANIM_MS; /* duração da animação atual (a da poção é mais longa) */

/* Efeito da poção (como na tela principal): tomba, partículas, piscada e textos abreviados. */
static bool item_fx, item_absorbed;
static char item_texts[FX_MAX_TEXTS][FX_TEXT_SIZE];
static int item_n_texts;
static bool at_modal;
static bool end_shown;
static uint32_t end_at;
static uint32_t intro_until;
static uint32_t menu_free_at; /* A só abre o menu de ações a partir daqui */
static char notice[24];

/* Combate entre dois Guarás (Conectar) */
typedef enum { MODE_TRAINING, MODE_HOST, MODE_GUEST } combat_mode_t;
static combat_mode_t mode;
static uint8_t peer_mac[6];
static uint32_t peer_uid;
static char peer_name[SIZE_NAME + 1];
static bool versus_started;   /* os dois lutadores trocados (START) */
static bool connection_lost;
static uint16_t send_seq, last_seq; /* mensagens enviadas e a última recebida do anfitrião */
static uint32_t last_state_at, last_resend_at;
static uint32_t peer_heard_at, last_alive_at; /* última notícia do outro e última presença direta enviada */
/* Anfitrião: ação escolhida e reação do convidado */
static bool remote_pending;
static combat_action_t remote_action;
static uint16_t remote_input_seq;
static int remote_reaction; /* 0 sem apertar, 1 no tempo, -1 fora do tempo */
/* Convidado: a ação escolhida já chegou ao anfitrião */
static bool input_acked;
static uint16_t input_seq;

/* Ação escolhida pelo jogador, executada quando a barra ATB encher. */
static bool pending;
static combat_action_t pending_action;
static catalog_record_t pending_item;

/* Preparo do golpe com a janela de reação (A no fim do preparo). */
static bool winding;
static uint32_t windup_start;
static int windup_side;
static combat_action_t windup_action;
static int player_reaction; /* 0 sem apertar, 1 no tempo, -1 fora do tempo */

/* Barra de ações (Atacar, Habilidade, Poções) aberta pelo A no lugar do ícone. */
static bool picking;
static int pick_index;
static const char *const ACTION_ICONS[ACTION_COUNT] = {"action_atk", "action_skill", "action_potion"};

enum { MODAL_SKILLS = 1, MODAL_ITEMS, MODAL_FORFEIT };

static void finish(void);
static const screen_t SCREEN_COMBAT;

/* ---------------- Ações ---------------- */

/* Inicia a animação do resultado em `last`; `before` são os vitais de quem agiu antes da ação. */
static void start_animation(int side, const fx_values_t *before, uint32_t now)
{
    const fighter_t *f = &combat.side[side];
    item_fx = last.type == ACTION_ITEM && !last.failed;
    anim_ms = ANIM_MS;
    if (item_fx) {
        fx_values_t after = {f->hp, f->mp, 0, 0, 0, f->states};
        item_n_texts = fx_build_texts(before, &after, item_texts, FX_MAX_TEXTS);
        item_absorbed = false;
        uint32_t tail = fx_texts_ms(item_n_texts);
        anim_ms = FX_SIP_MS + FX_FLY_TOTAL_MS + (tail > FX_FLASH_MS ? tail : FX_FLASH_MS);
    }
    animating = true;
    anim_start = now;
    if (side == 1) {
        audio_tone(last.damage ? 220 : 660, 60);
    }
}

/* Envia ao outro aparelho. */
static void versus_send(link_msg_t *m)
{
    radio_link_send(peer_mac, m);
}

/* Anfitrião: vitais dos dois lados e o fim do combate. */
static void fill_vitals(link_msg_t *m)
{
    link_vitals_from(&combat.side[0], &m->vitals[0]);
    link_vitals_from(&combat.side[1], &m->vitals[1]);
    m->finished = combat.finished;
    m->winner = (int8_t)combat.winner;
}

/* Anfitrião: resultado de uma ação (enviado duas vezes; o convidado descarta a repetida). */
static void host_send_result(void)
{
    link_msg_t m = {.type = LINK_RESULT, .seq = ++send_seq, .result = last};
    fill_vitals(&m);
    versus_send(&m);
    versus_send(&m);
}

/* Executa a ação de um lado e inicia a animação do resultado. */
static void execute_now(int side, combat_action_t action, const catalog_record_t *item, uint32_t now)
{
    const fighter_t *f = &combat.side[side];
    fx_values_t before = {f->hp, f->mp, 0, 0, 0, f->states};
    combat_execute(&combat, side, action, item, &last);
    if (side == 0 && action.type == ACTION_ITEM && item && !last.failed) {
        g_guara.inventory[item->id]--;
        app_mark_changed();
    }
    start_animation(side, &before, now);
    if (mode == MODE_HOST) {
        host_send_result();
    }
}

/* Começa a ação: golpes contra o outro lado passam pelo preparo com a janela de reação. */
static void begin_action(int side, combat_action_t action, uint32_t now)
{
    if (!combat_has_window(&combat, side, action)) {
        /* O jogador escolheu o item na lista; o do oponente vem da ação da IA */
        static catalog_record_t opponent_item;
        const catalog_record_t *item = NULL;
        if (action.type == ACTION_ITEM) {
            item = side == 0 ? &pending_item : (combat_action_item(action, &opponent_item) ? &opponent_item : NULL);
        }
        execute_now(side, action, item, now);
        return;
    }
    winding = true;
    windup_start = now;
    windup_side = side;
    windup_action = action;
    player_reaction = 0;
    remote_reaction = 0;
    if (mode == MODE_HOST) {
        link_msg_t m = {.type = LINK_WINDUP, .seq = ++send_seq, .side = (uint8_t)side, .action = action};
        versus_send(&m);
        versus_send(&m);
    }
}

/* Fim do preparo: junta a reação do jogador e a do oponente e resolve o golpe. */
static void impact(uint32_t now)
{
    winding = false;
    menu_free_at = now + MENU_GRACE_MS;
    combat_action_t action = windup_action;
    bool player_hit = player_reaction > 0;
    /* No treino a IA acerta a janela por sorte; contra outro Guará vale o A dele */
    bool opponent_hit = mode == MODE_HOST ? remote_reaction > 0 : rng_chance(&g_rng, AI_REACTION_PCT);
    if (windup_side == 0) {
        action.reaction = (player_hit ? COMBAT_TIMED_STRIKE : 0) | (opponent_hit ? COMBAT_GUARD : 0);
    } else {
        action.reaction = (opponent_hit ? COMBAT_TIMED_STRIKE : 0) | (player_hit ? COMBAT_GUARD : 0);
    }
    execute_now(windup_side, action, NULL, now);
}

/* Verdadeiro durante a janela de reação (fim do preparo). */
static bool in_window(uint32_t now)
{
    return winding && now - windup_start >= REACTION_WINDUP_MS - REACTION_WINDOW_MS;
}

/* Guarda a ação escolhida; ela só executa com a barra cheia. */
static void choose(combat_action_t action, const catalog_record_t *item)
{
    at_modal = false;
    pending = true;
    pending_action = action;
    if (item) {
        pending_item = *item;
    }
    if (mode == MODE_GUEST) {
        /* Vai para o anfitrião e se repete até ele confirmar que recebeu */
        input_seq++;
        input_acked = false;
        link_msg_t m = {.type = LINK_INPUT, .seq = input_seq, .action = action};
        versus_send(&m);
        last_resend_at = app_ms();
    }
}

static int combat_items[COUNT_ITEMS], n_items;
/* Guarda o item escolhido para o combate. */
static void item_chosen(int r, void *ctx)
{
    (void)ctx;
    catalog_record_t item;
    if (r < 0 || r >= n_items || !catalog_read(CAT_ITEMS, combat_items[r], &item)) {
        at_modal = false;
        return;
    }
    choose((combat_action_t){ACTION_ITEM, item.id, 0}, &item);
}

static int skill_slots[COUNT_SKILL_ACTIVE], skill_count;
/* Guarda a habilidade escolhida. */
static void skill_chosen(int r, void *ctx)
{
    (void)ctx;
    if (r < 0 || r >= skill_count) {
        at_modal = false;
        return;
    }
    choose((combat_action_t){ACTION_SKILL, skill_slots[r], 0}, NULL);
}

/* Resposta da confirmação de desistência: desistir vale na hora. */
static void forfeit_answer(int r, void *ctx)
{
    (void)ctx;
    at_modal = false;
    if (r == 1) {
        if (mode == MODE_GUEST) {
            choose((combat_action_t){ACTION_FORFEIT, 0, 0}, NULL); /* o anfitrião encerra na hora */
            return;
        }
        pending = false;
        execute_now(0, (combat_action_t){ACTION_FORFEIT, 0, 0}, NULL, app_ms());
    }
}

/* Abre o modal de escolha das habilidades ativas; sem nenhuma, avisa. */
static bool open_skills(void)
{
    static char names[COUNT_SKILL_ACTIVE][24];
    static const char *options[COUNT_SKILL_ACTIVE];
    skill_count = 0;
    catalog_record_t h;
    for (int i = 0; i < COUNT_SKILL_ACTIVE; i++) {
        if (player.skills[i] != SLOT_EMPTY && catalog_read(CAT_SKILLS, player.skills[i], &h)) {
            snprintf(names[skill_count], sizeof(names[skill_count]), "%s (%d)", h.name, h.mp_cost);
            options[skill_count] = names[skill_count];
            skill_slots[skill_count++] = i;
        }
    }
    if (skill_count == 0) {
        snprintf(notice, sizeof(notice), "Sem habilidades");
        return false;
    }
    ui_choice("HABILIDADE", options, skill_count, skill_chosen, NULL);
    return true;
}

/* Abre o modal de escolha das poções do inventário; sem nenhuma, avisa. */
static bool open_items(void)
{
    static char names[16][24];
    static const char *options[16];
    n_items = 0;
    catalog_record_t it;
    for (int id = 0; id < catalog_record_count(CAT_ITEMS) && n_items < 16; id++) {
        if (g_guara.inventory[id] && catalog_read(CAT_ITEMS, id, &it) && it.type == ITEM_POTION) {
            snprintf(names[n_items], sizeof(names[n_items]), "%s x%d", it.name, g_guara.inventory[id]);
            options[n_items] = names[n_items];
            combat_items[n_items++] = id;
        }
    }
    if (n_items == 0) {
        snprintf(notice, sizeof(notice), "Sem pocoes");
        return false;
    }
    ui_choice("POCOES", options, n_items, item_chosen, NULL);
    return true;
}

/* Abre um modal do combate. */
static void open_modal(int kind)
{
    if (kind == MODAL_SKILLS) {
        at_modal = open_skills();
    } else if (kind == MODAL_ITEMS) {
        at_modal = open_items();
    } else {
        at_modal = true;
        ui_confirm("DESISTIR", "fight", "Desistir do combate?", "NAO", "SIM", forfeit_answer, NULL);
    }
}

/* Confirma o ícone da barra de ações: atacar vale direto; habilidade e poção abrem a escolha. */
static void pick_confirm(int index)
{
    picking = false;
    notice[0] = '\0';
    if (index == 0) {
        choose((combat_action_t){ACTION_ATTACK, 0, 0}, NULL);
    } else {
        open_modal(index == 1 ? MODAL_SKILLS : MODAL_ITEMS);
    }
}

/* ---------------- Tela de combate ---------------- */

/* Começa a abertura e libera o combate (treino na hora; contra outro Guará, após o START). */
static void start_intro(void)
{
    player = combat.side[0];
    versus_started = true;
    intro_until = menu_free_at = app_ms() + INTRO_MS;
}

/* Convidado: envia o próprio lutador até o anfitrião responder com os dois. */
static void guest_send_start(void)
{
    link_msg_t m = {.type = LINK_START, .seq = 0};
    m.fighters[0] = player;
    versus_send(&m);
    last_resend_at = app_ms();
}

/* Anfitrião: os dois lutadores do combate (lado 0 = anfitrião). */
static void host_send_start(void)
{
    link_msg_t m = {.type = LINK_START, .seq = ++send_seq};
    m.fighters[0] = combat.side[0];
    m.fighters[1] = combat.side[1];
    versus_send(&m);
}

/* Prepara o combate: oponente gerado no treino; contra outro Guará, espera os lutadores. */
static void on_open(void *param)
{
    (void)param;
    memset(&last, 0, sizeof(last));
    animating = at_modal = picking = end_shown = pending = winding = item_fx = false;
    versus_started = connection_lost = remote_pending = input_acked = false;
    peer_heard_at = last_alive_at = app_ms();
    send_seq = last_seq = remote_input_seq = input_seq = 0;
    notice[0] = '\0';
    combat_fighter_from_guara(&g_guara, &player);
    app_block_sleep(true);
    if (mode == MODE_TRAINING) {
        combat_generate_opponent(&g_guara, &g_rng, &opponent);
        combat_start(&combat, &player, &opponent, &g_rng);
        start_intro();
    } else if (mode == MODE_GUEST) {
        guest_send_start();
    }
}

/* Avisa o outro aparelho ao sair; o Conectar continua aberto embaixo e segura o sono. */
static void on_close(void)
{
    if (mode != MODE_TRAINING) {
        link_msg_t m = {.type = LINK_BYE};
        versus_send(&m);
    }
    app_block_sleep(mode != MODE_TRAINING);
}

/* A abre a barra de ações uma vez por ciclo (ou reage na janela do golpe); B oferece desistir. */
static void on_key(const key_event_t *ev)
{
    if (ev->type != KEY_PRESSED) {
        return;
    }
    uint32_t now = app_ms();
    if ((int32_t)(intro_until - now) > 0) {
        if (ev->key == KEY_A || ev->key == KEY_B) {
            intro_until = now; /* pula a abertura */
        }
        return;
    }
    if (combat.finished) {
        if (!animating && end_shown && (ev->key == KEY_A || ev->key == KEY_B)) {
            finish();
        }
        return;
    }
    /* A barra de ações continua ativa durante o preparo e a animação dos golpes;
       no preparo, o A é a reação (uma tentativa por golpe) */
    if (winding && ev->key == KEY_A) {
        if (player_reaction == 0) {
            player_reaction = in_window(now) ? 1 : -1;
            if (mode == MODE_GUEST) {
                /* A reação é decidida aqui (pelo preparo visto neste aparelho) e vale no anfitrião */
                link_msg_t m = {.type = LINK_REACT, .hit = player_reaction > 0};
                versus_send(&m);
                versus_send(&m);
            }
        }
        return;
    }
    bool a_free = (int32_t)(now - menu_free_at) >= 0;
    if (picking) {
        /* ←/→ movem a seleção, A confirma e B fecha a barra */
        if (ev->key == KEY_LEFT) {
            pick_index = (pick_index + ACTION_COUNT - 1) % ACTION_COUNT;
        } else if (ev->key == KEY_RIGHT) {
            pick_index = (pick_index + 1) % ACTION_COUNT;
        } else if (ev->key == KEY_A && a_free) {
            pick_confirm(pick_index);
        } else if (ev->key == KEY_B) {
            picking = false;
        }
        return;
    }
    if (ev->key == KEY_A && !pending && a_free) { /* escolhida a ação, só no próximo ciclo */
        picking = true;
        pick_index = 0;
        notice[0] = '\0';
    } else if (ev->key == KEY_B) {
        open_modal(MODAL_FORFEIT);
    }
}

/*
 * Sai do combate: no treino, volta à Arena; contra outro Guará, fecha também
 * a busca do Conectar e volta à tela em que se estava antes do desafio.
 */
static void leave_combat(void)
{
    if (mode == MODE_TRAINING) {
        ui_back();
    } else {
        screens_close_meet();
    }
}

/* Fecha a tela de combate depois da ficha de vitória. */
static void card_closed(int r, void *ctx)
{
    (void)r;
    (void)ctx;
    leave_combat();
}

/* Aplica o resultado no Guará e mostra a ficha do vencedor. */
static void finish(void)
{
    const fighter_t *me = &combat.side[0];
    g_guara.hp = (uint16_t)(me->hp > 0 ? me->hp : 1);
    g_guara.mp = (uint16_t)me->mp;
    uint32_t persistent = STATE_BIT(ST_POISONED) | STATE_BIT(ST_BURNED) | STATE_BIT(ST_FROZEN) |
                            STATE_BIT(ST_PARALYZED) | STATE_BIT(ST_CONFUSED) | STATE_BIT(ST_CURSED);
    g_guara.states = (g_guara.states & ~persistent) | (me->states & persistent);
    g_guara.energy = (uint16_t)(g_guara.energy > CENTI(5) ? g_guara.energy - CENTI(5) : 0);

    bool won = combat.winner == 0;
    guara_add_gauge(&g_guara, GAUGE_VICTORIES, won ? GAUGE_STEP_VICTORY : -GAUGE_STEP_VICTORY);
    if (!won) {
        guara_add_gauge(&g_guara, GAUGE_ANGER, -GAUGE_STEP_SADNESS);
        app_mark_changed();
        leave_combat();
        return;
    }
    guara_set_state(&g_guara, ST_CURSED, false); /* cura com a vitória */
    uint32_t xp = combat_xp(&combat, 0);
    int drop = combat_drop(&combat, 0);
    char summary[96];
    int n = snprintf(summary, sizeof(summary), "%s +%lu XP", g_guara.name, (unsigned long)xp);
    if (drop >= 0 && drop < COUNT_ITEMS) {
        catalog_record_t it;
        catalog_read(CAT_ITEMS, drop, &it);
        if (g_guara.inventory[drop] < 255) {
            g_guara.inventory[drop]++;
        }
        snprintf(summary + n, sizeof(summary) - (size_t)n, " Ganhou: %s", it.name);
    }
    int levels = app_gain_xp(xp);
    if (levels > 0) {
        n = (int)strlen(summary);
        snprintf(summary + n, sizeof(summary) - (size_t)n, " NV %d!", g_guara.level);
    }
    ui_alert("VITORIA", drop >= 0 ? NULL : "level", summary, "OK", card_closed, NULL);
}

/* Fim do combate: fecha os modais, toca a melodia e espera para mostrar o resultado. */
static void combat_ended(uint32_t now)
{
    if (at_modal) {
        /* O combate acabou com um modal aberto: fecha os modais */
        ui_back_until(&SCREEN_COMBAT);
    }
    at_modal = picking = pending = false;
    end_at = now;
    if (combat.winner == 0) {
        audio_victory();
    } else {
        audio_defeat();
    }
}

/* Conexão perdida: avisa e volta ao Conectar, sem consequências para o Guará. */
static void lost_closed(int r, void *ctx)
{
    (void)r;
    (void)ctx;
    leave_combat();
}
static void versus_lost(void)
{
    if (connection_lost || (combat.finished && !animating)) {
        return;
    }
    connection_lost = true;
    if (at_modal) {
        ui_back_until(&SCREEN_COMBAT);
        at_modal = false;
    }
    audio_error();
    char msg[48];
    snprintf(msg, sizeof(msg), "Conexão com %s perdida.", peer_name);
    ui_alert("COMBATE", "connect", msg, "OK", lost_closed, NULL);
}

/* Lado visto pelo convidado: o anfitrião manda o lado 0 como ele mesmo. */
static int mirror(int side)
{
    return side < 0 ? side : 1 - side;
}

/* Convidado: aplica os vitais enviados pelo anfitrião (espelhados). */
static void guest_apply_vitals(const link_msg_t *m, uint32_t now)
{
    bool was_finished = combat.finished;
    link_vitals_apply(&m->vitals[1], &combat.side[0]);
    link_vitals_apply(&m->vitals[0], &combat.side[1]);
    combat.finished = m->finished;
    combat.winner = mirror(m->winner);
    if (combat.finished && !was_finished && !animating) {
        combat_ended(now); /* o fim chegou sem o resultado (mensagem perdida) */
    }
}

/* Convidado: uma ação resolvida pelo anfitrião vira a animação daqui. */
static void guest_apply_result(const link_msg_t *m, uint32_t now)
{
    winding = false;
    menu_free_at = now + MENU_GRACE_MS;
    last = m->result;
    last.actor = mirror(last.actor);
    last.target = mirror(last.target);
    const fighter_t *f = &combat.side[last.actor];
    fx_values_t before = {f->hp, f->mp, 0, 0, 0, f->states};
    bool was_finished = combat.finished;
    link_vitals_apply(&m->vitals[1], &combat.side[0]);
    link_vitals_apply(&m->vitals[0], &combat.side[1]);
    combat.finished = m->finished;
    combat.winner = mirror(m->winner);
    if (last.actor == 0) {
        /* A ação deste Guará foi executada: a poção sai do inventário daqui */
        if (last.type == ACTION_ITEM && !last.failed && pending_action.type == ACTION_ITEM &&
            g_guara.inventory[pending_item.id] > 0) {
            g_guara.inventory[pending_item.id]--;
            app_mark_changed();
        }
        pending = false;
        input_acked = true;
    }
    start_animation(last.actor, &before, now);
    (void)was_finished;
}

/* Verdadeiro se o outro Guará deu presença há pouco (a lista do rádio esquece em 3,5 s). */
static bool peer_present(void)
{
    link_peer_t peers[LINK_MAX_PEERS];
    int n = radio_link_peers(peers, LINK_MAX_PEERS);
    for (int i = 0; i < n; i++) {
        if (peers[i].profile.uid == peer_uid) {
            return true;
        }
    }
    return false;
}

/* Mensagens do outro aparelho, repetições pendentes, estado periódico e queda da conexão. */
static void versus_poll(uint32_t now)
{
    uint8_t mac[6];
    link_msg_t m;
    while (radio_link_receive(mac, &m)) {
        if (m.from.uid != peer_uid) {
            continue; /* outros Guarás por perto: ofertas e desafios ficam sem resposta */
        }
        peer_heard_at = now; /* qualquer mensagem do outro é sinal de vida */
        if (m.type == LINK_BYE) {
            versus_lost();
        } else if (m.type == LINK_CHALLENGE) {
            link_msg_t a = {.type = LINK_ACCEPT, .tx = m.tx}; /* o aceite se perdeu: responde de novo */
            versus_send(&a);
        } else if (mode == MODE_HOST) {
            if (m.type == LINK_START) {
                if (!versus_started) {
                    combat_start(&combat, &player, &m.fighters[0], &g_rng);
                    start_intro();
                }
                host_send_start(); /* também responde a um START repetido (o nosso se perdeu) */
            } else if (m.type == LINK_INPUT && m.seq != remote_input_seq && versus_started) {
                remote_input_seq = m.seq;
                if (m.action.type == ACTION_FORFEIT) {
                    if (!combat.finished) {
                        execute_now(1, m.action, NULL, now);
                    }
                } else {
                    remote_pending = true;
                    remote_action = (combat_action_t){m.action.type, m.action.position, 0};
                }
            } else if (m.type == LINK_REACT && winding && remote_reaction == 0) {
                remote_reaction = m.hit ? 1 : -1;
            }
        } else {
            if (m.type == LINK_START && m.seq != 0) {
                if (!versus_started) {
                    combat.side[0] = m.fighters[1];
                    combat.side[1] = m.fighters[0];
                    combat.finished = false;
                    combat.winner = -1;
                    combat.rng = &g_rng;
                    start_intro();
                }
                continue;
            }
            if (!versus_started || (int16_t)(m.seq - last_seq) <= 0) {
                continue; /* repetida ou atrasada */
            }
            last_seq = m.seq;
            if (m.type == LINK_STATE) {
                guest_apply_vitals(&m, now);
                if (m.guest_pending) {
                    input_acked = true;
                }
            } else if (m.type == LINK_WINDUP) {
                winding = true;
                windup_start = now;
                windup_side = mirror(m.side);
                windup_action = m.action;
                player_reaction = 0;
            } else if (m.type == LINK_RESULT) {
                guest_apply_result(&m, now);
            }
        }
    }
    if (mode == MODE_HOST && versus_started && now - last_state_at >= VERSUS_STATE_MS) {
        last_state_at = now;
        link_msg_t st = {.type = LINK_STATE, .seq = ++send_seq, .guest_pending = remote_pending};
        fill_vitals(&st);
        versus_send(&st);
    }
    if (mode == MODE_GUEST && now - last_resend_at >= VERSUS_RESEND_MS) {
        if (!versus_started) {
            guest_send_start();
        } else if (pending && !input_acked) {
            link_msg_t in = {.type = LINK_INPUT, .seq = input_seq, .action = pending_action};
            versus_send(&in);
            last_resend_at = now;
        }
    }
    /*
     * Queda: nenhuma mensagem nem presença do outro por VERSUS_TIMEOUT_MS. A
     * presença em broadcast não tem reenvio e se perde fácil; por isso, no
     * combate, cada lado manda também uma presença direta (unicast, com
     * confirmação e reenvio do rádio) a cada VERSUS_ALIVE_MS.
     */
    if (now - last_alive_at >= VERSUS_ALIVE_MS) {
        last_alive_at = now;
        link_msg_t alive = {.type = LINK_HELLO};
        versus_send(&alive);
    }
    if (peer_present()) {
        peer_heard_at = now;
    }
    if (now - peer_heard_at >= VERSUS_TIMEOUT_MS) {
        versus_lost();
    }
}

/* Avança o combate: abertura, preparo dos golpes, animações, barras e ações. */
static void step(uint32_t now)
{
    static uint32_t previous;
    uint32_t dt = previous ? now - previous : 0;
    previous = now;
    if (dt > 100) {
        dt = 100;
    }
    if (mode != MODE_TRAINING) {
        versus_poll(now);
        if (connection_lost || !versus_started) {
            return;
        }
    }
    if ((int32_t)(intro_until - now) > 0) {
        return;
    }
    if (winding) {
        /* O convidado espera o resultado do anfitrião (desiste se a mensagem se perdeu) */
        if (mode == MODE_GUEST) {
            if (now - windup_start >= VERSUS_STUCK_MS) {
                winding = false;
            }
        } else if (now - windup_start >= REACTION_WINDUP_MS) {
            impact(now);
        }
        return;
    }
    if (animating) {
        if (now - anim_start >= anim_ms) {
            animating = false;
            if (combat.finished) {
                combat_ended(now);
            }
        }
        return;
    }
    if (combat.finished) {
        if (!end_shown && now - end_at > 2500) {
            end_shown = true;
        }
        return;
    }
    /* Tempo real: as barras continuam enchendo e o oponente age com o menu aberto */
    unsigned ready_mask = combat_advance(&combat, (int)dt);
    if (mode == MODE_GUEST) {
        return; /* as barras só andam para a tela; quem age é decidido no anfitrião */
    }
    if ((ready_mask & 2u) && (mode == MODE_TRAINING || remote_pending)) {
        /* Treino: a IA escolhe; contra outro Guará, vale a ação que ele já escolheu */
        combat_action_t a = mode == MODE_TRAINING ? combat_ai(&combat, 1) : remote_action;
        remote_pending = false;
        begin_action(1, a, now);
    } else if ((ready_mask & 1u) && pending) {
        pending = false;
        begin_action(0, pending_action, now);
    }
}

/* Olho de cada lutador conforme a animação e o resultado. */
static const char *eye_fighter(int side, uint32_t now)
{
    if (combat.finished && !animating) {
        return combat.winner == side ? EYE_HAPPY : EYE_FAINTED;
    }
    uint32_t t = now - anim_start;
    if (animating && t > 150 && t < 700 && last.target == side && (last.damage > 0 || last.status_damage > 0) &&
        !last.dodged) {
        return EYE_DAZED;
    }
    if (animating && last.heal > 0 && last.target == side) {
        return EYE_HAPPY;
    }
    return EYE_ANGER;
}

/* Altura desenhada de cada lutador (o sprite é aproximadamente tão largo quanto alto). */
static int fighter_height[2] = {16, 16};

/* Desenha o lutador: recua no preparo, pula no golpe e pisca ao ser atingido. */
static void draw_fighter(gfx_t *g, int side, uint32_t now)
{
    const fighter_t *l = &combat.side[side];
    int x = side == 0 ? X_PLAYER : X_OPPONENT;
    int dir = side == 0 ? 1 : -1; /* direção do oponente */
    uint32_t t = now - anim_start;
    bool visible = true;
    if (winding && windup_side == side) {
        uint32_t tw = now - windup_start;
        /* Toma impulso (recua 3 px em 120 ms e segura) e, na janela de reação, avança
           de volta até o impacto: o jogador prevê o tempo pelo movimento, sem indicação */
        int windup = REACTION_WINDUP_MS - REACTION_WINDOW_MS;
        int back = (int)tw < WINDUP_RECOIL_MS ? (int)tw * 3 / WINDUP_RECOIL_MS : 3;
        x -= dir * ((int)tw < windup ? back : 3 - ((int)tw - windup) * 3 / REACTION_WINDOW_MS);
    }
    if (animating && last.actor == side && !last.failed && last.type == ACTION_ATTACK &&
        t < 300) {
        x += dir * (t < 150 ? (int)t / 25 : (int)(300 - t) / 25); /* pulinho na direção do alvo */
    }
    if (animating && last.target == side && last.damage > 0 && t > 150 && t < 600) {
        x -= dir * (last.guarded ? 1 : 2); /* recuo */
        visible = (t / 60) % 2 == 0;
    }
    bool defeated = combat.finished && combat.winner != side && !animating;
    int frame = defeated ? 1 : sprite_frame(now);
    if (visible) {
        fighter_height[side] = sprite_guara(g, l->color, l->head, l->stage, eye_fighter(side, now), x, BASE_Y,
                                            frame, side == 1 ? GFX_MIRROR : 0);
    }
    /* Poção absorvida: quem bebeu pisca invertido (só a silhueta) */
    uint32_t flash_at = FX_SIP_MS + FX_FLY_TOTAL_MS;
    if (animating && item_fx && last.actor == side && t >= flash_at && fx_flash_on(t - flash_at)) {
        static gfx_t sprite;
        gfx_init(&sprite);
        sprite_guara(&sprite, l->color, l->head, l->stage, eye_fighter(side, now), x, BASE_Y, frame,
                     side == 1 ? GFX_MIRROR : 0);
        fx_invert_silhouette(g, &sprite);
    }
}

/*
 * Poção: o frasco aparece ao lado de quem bebe, sobe e tomba 45° na direção
 * dele, e as partículas voam da boca do frasco até ele.
 */
static void draw_item_fx(gfx_t *g, uint32_t now)
{
    if (!animating || !item_fx) {
        return;
    }
    uint32_t t = now - anim_start;
    int side = last.actor;
    int fx = side == 0 ? X_PLAYER : X_OPPONENT;
    int ix = side == 0 ? fx + ITEM_ICON_DX : fx - ITEM_ICON_DX - 16;
    float degrees = side == 0 ? FX_POUR_DEGREES : -FX_POUR_DEGREES; /* o topo vai na direção de quem bebe */
    if (t < FX_SIP_MS) {
        int y = ITEM_ICON_Y - (int)(t * FX_RISE_PX / FX_SIP_MS);
        fx_icon_rotated(g, last.icon, ix, y, degrees * (float)t / FX_SIP_MS);
        return;
    }
    if (!item_absorbed) {
        item_absorbed = true;
        audio_magic();
    }
    int mx, my;
    fx_icon_mouth(ix, ITEM_ICON_Y - FX_RISE_PX, degrees, &mx, &my);
    fx_particles(g, mx, my, fx, BASE_Y - fighter_height[side] / 2, t - FX_SIP_MS);
}

/*
 * Animação do golpe (bitmaps/animations/hit) no lado do alvo voltado para o
 * atacante, em XOR: o preto é transparente e, onde cruza o sprite, inverte.
 */
static void draw_hit(gfx_t *g, uint32_t now)
{
    if (!animating || last.damage <= 0) {
        return;
    }
    uint32_t t = now - anim_start;
    int frame = -1;
    if (t >= HIT_START_MS && t < HIT_START_MS + HIT_FRAMES * HIT_FRAME_MS) {
        frame = (int)(t - HIT_START_MS) / HIT_FRAME_MS;
    } else if (last.hits == 2 && t >= HIT_SECOND_MS && t < HIT_SECOND_MS + HIT_FRAMES * HIT_FRAME_MS) {
        frame = (int)(t - HIT_SECOND_MS) / HIT_FRAME_MS;
    }
    if (frame < 0) {
        return;
    }
    /* Centro do efeito na borda do sprite voltada para o atacante, à meia altura.
       Jogador à esquerda do usuário: o golpe vem da direita da tela (e vice-versa). */
    int half = fighter_height[last.target] / 2;
    int cx = last.target == 0 ? X_PLAYER + half : X_OPPONENT - half;
    int cy = BASE_Y - half;
    char name[8];
    snprintf(name, sizeof(name), "hit_%d", frame + 1);
    ui_icon_mode(g, name, cx - 8, cy - 8, GFX_XOR, 0);
}

/*
 * Barra dupla de um lutador (56 x 8 px): HP com 2 px de altura em cima e MP
 * numa linha de 1 px embaixo, dentro da mesma moldura, com o nome logo abaixo.
 * No oponente, espelhado, o preenchimento e o nome partem da borda direita.
 */
static void draw_vitals(gfx_t *g, int x, const fighter_t *l, bool mirrored)
{
    const int width = 56, inner = width - 4;
    gfx_rect(g, x, 2, width, 8, GFX_WHITE);
    int hp = l->hp_max > 0 ? inner * (l->hp < 0 ? 0 : l->hp) / l->hp_max : 0;
    int mp = l->mp_max > 0 ? inner * (l->mp < 0 ? 0 : l->mp) / l->mp_max : 0;
    gfx_rect_filled(g, mirrored ? x + 2 + inner - hp : x + 2, 4, hp, 2, GFX_WHITE);
    gfx_line_h(g, mirrored ? x + 2 + inner - mp : x + 2, 7, mp, GFX_WHITE);
    gfx_text(g, mirrored ? x + width - gfx_text_width(l->name) : x, 11, l->name, GFX_WHITE);
}

/*
 * Barra ATB vertical (6 x 16 px) no canto inferior do lado do lutador: enche
 * de baixo para cima.
 */
static void draw_atb(gfx_t *g, int x, const fighter_t *l)
{
    const int y = 46, height = 16, inner = height - 4;
    gfx_rect(g, x, y, 6, height, GFX_WHITE);
    int rows = l->atb_total > 0 ? inner * l->atb_ms / l->atb_total : 0;
    gfx_rect_filled(g, x + 2, y + 2 + inner - rows, 2, rows, GFX_WHITE);
}

/*
 * Barra de ações: os três ícones 8x8 lado a lado, separados por 4 px; o
 * selecionado fica dentro de um retângulo sem preenchimento de cantos
 * arredondados (raio 1), a 1 px do ícone.
 */
static void draw_action_bar(gfx_t *g)
{
    for (int i = 0; i < ACTION_COUNT; i++) {
        int x = ACTION_BAR_X + 1 + i * 12;
        if (i == pick_index) {
            gfx_rect_rounded(g, x - 2, ACTION_BAR_Y - 2, 12, 12, 1, GFX_WHITE);
        }
        ui_icon_mode(g, ACTION_ICONS[i], x, ACTION_BAR_Y, GFX_COPY, 0);
    }
}

/* Abertura: "Fulano  VS  Siclano" numa linha, ou em três se não couber. */
static void draw_intro(gfx_t *g)
{
    char line[48];
    snprintf(line, sizeof(line), "%s  VS  %s", combat.side[0].name, combat.side[1].name);
    if (gfx_text_width(line) <= GFX_WIDTH - 4) {
        gfx_text_center(g, 0, GFX_WIDTH, 28, line, GFX_WHITE);
    } else {
        gfx_text_center(g, 0, GFX_WIDTH, 18, combat.side[0].name, GFX_WHITE);
        gfx_text_center(g, 0, GFX_WIDTH, 28, "VS", GFX_WHITE);
        gfx_text_center(g, 0, GFX_WIDTH, 38, combat.side[1].name, GFX_WHITE);
    }
}

/* Desenha HP/MP, lutadores, barra ATB, ícones e mensagens de dano. */
static void on_draw(gfx_t *g, uint32_t now)
{
    step(now);
    if (mode != MODE_TRAINING && !versus_started) {
        char line[40];
        snprintf(line, sizeof(line), "com %s", peer_name);
        ui_text_dots(g, 0, GFX_WIDTH, 22, "Conectando", now, GFX_WHITE);
        gfx_text_center(g, 0, GFX_WIDTH, 32, line, GFX_WHITE);
        return;
    }
    const fighter_t *me = &combat.side[0], *op = &combat.side[1];

    if ((int32_t)(intro_until - now) > 0) {
        draw_intro(g);
        return;
    }

    /* HP e MP numa moldura só por lutador, no topo de cada lado */
    draw_vitals(g, 2, me, false);
    draw_vitals(g, 70, op, true);

    draw_fighter(g, 0, now);
    draw_fighter(g, 1, now);
    draw_hit(g, now);
    draw_item_fx(g, now);

    if (!combat.finished) {
        draw_atb(g, 2, me);
        draw_atb(g, GFX_WIDTH - 8, op);
        /* Ícone do A piscando ao lado da barra do jogador enquanto ele pode escolher
           a ação; some quando a ação é escolhida (só volta no próximo ciclo). Com a
           barra de ações aberta, os três ícones ficam no lugar dele. */
        if (picking) {
            draw_action_bar(g);
        } else if (!pending && !at_modal && (now / 300) % 2 == 0) {
            ui_icon_mode(g, "button_a", 10, 52, GFX_COPY, 0);
        }
    }

    uint32_t t = now - anim_start;
    if (animating) {
        /* Ícone da habilidade/item no centro, piscando cada vez mais rápido */
        if (last.icon[0] && last.type != ACTION_ATTACK && !item_fx && t < 800) {
            uint32_t period = 200 - t * 160 / 800;
            if ((t / period) % 2 == 0) {
                ui_icon_mode(g, last.icon, 56, 24, GFX_COPY, 0);
            }
        }
    } else if (combat.finished) {
        gfx_text_center(g, 0, GFX_WIDTH, 22, combat.winner == 0 ? "VITORIA!" : "DERROTA", GFX_WHITE);
    } else if (notice[0]) {
        gfx_text_center(g, 0, GFX_WIDTH, 22, notice, GFX_WHITE);
    }
}

/*
 * Valor do dano (ou cura, ou erro) subindo sobre o alvo, desenhado por cima
 * dos modais, sobre um fundo preto de cantos arredondados.
 */
static void draw_over(gfx_t *g, uint32_t now)
{
    uint32_t t = now - anim_start;
    if (!animating || t <= 150 || (int32_t)(intro_until - now) > 0) {
        return;
    }
    if (item_fx) {
        /* Textos do efeito da poção, junto com as piscadas, sobre quem bebeu */
        uint32_t texts_at = FX_SIP_MS + FX_FLY_TOTAL_MS;
        if (t >= texts_at) {
            fx_texts(g, item_texts, item_n_texts, t - texts_at, last.actor == 0 ? X_PLAYER : X_OPPONENT,
                     ITEM_TEXT_Y);
        }
        return;
    }
    char msg[16] = "";
    if (last.failed) {
        snprintf(msg, sizeof(msg), "Falhou!");
    } else if (last.dodged) {
        snprintf(msg, sizeof(msg), "Esquivou!");
    } else if (last.missed && !last.self_hit) {
        snprintf(msg, sizeof(msg), "Errou!");
    } else if (last.damage > 0) {
        snprintf(msg, sizeof(msg), "%s-%d%s", last.hits == 2 ? "2x" : "", last.damage, last.critical ? "!" : "");
    } else if (last.heal > 0) {
        snprintf(msg, sizeof(msg), "+%d", last.heal);
    }
    if (msg[0]) {
        int xa = last.target == 0 ? X_PLAYER : X_OPPONENT;
        int dy = (int)(t - 150) / 60;
        /* Fundo preto de cantos arredondados (raio 2) para o número ficar legível sobre o modal */
        int w = gfx_text_width(msg);
        gfx_rect_rounded_filled(g, xa - w / 2 - 2, 21 - dy, w + 4, 10, 2, GFX_BLACK);
        gfx_text(g, xa - w / 2, 22 - dy, msg, GFX_WHITE);
    }
}

static const screen_t SCREEN_COMBAT = {
    .name = "combate",
    .fps = 30,
    .on_open = on_open,
    .on_close = on_close,
    .on_key = on_key,
    .on_draw = on_draw,
    .on_draw_over = draw_over,
};

/* ---------------- Menu da Arena ---------------- */

/* Motivo para o Guará não poder lutar agora (desmaiado, dormindo, HP < 20%), ou NULL; `icon` recebe o ícone do aviso. */
const char *arena_cannot_fight(const char **icon)
{
    if (guara_has_state(&g_guara, ST_FAINTED) || guara_has_state(&g_guara, ST_SLEEPING)) {
        *icon = guara_has_state(&g_guara, ST_FAINTED) ? "fainted" : "sleeping";
        return "O Guará não pode lutar agora.";
    }
    if (g_guara.hp == 0 || g_guara.hp * 5 < g_guara.hp_max) {
        *icon = "hp";
        return "HP muito baixo para lutar.";
    }
    return NULL;
}

/* Abre o treino se o Guará estiver em condições de lutar. */
static void training(void)
{
    const char *icon, *reason = arena_cannot_fight(&icon);
    if (reason) {
        ui_alert("TREINO", icon, reason, "OK", NULL, NULL);
        return;
    }
    mode = MODE_TRAINING;
    ui_open(&SCREEN_COMBAT, NULL);
}

/* Combate contra outro Guará: abre a tela Conectar, onde se desafia quem estiver por perto. */
static void combat_versus(void)
{
    screens_open_meet(-1, 0);
}

/* Abre o combate contra o Guará `uid` em `mac` (quem desafiou é o anfitrião). */
void arena_open_versus(const uint8_t mac[6], uint32_t uid, const char *name, bool host)
{
    mode = host ? MODE_HOST : MODE_GUEST;
    memcpy(peer_mac, mac, 6);
    peer_uid = uid;
    snprintf(peer_name, sizeof(peer_name), "%s", name);
    ui_open(&SCREEN_COMBAT, NULL);
}

static const submenu_item_t ITEMS_ARENA[] = {
    {"training", "Treino", NULL, training},
    {"fight", "Combate", NULL, combat_versus},
};
static const submenu_t MENU_ARENA = {"ARENA", ITEMS_ARENA, 2};

/* Abre o sub-menu da Arena. */
void arena_open_menu(void)
{
    submenu_open(&MENU_ARENA);
}
