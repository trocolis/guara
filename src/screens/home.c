/*
 * Tela principal: barra superior, Guará animado em 2x, balão de estado, animação
 * de uso de itens e o alarme dos despertadores/temporizador.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "alarms.h"
#include "app.h"
#include "audio.h"
#include "battery.h"
#include "widgets.h"
#include "config.h"
#include "modal.h"
#include "persistence.h"
#include "rtc_clock.h"
#include "sprite.h"
#include "storage.h"
#include "screens.h"
#include "network.h"

/* Layout (mockup bitmaps/exemple/main_screen.bmp) */
#define GUARA_X 64
#define GUARA_BASE 61 /* 2 px acima do limite inferior */
#define BALLOON_X 18  /* lado direito do aparelho = esquerda do usuário */
#define BALLOON_Y 14
#define ITEM_X 100             /* área de itens: à esquerda do Guará (direita do usuário) */
#define ITEM_Y 34

static struct {
    uint32_t blink_at, blink_until;
    uint32_t happy_at, happy_until;
    uint32_t emotion_at, emotion_until;
    uint32_t wink_until;
    uint32_t bite_happy_until; /* olhar feliz logo após cada mordida */
} anim;

/* Intervalo aleatório em milissegundos. */
static uint32_t random_ms(uint32_t min, uint32_t max)
{
    return (uint32_t)rng_range(&g_rng, (int)min, (int)max);
}

/* Agenda piscadas, olhar feliz e exibição de emoções. */
static void schedule(uint32_t now)
{
    if (anim.blink_at == 0) {
        anim.blink_at = now + random_ms(3000, 6000);
        anim.happy_at = now + random_ms(20 * 60000, 30 * 60000);
        anim.emotion_at = now + random_ms(20 * 60000, 30 * 60000);
    }
    if (now >= anim.blink_at) {
        anim.blink_until = now + 150;
        anim.blink_at = now + random_ms(3000, 6000);
    }
    /* Olhar feliz: 5 a 10 s a cada 20 a 30 min, se os vitais estiverem acima de 80% */
    if (now >= anim.happy_at) {
        if (guara_has_state(&g_guara, ST_HAPPY)) {
            anim.happy_until = now + random_ms(5000, 10000);
        }
        anim.happy_at = now + random_ms(20 * 60000, 30 * 60000);
    }
    /* Emoções no balão: 5 a 30 s a cada 20 a 30 min */
    if (now >= anim.emotion_at) {
        anim.emotion_until = now + random_ms(5000, 30000);
        anim.emotion_at = now + random_ms(20 * 60000, 30 * 60000);
    }
}

/* Mostra a piscadela até o instante indicado (após comer ou usar item). */
void home_set_wink(uint32_t until)
{
    anim.wink_until = until;
}

/* Olho atual conforme estado, emoção e animações. */
static const char *current_eye(uint32_t now)
{
    const guara_t *g = &g_guara;
    if (guara_has_state(g, ST_FAINTED)) return EYE_FAINTED;
    if (guara_has_state(g, ST_SLEEPING)) return EYE_CLOSED;
    if (now < anim.bite_happy_until) return EYE_HAPPY;
    if (now < anim.wink_until) return EYE_WINK;
    if (guara_has_state(g, ST_POISONED) || guara_has_state(g, ST_BURNED) || guara_has_state(g, ST_FROZEN)) return EYE_SCARED;
    if (guara_has_state(g, ST_HUNGRY) || guara_has_state(g, ST_TIRED) || guara_has_state(g, ST_CONFUSED) ||
        guara_has_state(g, ST_PARALYZED))
        return EYE_DAZED;
    if (guara_has_state(g, ST_ANGRY)) return EYE_ANGER;
    if (guara_has_state(g, ST_SAD) || guara_has_state(g, ST_BORED)) return EYE_BORED;
    if (now < anim.happy_until) return EYE_HAPPY;
    if (now < anim.blink_until) return EYE_CLOSED;
    return EYE_NORMAL;
}

