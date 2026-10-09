/*
 * Arena: menu (Treino e Combate) e a tela de combate de treino, com barras ATB,
 * barra de ações, animações e a ficha de vitória.
 */
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "audio.h"
#include "combat.h"
#include "widgets.h"
#include "modal.h"
#include "rules.h"
#include "sprite.h"
#include "submenu.h"
#include "screens.h"

/* Arena: Treino contra um Guará gerado e Combate (online, futuro). */

#define X_PLAYER 30   /* esquerda do usuário */
#define X_OPPONENT 98 /* direita do usuário: espelhado para os dois se olharem */
#define BASE_Y 50
#define ANIM_MS 900
#define INTRO_MS 1800 /* abertura "Fulano  VS  Siclano" */
#define WINDUP_RECOIL_MS 120 /* recuo rápido no início do preparo do golpe */
#define HIT_FRAMES 7
#define HIT_FRAME_MS 33   /* 30 FPS */
#define HIT_START_MS 150  /* começa no impacto, junto com o recuo */
#define HIT_SECOND_MS 450 /* segundo impacto do ataque duplo, no mesmo lugar do primeiro */
#define MENU_GRACE_MS 600 /* depois do impacto, A não mexe na barra de ações (toques atrasados da janela) */
#define ACTION_BAR_X 10 /* barra de ações: no lugar do ícone do A, ao lado da barra ATB */
#define ACTION_BAR_Y 54
#define ACTION_COUNT 3

static combat_t combat;
static fighter_t player, opponent;
static combat_result_t last;
static bool animating;
static uint32_t anim_start;
static bool at_modal;
static bool modal_hidden; /* modal escondido durante um golpe; volta depois da animação */
static int modal_kind;    /* qual modal está aberto (MODAL_*) */
static bool end_shown;
static uint32_t end_at;
static uint32_t intro_until;
static uint32_t menu_free_at; /* A só abre o menu de ações a partir daqui */
static char notice[24];

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

/* Executa a ação de um lado e inicia a animação do resultado. */
static void execute_now(int side, combat_action_t action, const catalog_record_t *item, uint32_t now)
{
    combat_execute(&combat, side, action, item, &last);
    if (side == 0 && action.type == ACTION_ITEM && item && !last.failed) {
        g_guara.inventory[item->id]--;
        app_mark_changed();
    }
    animating = true;
    anim_start = now;
    if (side == 1) {
        audio_tone(last.damage ? 220 : 660, 60);
    }
}

/* Começa a ação: golpes contra o outro lado passam pelo preparo com a janela de reação. */
static void begin_action(int side, combat_action_t action, uint32_t now)
{
    if (at_modal && !modal_hidden && ui_top() != &SCREEN_COMBAT) {
        /* Esconde o modal de escolha para mostrar o golpe; ele volta depois da animação */
        ui_back_until(&SCREEN_COMBAT);
        modal_hidden = true;
    }
    if (!combat_has_window(&combat, side, action)) {
        execute_now(side, action, action.type == ACTION_ITEM ? &pending_item : NULL, now);
        return;
    }
    winding = true;
    windup_start = now;
    windup_side = side;
    windup_action = action;
    player_reaction = 0;
}

