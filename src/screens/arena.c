/*
 * Arena: menu (Treino e Combate) e a tela de combate de treino, com barras ATB,
 * modal de ações, animações e a ficha de vitória.
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
#define HIT_FRAMES 7
#define HIT_FRAME_MS 33   /* 30 FPS */
#define HIT_START_MS 150  /* começa no impacto, junto com o recuo */
#define HIT_SECOND_MS 450 /* segundo impacto do ataque duplo (segundo pulo começa em 300 ms) */
#define HIT_SECOND_INSET 8 /* o segundo impacto fica 8 px mais para dentro do sprite */
#define MENU_GRACE_MS 600 /* depois de um golpe, A não abre o menu (toques atrasados da janela) */

static combat_t combat;
static fighter_t player, opponent;
static combat_result_t last;
static bool animating;
static uint32_t anim_start;
static bool at_modal;
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

/* Trata a ação escolhida no modal (atacar, habilidade, itens, desistir). */
static void action_chosen(int r, void *ctx)
{
    (void)ctx;
    if (r == 0) {
        choose((combat_action_t){ACTION_ATTACK, 0, 0}, NULL);
    } else if (r == 1) {
        static char names[COUNT_SKILL_ACTIVE][24];
        const char *options[COUNT_SKILL_ACTIVE];
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
            at_modal = false;
            snprintf(notice, sizeof(notice), "Sem habilidades");
            return;
        }
        ui_choice("HABILIDADE", options, skill_count, skill_chosen, NULL);
    } else if (r == 2) {
        static char names[16][24];
        const char *options[16];
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
            at_modal = false;
            snprintf(notice, sizeof(notice), "Sem itens de cura");
            return;
        }
        ui_choice("ITENS", options, n_items, item_chosen, NULL);
    } else if (r == 3) {
        ui_confirm("DESISTIR", "fight", "Desistir do combate?", "NAO", "SIM", forfeit_answer, NULL);
    } else {
        at_modal = false;
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
    animating = at_modal = end_shown = pending = winding = false;
    notice[0] = '\0';
    intro_until = menu_free_at = app_ms() + INTRO_MS;
    app_block_sleep(true);
}

/* Libera o deep sleep ao sair. */
static void on_close(void)
{
    app_block_sleep(false);
}

/* A escolhe a ação uma vez por ciclo (ou reage na janela do golpe); B oferece desistir. */
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
    if (winding) {
        if (ev->key == KEY_A && player_reaction == 0) {
            player_reaction = in_window(now) ? 1 : -1; /* uma tentativa por golpe */
        }
        return;
    }
    if (animating) {
        return;
    }
    if (combat.finished) {
        if (end_shown && (ev->key == KEY_A || ev->key == KEY_B)) {
            finish();
        }
        return;
    }
    if (ev->key == KEY_A && !pending && (int32_t)(now - menu_free_at) >= 0) { /* escolhida a ação, só no próximo ciclo */
        static const char *const options[] = {"Atacar", "Habilidade", "Itens", "Desistir"};
        at_modal = true;
        notice[0] = '\0';
        ui_choice("ACAO", options, 4, action_chosen, NULL);
    } else if (ev->key == KEY_B) {
        at_modal = true;
        ui_confirm("DESISTIR", "fight", "Desistir do combate?", "NAO", "SIM", forfeit_answer, NULL);
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
            menu_free_at = now + MENU_GRACE_MS;
            if (combat.finished) {
                end_at = now;
                if (combat.winner == 0) {
                    audio_victory();
                } else {
                    audio_defeat();
                }
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
    if (at_modal || ui_top() != &SCREEN_COMBAT) {
        return;
    }
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
        /* Toma impulso (recua 3 px) e, na janela de reação, avança de volta até o
           impacto: o jogador prevê o tempo pelo movimento, sem indicação na tela */
        int windup = REACTION_WINDUP_MS - REACTION_WINDOW_MS;
        x -= dir * (tw < (uint32_t)windup ? (int)tw * 3 / windup
                                          : 3 - ((int)tw - windup) * 3 / REACTION_WINDOW_MS);
    }
    if (animating && last.actor == side && !last.failed && last.type == ACTION_ATTACK &&
        t < (last.hits == 2 ? 600u : 300u)) {
        uint32_t tj = t % 300; /* no ataque duplo, o pulinho se repete */
        x += dir * (tj < 150 ? (int)tj / 25 : (int)(300 - tj) / 25); /* pulinho na direção do alvo */
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
    int frame = -1, inset = 0;
    if (t >= HIT_START_MS && t < HIT_START_MS + HIT_FRAMES * HIT_FRAME_MS) {
        frame = (int)(t - HIT_START_MS) / HIT_FRAME_MS;
    } else if (last.hits == 2 && t >= HIT_SECOND_MS && t < HIT_SECOND_MS + HIT_FRAMES * HIT_FRAME_MS) {
        frame = (int)(t - HIT_SECOND_MS) / HIT_FRAME_MS;
        inset = HIT_SECOND_INSET;
    }
    if (frame < 0) {
        return;
    }
    /* Centro do efeito na borda do sprite voltada para o atacante, à meia altura.
       Jogador à esquerda do usuário: o golpe vem da direita da tela (e vice-versa). */
    int half = fighter_height[last.target] / 2;
    int cx = last.target == 0 ? X_PLAYER + half - inset : X_OPPONENT - half + inset;
    int cy = BASE_Y - half;
    char name[8];
    snprintf(name, sizeof(name), "hit_%d", frame + 1);
    ui_icon_mode(g, name, cx - 8, cy - 8, GFX_XOR, 0);
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

    /* HP no topo de cada lado e MP logo abaixo, com metade do comprimento */
    ui_progress(g, 2, 2, 56, 6, me->hp, me->hp_max);
    ui_progress(g, 70, 2, 56, 6, op->hp, op->hp_max);
    ui_progress(g, 2, 9, 28, 5, me->mp, me->mp_max);
    ui_progress(g, 98, 9, 28, 5, op->mp, op->mp_max);

    draw_fighter(g, 0, now);
    draw_fighter(g, 1, now);
    draw_hit(g, now);

    /* Ícone do botão A (10x10): pisca até a ação ser escolhida; depois, a barra ATB de 32 px */
    if (!combat.finished && (pending || (now / 300) % 2)) {
        ui_icon_mode(g, "button_a", 2, 53, GFX_COPY, 0);
    }
    ui_progress(g, 14, 57, 32, 3, me->atb_ms, me->atb_total);

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