/* Ícone do balão: estado físico ou emoção no momento de exibir. */
static const char *balloon_icon(uint32_t now)
{
    int physical = guara_main_state(&g_guara);
    if (physical >= 0) {
        return state_icon((state_t)physical);
    }
    if (now < anim.happy_until) {
        return state_icon(ST_HAPPY);
    }
    int emotion = guara_main_emotion(&g_guara);
    if (emotion >= 0 && emotion != ST_HAPPY && now < anim.emotion_until) {
        return state_icon((state_t)emotion);
    }
    return NULL;
}

/* Relógio à esquerda do usuário e indicadores à direita (bateria, Wi-Fi, etc.). */
static void draw_top_bar(gfx_t *g, uint32_t now)
{
    char hour[8];
    clock_format_hhmm(hour, sizeof(hour));
    gfx_text(g, 1, 1, hour, GFX_WHITE);

    int x = GFX_WIDTH - 9;
    char name[24];
    if (!battery_present()) {
        snprintf(name, sizeof(name), "status_bateria_ausente_8x8");
    } else {
        int level = battery_percent() / 25;
        if (level > 3) {
            level = 3;
        }
        if (battery_charging()) {
            level = (int)((now / 500) % 4); /* carregando: animação */
        }
        snprintf(name, sizeof(name), "battery_%d", level);
    }
    ui_icon(g, name, x, 1);
    x -= 10;
    network_state_t network = network_state();
    if (network != NETWORK_DISABLED) {
        if (network == NETWORK_CONNECTED) {
            snprintf(name, sizeof(name), "wifi_%d", network_bars() ? network_bars() : 1);
        } else if (network == NETWORK_CONNECTING) {
            snprintf(name, sizeof(name), "wifi_%d", (int)(now / 300 % 3) + 1); /* barras subindo */
        } else {
            snprintf(name, sizeof(name), "wifi_3");
        }
        ui_icon(g, name, x, 1);
        if (network == NETWORK_IDLE) {
            /* permitida e ociosa: ícone "apagado" (xadrez) */
            for (int yy = 1; yy < 9; yy++) {
                for (int xx = x; xx < x + 8; xx++) {
                    if ((xx + yy) % 2) {
                        gfx_pixel(g, xx, yy, GFX_BLACK);
                    }
                }
            }
        }
        x -= 10;
    }
    if (g_cfg.bt_on) {
        ui_icon(g, "status_bluetooth", x, 1);
        x -= 10;
    }
    if (alarms_count() > 0) {
        ui_icon(g, "status_alarm", x, 1);
        x -= 10;
    }
    if (timer_active()) {
        ui_icon(g, "status_timer", x, 1);
        x -= 10;
    }
    if (g_cfg.mute) {
        ui_icon(g, "status_mute", x, 1);
    }
}

/* Quadro do Guará na tela principal: desmaiado ou dormindo, parado no quadro 1. */
static int guara_frame(uint32_t now)
{
    bool still = guara_has_state(&g_guara, ST_FAINTED) || guara_has_state(&g_guara, ST_SLEEPING);
    return still ? 1 : sprite_frame(now);
}

/* Cena da tela principal, reutilizada pela animação de uso de item. */
void draw_home_scene(gfx_t *g, uint32_t now, bool with_balloon)
{
    schedule(now);
    draw_top_bar(g, now);
    if (!g_has_guara) {
        return;
    }
    int frame = guara_frame(now);
    sprite_guara(g, g_guara.color, g_guara.head, g_guara.stage, current_eye(now), GUARA_X, GUARA_BASE, frame,
                 GFX_SCALE2X);

    const char *icon = with_balloon ? balloon_icon(now) : NULL;
    if (icon) {
        /* "respiração": desce 8 px e volta, suavemente (ciclo de 3 s) */
        static const uint8_t wave[] = {0, 0, 1, 2, 3, 4, 5, 6, 7, 7, 8, 8, 8, 7, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0};
        int dy = wave[(now / 125) % sizeof(wave)];
        ui_icon_mode(g, "speech_balloon", BALLOON_X, BALLOON_Y + dy, GFX_COPY, 0);
        ui_icon(g, icon, BALLOON_X + 2, BALLOON_Y + dy + 2);
    }
}

