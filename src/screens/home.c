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
#include "item_fx.h"
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
    uint32_t angry_until;      /* olhar de raiva ao recusar um item que não faria efeito */
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
    if (now < anim.angry_until) return EYE_ANGER;
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

/* Relógio à esquerda do usuário e indicadores à direita (bateria, Wi-Fi, Visível, etc.). */
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
    if (g_cfg.link_visible) {
        ui_icon(g, "visible", x, 1); /* Conectar > Visível: outros Guarás podem encontrá-lo */
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

/* Balão de estados com `icon` dentro (NULL = balão vazio), "respirando". */
static void draw_balloon(gfx_t *g, uint32_t now, const char *icon)
{
    /* "respiração": desce 8 px e volta, suavemente (ciclo de 3 s) */
    static const uint8_t wave[] = {0, 0, 1, 2, 3, 4, 5, 6, 7, 7, 8, 8, 8, 7, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0};
    int dy = wave[(now / 125) % sizeof(wave)];
    ui_icon_mode(g, "speech_balloon", BALLOON_X, BALLOON_Y + dy, GFX_COPY, 0);
    if (icon) {
        ui_icon(g, icon, BALLOON_X + 2, BALLOON_Y + dy + 2);
    }
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
        draw_balloon(g, now, icon);
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

static struct {
    catalog_record_t item;
    uint32_t start;
    int last_frame;
    bool absorbed; /* partículas já lançadas (som tocado) */
    bool refused;  /* item recusado (não faria efeito): só o ícone, raiva e o balão "não" */
    char texts[FX_MAX_TEXTS][FX_TEXT_SIZE];
    int n_texts;
} usage;

#define FRAME_BITE_MS 350 /* duração de cada quadro da mordida */
#define REFUSE_MS 2000     /* recusa: o "não" pisca 1,5 vez por segundo (3 vezes) */
#define REFUSE_BLINK_MS 333 /* metade do ciclo da piscada */
#define BITES 5
#define TEXT_X 22 /* textos do efeito: centro da área livre à esquerda do Guará (onde fica o balão) */
#define TEXT_Y 44 /* linha de partida; sobe até Y=14 */
#define GUARA_CENTER_Y 36 /* centro aproximado do Guará 2x, alvo das partículas */

/* Prepara a animação do item usado. */
static void item_use_on_open(void *param)
{
    int id = (int)(intptr_t)param;
    catalog_read(CAT_ITEMS, id, &usage.item);
    usage.start = app_ms();
    usage.last_frame = -1;
    usage.absorbed = false;
}

/* A animação não pode ser interrompida. */
static void item_use_on_key(const key_event_t *ev)
{
    (void)ev; /* a animação não pode ser interrompida */
}

/* Duração do consumo: as mordidas da comida ou o gole rápido da bebida e da poção. */
static uint32_t consume_ms(void)
{
    return usage.item.type == ITEM_FOOD ? (BITES + 1) * FRAME_BITE_MS : FX_SIP_MS;
}

/*
 * Consumo do ícone: a comida é coberta pelas mordidas, uma a cada
 * FRAME_BITE_MS; a bebida e a poção sobem e tombam na direção do Guará.
 */
static void draw_consume(gfx_t *g, uint32_t now, uint32_t t)
{
    if (usage.item.type != ITEM_FOOD) {
        int y = ITEM_Y - (int)(t * FX_RISE_PX / FX_SIP_MS);
        fx_icon_rotated(g, usage.item.icon, ITEM_X, y, (float)t * FX_POUR_DEGREES / FX_SIP_MS);
        return;
    }
    ui_icon_mode(g, usage.item.icon, ITEM_X, ITEM_Y, GFX_COPY, 0);
    int frame = (int)(t / FRAME_BITE_MS);
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
}

/* Inverte só a silhueta do Guará: compõe o sprite num quadro à parte, igual ao da cena. */
static void invert_guara(gfx_t *g, uint32_t now)
{
    static gfx_t sprite;
    gfx_init(&sprite);
    sprite_guara(&sprite, g_guara.color, g_guara.head, g_guara.stage, EYE_HAPPY, GUARA_X, GUARA_BASE,
                 guara_frame(now), GFX_SCALE2X);
    fx_invert_silhouette(g, &sprite);
}

/*
 * Consumo (mordidas ou gole) e, depois, as partículas voam até o Guará, ele
 * pisca invertido e os textos do efeito sobem ao mesmo tempo.
 */
static void item_use_on_draw(gfx_t *g, uint32_t now)
{
    uint32_t t = now - usage.start;
    draw_home_scene(g, now, false);
    if (usage.refused) {
        /* Recusa: o item aparece, o Guará fica com raiva e o balão mostra o "não" piscando (1,5 vez/s) */
        if (t >= REFUSE_MS) {
            anim.angry_until = 0;
            ui_back();
            return;
        }
        anim.angry_until = now + 100;
        ui_icon_mode(g, usage.item.icon, ITEM_X, ITEM_Y, GFX_COPY, 0);
        draw_balloon(g, now, (t / REFUSE_BLINK_MS) % 2 == 0 ? "not" : NULL);
        return;
    }
    if (t < consume_ms()) {
        draw_consume(g, now, t);
        return;
    }
    uint32_t ta = t - consume_ms();
    if (!usage.absorbed) {
        usage.absorbed = true;
        audio_magic();
    }
    /* Partículas da boca do frasco tombado (bebida e poção) ou do centro da comida */
    int x0 = ITEM_X + 8, y0 = ITEM_Y + 8;
    if (usage.item.type != ITEM_FOOD) {
        fx_icon_mouth(ITEM_X, ITEM_Y - FX_RISE_PX, FX_POUR_DEGREES, &x0, &y0);
    }
    fx_particles(g, x0, y0, GUARA_X, GUARA_CENTER_Y, ta);
    if (ta < FX_FLY_TOTAL_MS) {
        return;
    }
    uint32_t tf = ta - FX_FLY_TOTAL_MS;
    if (tf < FX_FLASH_MS) {
        anim.bite_happy_until = now + FX_FLASH_STEP_MS;
        if (fx_flash_on(tf)) {
            invert_guara(g, now);
        }
    }
    bool texts = fx_texts(g, usage.texts, usage.n_texts, tf, TEXT_X, TEXT_Y);
    if (!texts && tf >= FX_FLASH_MS) {
        home_set_wink(now + 1500); /* depois do uso, piscadela e volta para onde estava */
        ui_back();
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
    usage.refused = res == USE_FULL;
    if (usage.refused) {
        /* Não faria efeito: o item não é gasto e o Guará recusa na tela principal */
        audio_error();
        app_interact();
        ui_open(&SCREEN_USE_ITEM, (void *)(intptr_t)item_id);
        return;
    }
    if (res != USE_OK) {
        static const char *const reasons[] = {"", "Desmaiado! Espere ou use uma cura.", "Amaldiçoado! Não pode usar itens.",
                                              "Sem estoque.", "Item inválido."};
        audio_error();
        ui_alert(r.name, r.icon, reasons[res], "OK", NULL, NULL);
        return;
    }
    fx_values_t from = {before.hp, before.mp, before.energy, before.mood, before.satiety, before.states};
    fx_values_t to = {g_guara.hp, g_guara.mp, g_guara.energy, g_guara.mood, g_guara.satiety, g_guara.states};
    usage.n_texts = fx_build_texts(&from, &to, usage.texts, FX_MAX_TEXTS);
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