/* Fim do preparo: junta a reação do jogador e a do oponente e resolve o golpe. */
static void impact(uint32_t now)
{
    winding = false;
    menu_free_at = now + MENU_GRACE_MS;
    combat_action_t action = windup_action;
    bool player_hit = player_reaction > 0;
    bool opponent_hit = rng_chance(&g_rng, AI_REACTION_PCT);
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

/* Abre (ou reabre, depois de um golpe) um modal do combate. */
static void open_modal(int kind)
{
    modal_kind = kind;
    modal_hidden = false;
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

/* Prepara o combate contra um oponente gerado. */
static void on_open(void *param)
{
    (void)param;
    combat_fighter_from_guara(&g_guara, &player);
    combat_generate_opponent(&g_guara, &g_rng, &opponent);
    combat_start(&combat, &player, &opponent, &g_rng);
    player = combat.side[0];
    memset(&last, 0, sizeof(last));
    animating = at_modal = modal_hidden = picking = end_shown = pending = winding = false;
    notice[0] = '\0';
    intro_until = menu_free_at = app_ms() + INTRO_MS;
    app_block_sleep(true);
}

/* Libera o deep sleep ao sair. */
static void on_close(void)
{
    app_block_sleep(false);
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

/* Fecha a tela de combate depois da ficha de vitória. */
static void card_closed(int r, void *ctx)
{
    (void)r;
    (void)ctx;
    ui_back();
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
        ui_back();
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

/* Avança o combate: abertura, preparo dos golpes, animações, barras e ações. */
static void step(uint32_t now)
{
    static uint32_t previous;
    uint32_t dt = previous ? now - previous : 0;
    previous = now;
    if (dt > 100) {
        dt = 100;
    }
    if ((int32_t)(intro_until - now) > 0) {
        return;
    }
    if (winding) {
        if (now - windup_start >= REACTION_WINDUP_MS) {
            impact(now);
        }
        return;
    }
    if (animating) {
        if (now - anim_start >= ANIM_MS) {
            animating = false;
            if (combat.finished) {
                if (at_modal && !modal_hidden) {
                    /* O combate acabou com um modal aberto: fecha os modais */
                    ui_back_until(&SCREEN_COMBAT);
                }
                at_modal = modal_hidden = picking = pending = false;
                end_at = now;
                if (combat.winner == 0) {
                    audio_victory();
                } else {
                    audio_defeat();
                }
            } else if (modal_hidden) {
                open_modal(modal_kind); /* o modal escondido pelo golpe volta */
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
    if (ready_mask & 2u) {
        begin_action(1, combat_ai(&combat, 1), now);
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
 * Barra de ações: os três ícones 8x8 lado a lado, separados por 2 px; o
 * selecionado fica dentro de um retângulo sem preenchimento.
 */
static void draw_action_bar(gfx_t *g)
{
    for (int i = 0; i < ACTION_COUNT; i++) {
        int x = ACTION_BAR_X + 1 + i * 10;
        if (i == pick_index) {
            gfx_rect(g, x - 1, ACTION_BAR_Y - 1, 10, 10, GFX_WHITE);
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
        if (last.icon[0] && last.type != ACTION_ATTACK && t < 800) {
            uint32_t period = 200 - t * 160 / 800;
            if ((t / period) % 2 == 0) {
                ui_icon_mode(g, last.icon, 56, 24, GFX_COPY, 0);
            }
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
        if (msg[0] && t > 150) {
            int xa = last.target == 0 ? X_PLAYER : X_OPPONENT;
            int dy = (int)(t - 150) / 60;
            gfx_text_center(g, xa - 20, 40, 22 - dy, msg, GFX_INVERT);
        }
    } else if (combat.finished) {
        gfx_text_center(g, 0, GFX_WIDTH, 22, combat.winner == 0 ? "VITORIA!" : "DERROTA", GFX_WHITE);
    } else if (notice[0]) {
        gfx_text_center(g, 0, GFX_WIDTH, 22, notice, GFX_WHITE);
    }
}

static const screen_t SCREEN_COMBAT = {
    .name = "combate",
    .fps = 30,
    .on_open = on_open,
    .on_close = on_close,
    .on_key = on_key,
    .on_draw = on_draw,
};

/* ---------------- Menu da Arena ---------------- */

/* Abre o treino se o Guará estiver em condições de lutar. */
static void training(void)
{
    if (guara_has_state(&g_guara, ST_FAINTED) || guara_has_state(&g_guara, ST_SLEEPING)) {
        ui_alert("TREINO", "sleeping", "O Guará não pode lutar agora.", "OK", NULL, NULL);
        return;
    }
    if (g_guara.hp == 0 || g_guara.hp * 5 < g_guara.hp_max) {
        ui_alert("TREINO", "hp", "HP muito baixo para treinar.", "OK", NULL, NULL);
        return;
    }
    ui_open(&SCREEN_COMBAT, NULL);
}

/* Combate entre Guarás (ainda não implementado). */
static void combat_online(void)
{
    /* README: "o combate com outro guará será implementado no futuro" */
    ui_alert("COMBATE", "fight", "Em breve: combate entre Guarás.", "OK", NULL, NULL);
}

static const submenu_item_t ITEMS_ARENA[] = {
    {"training", "Treino", NULL, training},
    {"fight", "Combate", NULL, combat_online},
};
static const submenu_t MENU_ARENA = {"ARENA", ITEMS_ARENA, 2};

/* Abre o sub-menu da Arena. */
void arena_open_menu(void)
{
    submenu_open(&MENU_ARENA);
}