/* A abre o menu principal. */
static void on_key(const key_event_t *ev)
{
    if (ev->type != KEY_PRESSED) {
        return;
    }
    if (ev->key == KEY_A) {
        app_interact();
        ui_open(&SCREEN_MENU, NULL);
    }
}

/* Desenha a cena com o balão. */
static void on_draw(gfx_t *g, uint32_t now)
{
    draw_home_scene(g, now, true);
}

/* Ao voltar do menu, grava se houver alterações (Pilar 2). */
static void on_resume(void)
{
    /* Voltou do menu: grava se algo mudou (Pilar 2) */
    persist_save_if_dirty(&g_guara);
}

const screen_t SCREEN_HOME = {
    .name = "principal",
    .on_key = on_key,
    .on_draw = on_draw,
    .on_resume = on_resume,
};

/* ------------------------------------------------------------------------- */
/* Uso de item: a tela principal aparece com a animação e depois volta       */
/* ------------------------------------------------------------------------- */

#define USE_MAX_TEXTS 8

static struct {
    catalog_record_t item;
    uint32_t start;
    int last_frame;
    uint32_t texts_at; /* início dos textos de efeito (0 = ainda não) */
    char texts[USE_MAX_TEXTS][20];
    int n_texts;
} usage;

#define FRAME_BITE_MS 450 /* duração de cada quadro da mordida */
#define FRAME_GULP_MS 450 /* duração de cada gole */
#define GULPS 4
/* Poção: sobe, vira partículas que voam até o Guará e ele pisca invertido */
#define POTION_RISE_MS 500
#define POTION_FLY_MS 450
#define POTION_PARTICLES 8
#define POTION_PARTICLE_DELAY_MS 25
#define POTION_FLASH_AT (POTION_RISE_MS + POTION_FLY_MS + POTION_PARTICLES * POTION_PARTICLE_DELAY_MS)
#define POTION_FLASH_MS 600
/* Textos de efeito: cada um sobe por TEXT_MS, um a cada TEXT_STEP_MS */
#define TEXT_MS 800
#define TEXT_STEP_MS 300
#define TEXT_Y 44 /* linha de partida; sobe até Y=18 */
#define GUARA_CENTER_Y 36 /* centro aproximado do Guará 2x, alvo das partículas */

/* Prepara a animação do item usado. */
static void item_use_on_open(void *param)
{
    int id = (int)(intptr_t)param;
    catalog_read(CAT_ITEMS, id, &usage.item);
    usage.start = app_ms();
    usage.last_frame = -1;
    usage.texts_at = 0;
}

/* A animação não pode ser interrompida. */
static void item_use_on_key(const key_event_t *ev)
{
    (void)ev; /* a animação não pode ser interrompida */
}

/* Encerra a animação: piscadela e volta para onde estava. */
static void item_use_finish(uint32_t now)
{
    home_set_wink(now + 1500);
    ui_back();
}

/* Comida: o ícone é coberto pelas mordidas, uma a cada FRAME_BITE_MS. Retorna se terminou. */
static bool draw_food(gfx_t *g, uint32_t now, uint32_t t)
{
    int frame = (int)(t / FRAME_BITE_MS);
    if (frame >= 6) {
        return true;
    }
    ui_icon_mode(g, usage.item.icon, ITEM_X, ITEM_Y, GFX_COPY, 0);
    if (frame >= 1) {
        char name[12];
        snprintf(name, sizeof(name), "bite_%d", frame);
        ui_icon_mode(g, name, ITEM_X, ITEM_Y, GFX_MASK, 0);
        if (frame != usage.last_frame) {
            /* cada mordida: som e olhar feliz por meio quadro, depois volta ao normal */
            audio_bite();
            anim.bite_happy_until = now + FRAME_BITE_MS / 2;
        }
    }
    usage.last_frame = frame;
    return false;
}

