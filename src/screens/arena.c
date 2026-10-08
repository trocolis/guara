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

static combat_t combat;
static fighter_t player, opponent;
static combat_result_t last;
static bool animating;
static uint32_t anim_start;
static bool at_modal;
static bool end_shown;
static uint32_t end_at;
static char notice[24];

static void finish(void);
static const screen_t SCREEN_COMBAT;

/* ---------------- Ações do jogador ---------------- */

/* Executa a ação escolhida pelo jogador e inicia a animação. */
static void run(combat_action_t action, const catalog_record_t *item)
{
    at_modal = false;
    combat_execute(&combat, 0, action, item, &last);
    if (action.type == ACTION_ITEM && item && !last.failed) {
        g_guara.inventory[item->id]--;
        app_mark_changed();
    }
    animating = true;
    anim_start = app_ms();
}

static int combat_items[COUNT_ITEMS], n_items;
/* Usa o item escolhido no combate. */
static void item_chosen(int r, void *ctx)
{
    (void)ctx;
    catalog_record_t item;
    if (r < 0 || r >= n_items || !catalog_read(CAT_ITEMS, combat_items[r], &item)) {
        at_modal = false;
        return;
    }
    run((combat_action_t){ACTION_ITEM, item.id}, &item);
}

static int skill_slots[COUNT_SKILL_ACTIVE], skill_count;
/* Usa a habilidade escolhida. */
static void skill_chosen(int r, void *ctx)
{
    (void)ctx;
    if (r < 0 || r >= skill_count) {
        at_modal = false;
        return;
    }
    run((combat_action_t){ACTION_SKILL, skill_slots[r]}, NULL);
}

/* Resposta da confirmação de desistência. */
static void forfeit_answer(int r, void *ctx)
{
    (void)ctx;
    if (r == 1) {
        run((combat_action_t){ACTION_FORFEIT, 0}, NULL);
    } else {
        at_modal = false;
    }
}

/* Trata a ação escolhida no modal (atacar, habilidade, itens, desistir). */
static void action_chosen(int r, void *ctx)
{
    (void)ctx;
    if (r == 0) {
        run((combat_action_t){ACTION_ATTACK, 0}, NULL);
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
        static char names[8][24];
        const char *options[8];
        n_items = 0;
        catalog_record_t it;
        for (int id = 0; id < catalog_record_count(CAT_ITEMS) && n_items < 8; id++) {
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
    animating = at_modal = end_shown = false;
    notice[0] = '\0';
    app_block_sleep(true);
}

/* Libera o deep sleep ao sair. */
static void on_close(void)
{
    app_block_sleep(false);
}

/* A abre o modal de ações quando a barra enche; B oferece desistir. */
static void on_key(const key_event_t *ev)
{
    if (ev->type != KEY_PRESSED || animating) {
        return;
    }
    if (combat.finished) {
        if (end_shown && (ev->key == KEY_A || ev->key == KEY_B)) {
            finish();
        }
        return;
    }
    if (ev->key == KEY_A && combat_ready(&combat, 0)) {
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

/* Avança o combate: animações, barras e ações do oponente. */
static void step(uint32_t now)
{
    static uint32_t previous;
    uint32_t dt = previous ? now - previous : 0;
    previous = now;
    if (dt > 100) {
        dt = 100;
    }
    if (animating) {
        if (now - anim_start >= ANIM_MS) {
            animating = false;
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
        combat_execute(&combat, 1, combat_ai(&combat, 1), NULL, &last);
        animating = true;
        anim_start = now;
        audio_tone(last.damage ? 220 : 660, 60);
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

/* Desenha o lutador com pulo de ataque, recuo e piscar ao ser atingido. */
static void draw_fighter(gfx_t *g, int side, uint32_t now)
{
    const fighter_t *l = &combat.side[side];
    int x = side == 0 ? X_PLAYER : X_OPPONENT;
    int dir = side == 0 ? 1 : -1; /* direção do oponente */
    uint32_t t = now - anim_start;
    bool visible = true;
    if (animating && last.actor == side && !last.failed && last.type == ACTION_ATTACK && t < 300) {
        x += dir * (t < 150 ? (int)t / 25 : (int)(300 - t) / 25); /* pulinho na direção do alvo */
    }
    if (animating && last.target == side && last.damage > 0 && t > 150 && t < 600) {
        x -= dir * 2; /* recuo */
        visible = (t / 60) % 2 == 0;
    }
    bool defeated = combat.finished && combat.winner != side && !animating;
    int frame = defeated ? 1 : sprite_frame(now);
    if (visible) {
        sprite_guara(g, l->color, l->head, l->stage, eye_fighter(side, now), x, BASE_Y, frame,
                     side == 1 ? GFX_MIRROR : 0);
    }
}

/* Desenha HP, lutadores, barra ATB, ícones e mensagens de dano. */
static void on_draw(gfx_t *g, uint32_t now)
{
    step(now);
    const fighter_t *me = &combat.side[0], *op = &combat.side[1];

    /* HP no topo de cada lado */
    ui_progress(g, 2, 2, 56, 6, me->hp, me->hp_max);
    ui_progress(g, 70, 2, 56, 6, op->hp, op->hp_max);
    gfx_text(g, 2, 10, me->name, GFX_WHITE);
    int w = gfx_text_width(op->name);
    gfx_text(g, 126 - w, 10, op->name, GFX_WHITE);

    draw_fighter(g, 0, now);
    draw_fighter(g, 1, now);

    /* Barra ATB do jogador: menor e mais fina, no canto inferior */
    ui_progress(g, 2, 59, 50, 3, me->atb_ms, me->atb_total);
    char mp[12];
    snprintf(mp, sizeof(mp), "MP %d", me->mp);
    gfx_text(g, 56, 56, mp, GFX_WHITE);
    if (!combat.finished && combat_ready(&combat, 0) && !animating && (now / 300) % 2) {
        ui_icon_mode(g, "button_a", 116, 53, GFX_COPY, 0);
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
            snprintf(msg, sizeof(msg), "%s-%d", last.critical ? "!" : "", last.damage);
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