/* Bebida: o nível baixa de cima para baixo, um gole a cada FRAME_GULP_MS. Retorna se terminou. */
static bool draw_drink(gfx_t *g, uint32_t now, uint32_t t)
{
    int frame = (int)(t / FRAME_GULP_MS);
    if (frame > GULPS) {
        return true;
    }
    ui_icon_mode(g, usage.item.icon, ITEM_X, ITEM_Y, GFX_COPY, 0);
    if (frame >= 1) {
        gfx_rect_filled(g, ITEM_X, ITEM_Y, 16, 16 * frame / GULPS, GFX_BLACK);
        if (frame != usage.last_frame) {
            audio_gulp();
            anim.bite_happy_until = now + FRAME_GULP_MS / 2;
        }
    }
    usage.last_frame = frame;
    return false;
}

/*
 * Inverte só o Guará (a silhueta, sem o fundo): compõe o sprite num quadro à
 * parte, marca o fundo externo por inundação a partir das bordas e inverte na
 * tela os pixels que não são fundo externo. A máscara é refeita só quando o
 * quadro muda.
 */
static void invert_guara(gfx_t *g, int frame)
{
    static gfx_t sprite, outside;
    static int mask_frame;
    if (usage.last_frame == 1) {
        mask_frame = 0; /* primeira piscada desta poção: máscara nova */
        usage.last_frame = 2;
    }
    if (frame == mask_frame) {
        goto apply;
    }
    mask_frame = frame;
    gfx_init(&sprite);
    gfx_init(&outside);
    sprite_guara(&sprite, g_guara.color, g_guara.head, g_guara.stage, EYE_HAPPY, GUARA_X, GUARA_BASE, frame,
                 GFX_SCALE2X);
    /* Sementes: pixels apagados da borda da tela */
    for (int x = 0; x < GFX_WIDTH; x++) {
        for (int y = 0; y < GFX_HEIGHT; y += GFX_HEIGHT - 1) {
            if (!gfx_read_pixel(&sprite, x, y)) {
                gfx_pixel(&outside, x, y, GFX_WHITE);
            }
        }
    }
    for (int y = 0; y < GFX_HEIGHT; y++) {
        for (int x = 0; x < GFX_WIDTH; x += GFX_WIDTH - 1) {
            if (!gfx_read_pixel(&sprite, x, y)) {
                gfx_pixel(&outside, x, y, GFX_WHITE);
            }
        }
    }
    /* Varreduras de ida e volta até estabilizar (a silhueta é simples, bastam poucas) */
    for (bool changed = true; changed;) {
        changed = false;
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i < GFX_WIDTH * GFX_HEIGHT; i++) {
                int k = pass == 0 ? i : GFX_WIDTH * GFX_HEIGHT - 1 - i;
                int x = k % GFX_WIDTH, y = k / GFX_WIDTH;
                if (gfx_read_pixel(&outside, x, y) || gfx_read_pixel(&sprite, x, y)) {
                    continue;
                }
                if ((x > 0 && gfx_read_pixel(&outside, x - 1, y)) ||
                    (x < GFX_WIDTH - 1 && gfx_read_pixel(&outside, x + 1, y)) ||
                    (y > 0 && gfx_read_pixel(&outside, x, y - 1)) ||
                    (y < GFX_HEIGHT - 1 && gfx_read_pixel(&outside, x, y + 1))) {
                    gfx_pixel(&outside, x, y, GFX_WHITE);
                    changed = true;
                }
            }
        }
    }
apply:
    for (int y = 0; y < GFX_HEIGHT; y++) {
        for (int x = 0; x < GFX_WIDTH; x++) {
            if (!gfx_read_pixel(&outside, x, y)) {
                gfx_pixel(g, x, y, GFX_INVERT);
            }
        }
    }
}

/* Poção: sobe, se desfaz em partículas que voam até o Guará, e ele pisca invertido. Retorna se terminou. */
static bool draw_potion(gfx_t *g, uint32_t now, uint32_t t)
{
    if (t < POTION_RISE_MS) {
        ui_icon_mode(g, usage.item.icon, ITEM_X, ITEM_Y - (int)t * 8 / POTION_RISE_MS, GFX_COPY, 0);
        return false;
    }
    if (usage.last_frame < 0) {
        usage.last_frame = 0;
        audio_magic();
    }
    /* Curva quadrática do ícone até o Guará, passando por cima; cada partícula com seu desvio */
    const int x0 = ITEM_X + 8, y0 = ITEM_Y, x2 = GUARA_X, y2 = GUARA_CENTER_Y;
    for (int i = 0; i < POTION_PARTICLES; i++) {
        int32_t pt = (int32_t)(t - POTION_RISE_MS) - i * POTION_PARTICLE_DELAY_MS;
        if (pt < 0 || pt >= POTION_FLY_MS) {
            continue;
        }
        int dx = (i % 4 - 2) * 4, dy = (i / 4) * 6 - 3;
        int x1 = (x0 + x2) / 2 + dx, y1 = 4 + dy;
        int u = pt * 64 / POTION_FLY_MS, v = 64 - u; /* parâmetro em 1/64 */
        int x = (v * v * x0 + 2 * u * v * x1 + u * u * (x2 + dx)) / 4096;
        int y = (v * v * y0 + 2 * u * v * y1 + u * u * (y2 + dy)) / 4096;
        gfx_pixel(g, x, y, GFX_INVERT);
        gfx_pixel(g, x - 1, y, GFX_INVERT);
        gfx_pixel(g, x + 1, y, GFX_INVERT);
        gfx_pixel(g, x, y - 1, GFX_INVERT);
        gfx_pixel(g, x, y + 1, GFX_INVERT);
    }
    if (t >= POTION_FLASH_AT + POTION_FLASH_MS) {
        return true;
    }
    if (t >= POTION_FLASH_AT) {
        if (usage.last_frame == 0) {
            usage.last_frame = 1;
            anim.bite_happy_until = now + POTION_FLASH_MS;
        }
        /* 3 piscadas invertidas de 100 ms */
        if (((t - POTION_FLASH_AT) / 100) % 2 == 0) {
            invert_guara(g, guara_frame(now));
        }
    }
    return false;
}

/*
 * Textos do efeito ("HP +150", "Energia +12%", "Curado!") subindo um após o
 * outro, como o dano no combate. Retorna se todos terminaram.
 */
static bool draw_effect_texts(gfx_t *g, uint32_t now)
{
    if (usage.texts_at == 0) {
        usage.texts_at = now;
        anim.bite_happy_until = now + (uint32_t)usage.n_texts * TEXT_STEP_MS + TEXT_MS;
    }
    uint32_t t = now - usage.texts_at;
    bool running = false;
    for (int i = 0; i < usage.n_texts; i++) {
        int32_t st = (int32_t)t - i * TEXT_STEP_MS;
        if (st < 0 || st >= TEXT_MS) {
            running |= st < 0;
            continue;
        }
        running = true;
        /* 1 px a cada 30 ms: textos seguidos ficam 10 px (uma linha) um do outro */
        int y = TEXT_Y - st / 30;
        int w = gfx_text_width(usage.texts[i]);
        gfx_rect_rounded_filled(g, GUARA_X - w / 2 - 2, y - 1, w + 4, 10, 2, GFX_BLACK);
        gfx_text(g, GUARA_X - w / 2, y, usage.texts[i], GFX_WHITE);
    }
    return !running;
}

/* Mordidas (comida), goles (bebida) ou partículas (poção); depois, os textos do efeito. */
static void item_use_on_draw(gfx_t *g, uint32_t now)
{
    uint32_t t = now - usage.start;
    draw_home_scene(g, now, false);
    if (usage.texts_at == 0) {
        bool done = usage.item.type == ITEM_FOOD    ? draw_food(g, now, t)
                    : usage.item.type == ITEM_DRINK ? draw_drink(g, now, t)
                                                    : draw_potion(g, now, t);
        if (!done) {
            return;
        }
        if (usage.item.type == ITEM_FOOD || usage.n_texts == 0) {
            item_use_finish(now);
            return;
        }
    }
    if (draw_effect_texts(g, now)) {
        item_use_finish(now);
    }
}

/* Adiciona um texto de efeito, se houver espaço. */
static void add_effect_text(const char *name, int delta, const char *unit)
{
    if (delta != 0 && usage.n_texts < USE_MAX_TEXTS) {
        snprintf(usage.texts[usage.n_texts++], sizeof(usage.texts[0]), "%s %+d%s", name, delta, unit);
    }
}

/* Monta os textos com o que o item mudou de fato (antes x depois, já com os limites). */
static void build_effect_texts(const guara_t *before, const guara_t *after)
{
    usage.n_texts = 0;
    add_effect_text("HP", after->hp - before->hp, "");
    add_effect_text("MP", after->mp - before->mp, "");
    add_effect_text("Energia", (after->energy - before->energy) / 100, "%");
    add_effect_text("Humor", (after->mood - before->mood) / 100, "%");
    add_effect_text("Alimentação", (after->satiety - before->satiety) / 100, "%");
    add_effect_text("Saúde", (after->health - before->health) / 100, "%");
    /* Desmaio e estados negativos curados */
    uint32_t cured = before->states & ~after->states & (STATE_BIT(ST_FAINTED) | STATE_BIT(ST_POISONED) |
                     STATE_BIT(ST_PARALYZED) | STATE_BIT(ST_BURNED) | STATE_BIT(ST_FROZEN) |
                     STATE_BIT(ST_CONFUSED) | STATE_BIT(ST_CURSED));
    if (cured && usage.n_texts < USE_MAX_TEXTS) {
        snprintf(usage.texts[usage.n_texts++], sizeof(usage.texts[0]), "Curado!");
    }
}

static const screen_t SCREEN_USE_ITEM = {
    .name = "uso_item",
    .on_open = item_use_on_open,
    .on_key = item_use_on_key,
    .on_draw = item_use_on_draw,
    .fps = 30, /* partículas e textos suaves */
};

/* Usa o item e mostra a animação na tela principal (ou o motivo do bloqueio). */
void screens_use_item(int item_id)
{
    catalog_record_t r;
    if (!catalog_read(CAT_ITEMS, item_id, &r)) {
        return;
    }
    static guara_t before; /* estático: o guara_t é grande para a pilha */
    before = g_guara;
    item_use_result_t res = guara_use_item(&g_guara, &r, &g_rng);
    if (res != USE_OK) {
        static const char *const reasons[] = {"", "Desmaiado! Espere ou use uma cura.", "Amaldiçoado! Não pode usar itens.",
                                              "Sem estoque.", "Item inválido."};
        audio_error();
        ui_alert(r.name, r.icon, reasons[res], "OK", NULL, NULL);
        return;
    }
    build_effect_texts(&before, &g_guara);
    app_interact();
    app_mark_changed();
    ui_open(&SCREEN_USE_ITEM, (void *)(intptr_t)item_id);
}

/* ------------------------------------------------------------------------- */
/* Alarme: bipes até confirmar ou por 1 minuto                               */
/* ------------------------------------------------------------------------- */

static struct {
    bool ringing;
    uint32_t start, last_beep;
    char title[24];
} alarm;

/* Confirmação do alarme: para os bipes. */
static void alarm_answer(int r, void *ctx)
{
    (void)r;
    (void)ctx;
    alarm.ringing = false;
    audio_stop_effects();
}

/* Repete os bipes do alarme por até 1 minuto. Retorna se está tocando. */
bool screens_alarm_ringing(void)
{
    if (alarm.ringing) {
        uint32_t now = app_ms();
        if (now - alarm.start > 60000) {
            alarm.ringing = false; /* para sozinho após 1 minuto */
        } else if (now - alarm.last_beep > 1200) {
            alarm.last_beep = now;
            audio_alarm();
        }
    }
    return alarm.ringing;
}

/* Dispara o alarme com o modal de aviso. */
void screens_trigger_alarm(const char *title)
{
    char hour[8], msg[40];
    clock_format_hhmm(hour, sizeof(hour));
    snprintf(msg, sizeof(msg), "%s - %s", title, hour);
    snprintf(alarm.title, sizeof(alarm.title), "%s", title);
    alarm.ringing = true;
    alarm.start = alarm.last_beep = app_ms();
    audio_alarm();
    ui_alert(title, strcmp(title, "Temporizador") == 0 ? "timer" : "alarm",
              strcmp(title, "Temporizador") == 0 ? "A contagem terminou!" : msg, "OK", alarm_answer, NULL);
}
