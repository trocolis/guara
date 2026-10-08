/*
 * Telas do Relógio: editor de hora, despertadores, cronômetro, temporizador,
 * hora de dormir, ajuste manual e sincronização NTP.
 */
#include <stdio.h>
#include <string.h>

#include "alarms.h"
#include "app.h"
#include "audio.h"
#include "widgets.h"
#include "config.h"
#include "modal.h"
#include "rtc_clock.h"
#include "submenu.h"
#include "screens.h"
#include "network.h"

/* ========================================================================= */
/* Editor de hora "--:--" (README > Relógio > Despertador)                   */
/* ========================================================================= */

typedef void (*time_edit_fn)(int value, bool cancelled);

static struct {
    char title[24];
    int digits[4];
    int cursor;
    bool duration; /* MM:SS em vez de HH:MM */
    bool is_edit;
    time_edit_fn fn;
} ed;

/* Maior valor permitido para cada dígito do editor. */
static int max_digit(int i)
{
    if (ed.duration) {
        return i == 2 ? 5 : 9;
    }
    switch (i) {
    case 0: return 2;
    case 1: return ed.digits[0] == 2 ? 3 : 9;
    case 2: return 5;
    default: return 9;
    }
}

/* Valor do editor em minutos (HH:MM) ou segundos (MM:SS). */
static int editor_value(void)
{
    int a = ed.digits[0] * 10 + ed.digits[1], b = ed.digits[2] * 10 + ed.digits[3];
    return ed.duration ? a * 60 + b : a * 60 + b;
}

/* Teclas do editor: ←/→ movem o cursor, ↑/↓ mudam o dígito, A salva, B cancela. */
static void ed_key(const key_event_t *ev)
{
    if (ev->type == KEY_RELEASED) {
        return;
    }
    switch (ev->on_key) {
    case KEY_LEFT:
        ed.cursor = (ed.cursor + 3) % 4;
        break;
    case KEY_RIGHT:
        ed.cursor = (ed.cursor + 1) % 4;
        break;
    case KEY_UP:
        ed.digits[ed.cursor] = ed.digits[ed.cursor] >= max_digit(ed.cursor) ? 0 : ed.digits[ed.cursor] + 1;
        break;
    case KEY_DOWN:
        ed.digits[ed.cursor] = ed.digits[ed.cursor] <= 0 ? max_digit(ed.cursor) : ed.digits[ed.cursor] - 1;
        break;
    case KEY_A:
    case KEY_B:
        if (ev->type == KEY_PRESSED) {
            time_edit_fn fn = ed.fn;
            int v = editor_value();
            bool cancelled = ev->on_key == KEY_B;
            ui_back();
            fn(v, cancelled);
        }
        return;
    default:
        break;
    }
    if (ed.digits[1] > max_digit(1)) {
        ed.digits[1] = max_digit(1);
    }
}

/* Dígitos grandes centralizados; o cursor é um retângulo cheio que inverte a fonte e pisca. */
static void draw_digits(gfx_t *g, const int d[4], int cursor, uint32_t now, int y)
{
    const int scale = 2, step = 6 * scale, x0 = (GFX_WIDTH - 5 * step + scale) / 2;
    for (int i = 0; i < 5; i++) {
        int x = x0 + i * step;
        char s[2] = {i == 2 ? ':' : (char)('0' + d[i < 2 ? i : i - 1]), 0};
        bool sel = i != 2 && (i < 2 ? i : i - 1) == cursor && (now / 400) % 2 == 0;
        if (sel) {
            gfx_rect_rounded_filled(g, x - 2, y - 2, step + 2, 8 * scale + 2, 2, GFX_WHITE);
        }
        gfx_text_scaled(g, x, y, s, sel ? GFX_BLACK : GFX_WHITE, scale);
    }
}

/* Desenha o editor de hora. */
static void ed_draw(gfx_t *g, uint32_t now)
{
    ui_title(g, ed.title, now);
    draw_digits(g, ed.digits, ed.cursor, now, 22);
    ui_footer(g, "CANCELAR", "SALVAR", now);
}

static const screen_t SCREEN_EDIT_HOUR = {
    .name = "editar_hora",
    .on_key = ed_key,
    .on_draw = ed_draw,
};

/* Abre o editor de hora com o valor inicial. */
static void edit_time(const char *title, int value, bool duration, bool is_edit, time_edit_fn fn)
{
    memset(&ed, 0, sizeof(ed));
    snprintf(ed.title, sizeof(ed.title), "%s", title);
    int a = value / 60, b = value % 60;
    ed.digits[0] = a / 10 % 10;
    ed.digits[1] = a % 10;
    ed.digits[2] = b / 10;
    ed.digits[3] = b % 10;
    ed.duration = duration;
    ed.is_edit = is_edit;
    ed.fn = fn;
    ui_open(&SCREEN_EDIT_HOUR, NULL);
}

/* ========================================================================= */
/* Despertadores                                                             */
/* ========================================================================= */

static ui_list_t alarm_list;

/* Itens da lista de despertadores ("Criar" + horários). */
static void item_alarm(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    if (i == 0) {
        it->icon = "add";
        snprintf(it->text, sizeof(it->text), "Criar");
        return;
    }
    int m = alarms_minute_at(i - 1);
    it->icon = "alarm";
    snprintf(it->text, sizeof(it->text), "%02d:%02d", m / 60, m % 60);
}

/* Avisa que é preciso um cartão SD para salvar. */
static void no_card(void)
{
    audio_error();
    ui_alert("DESPERTADOR", "sd_card", "Insira o cartão SD para salvar.", "OK", NULL, NULL);
}

/* Salva um despertador novo. */
static void new_saved(int value, bool cancelled)
{
    if (!cancelled && !alarms_add(value)) {
        no_card();
    }
}

static int editing_index;
/* Confirma a exclusão do despertador. */
static void erase_answer(int r, void *ctx)
{
    (void)ctx;
    if (r == 1) {
        alarms_remove(editing_index);
    }
}

/* Salva a edição; cancelar oferece apagar o despertador. */
static void edited(int value, bool cancelled)
{
    if (cancelled) {
        ui_confirm("DESPERTADOR", "alarm", "Apagar este despertador?", "NAO", "SIM", erase_answer, NULL);
    } else if (!alarms_edit(editing_index, value)) {
        no_card();
    }
}

/* Teclas da lista de despertadores. */
static void alarms_key(const key_event_t *ev)
{
    alarm_list.total = alarms_count() + 1;
    if (ui_list_key(&alarm_list, ev, UI_LINES_VISIBLE) || ev->type != KEY_PRESSED) {
        return;
    }
    if (ev->on_key == KEY_B) {
        ui_back();
    } else if (ev->on_key == KEY_A) {
        if (alarm_list.sel == 0) {
            if (alarms_count() >= ALARMS_MAX) {
                ui_alert("DESPERTADOR", "alarm", "Limite de despertadores atingido.", "OK", NULL, NULL);
            } else {
                edit_time("NOVO DESPERTADOR", clock_minute_of_day() >= 0 ? clock_minute_of_day() : 0, false,
                            false, new_saved);
            }
        } else {
            editing_index = alarm_list.sel - 1;
            edit_time("DESPERTADOR", alarms_minute_at(editing_index), false, true, edited);
        }
    }
}

/* Desenha a lista de despertadores. */
static void alarms_draw(gfx_t *g, uint32_t now)
{
    alarm_list.total = alarms_count() + 1;
    ui_title(g, "DESPERTADOR", now);
    ui_list_draw(g, &alarm_list, UI_LIST_Y, UI_LINES_VISIBLE, item_alarm, NULL, now);
}

static const screen_t SCREEN_ALARM_CLOCKS = {
    .name = "despertadores",
    .on_key = alarms_key,
    .on_draw = alarms_draw,
};

/* ========================================================================= */
/* Cronômetro                                                                */
/* ========================================================================= */

static struct {
    bool running;
    uint32_t start, elapsed;
} stopwatch;

/* A inicia ou para o cronômetro; B sai. */
static void stopwatch_key(const key_event_t *ev)
{
    if (ev->type != KEY_PRESSED) {
        return;
    }
    if (ev->on_key == KEY_B) {
        app_block_sleep(false);
        ui_back();
    } else if (ev->on_key == KEY_A) {
        if (stopwatch.running) {
            stopwatch.elapsed = app_ms() - stopwatch.start;
            stopwatch.running = false;
        } else {
            stopwatch.start = app_ms();
            stopwatch.elapsed = 0;
            stopwatch.running = true;
        }
        app_block_sleep(stopwatch.running);
    }
}

/* Desenha o tempo do cronômetro. */
static void stopwatch_draw(gfx_t *g, uint32_t now)
{
    uint32_t ms = stopwatch.running ? now - stopwatch.start : stopwatch.elapsed;
    char buf[16];
    uint32_t s = ms / 1000;
    if (s >= 3600) {
        snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
                 (unsigned long)(s % 60));
    } else {
        snprintf(buf, sizeof(buf), "%02lu:%02lu.%lu", (unsigned long)(s / 60), (unsigned long)(s % 60),
                 (unsigned long)(ms / 100 % 10));
    }
    ui_title(g, "CRONOMETRO", now);
    int w = gfx_text_width(buf) * 2 + 1;
    gfx_text_scaled(g, (GFX_WIDTH - w) / 2, 24, buf, GFX_WHITE, 2);
    ui_footer(g, "SAIR", stopwatch.running ? "PARAR" : "INICIAR", now);
}

static const screen_t SCREEN_STOPWATCH = {
    .name = "cronometro",
    .fps = 30,
    .on_key = stopwatch_key,
    .on_draw = stopwatch_draw,
};

/* ========================================================================= */
/* Temporizador (roda em segundo plano; editável quando parado)              */
/* ========================================================================= */

static int timer_digits[4] = {0, 5, 0, 0};
static int timer_cursor;

/* Edita a duração quando parado; A inicia ou para; B sai. */
static void timer_key(const key_event_t *ev)
{
    if (ev->type == KEY_RELEASED) {
        return;
    }
    bool stopped = !timer_active();
    if (stopped) {
        static const int maximums[4] = {9, 9, 5, 9};
        switch (ev->on_key) {
        case KEY_LEFT: timer_cursor = (timer_cursor + 3) % 4; return;
        case KEY_RIGHT: timer_cursor = (timer_cursor + 1) % 4; return;
        case KEY_UP:
            timer_digits[timer_cursor] = timer_digits[timer_cursor] >= maximums[timer_cursor] ? 0 : timer_digits[timer_cursor] + 1;
            return;
        case KEY_DOWN:
            timer_digits[timer_cursor] = timer_digits[timer_cursor] <= 0 ? maximums[timer_cursor] : timer_digits[timer_cursor] - 1;
            return;
        default: break;
        }
    }
    if (ev->type != KEY_PRESSED) {
        return;
    }
    if (ev->on_key == KEY_B) {
        ui_back();
    } else if (ev->on_key == KEY_A) {
        if (stopped) {
            uint32_t s = (uint32_t)((timer_digits[0] * 10 + timer_digits[1]) * 60 + timer_digits[2] * 10 + timer_digits[3]);
            if (s > 0) {
                timer_start(s);
            }
        } else {
            timer_stop();
        }
    }
}

/* Desenha a contagem regressiva ou a duração editável. */
static void timer_draw(gfx_t *g, uint32_t now)
{
    ui_title(g, "TEMPORIZADOR", now);
    if (timer_active()) {
        uint32_t r = timer_remaining();
        int d[4] = {(int)(r / 600 % 10), (int)(r / 60 % 10), (int)(r % 60 / 10), (int)(r % 10)};
        draw_digits(g, d, -1, now, 22);
    } else {
        draw_digits(g, timer_digits, timer_cursor, now, 22);
    }
    ui_footer(g, "SAIR", timer_active() ? "PARAR" : "INICIAR", now);
}

static const screen_t SCREEN_TIMER = {
    .name = "temporizador",
    .on_key = timer_key,
    .on_draw = timer_draw,
};

/* ========================================================================= */
/* Hora de dormir, ajuste manual e NTP                                       */
/* ========================================================================= */

/* Salva a hora de dormir. */
static void bedtime_saved(int v, bool cancelled)
{
    if (!cancelled) {
        g_cfg.bedtime_minute = (uint16_t)v;
        config_save();
    }
}

/* Salva a hora de acordar. */
static void wake_time_saved(int v, bool cancelled)
{
    if (!cancelled) {
        g_cfg.wake_minute = (uint16_t)v;
        config_save();
    }
}

static void info_bedtime(char *b, int t) { snprintf(b, (size_t)t, "%02d%02d", g_cfg.bedtime_minute / 60, g_cfg.bedtime_minute % 60); }
static void info_wake_time(char *b, int t) { snprintf(b, (size_t)t, "%02d%02d", g_cfg.wake_minute / 60, g_cfg.wake_minute % 60); }
static void open_bedtime(void) { edit_time("HORA DE DORMIR", g_cfg.bedtime_minute, false, false, bedtime_saved); }
static void open_wake_time(void) { edit_time("HORA DE ACORDAR", g_cfg.wake_minute, false, false, wake_time_saved); }

static const submenu_item_t ITEMS_SLEEP[] = {
    {"moon_face", "Dormir", info_bedtime, open_bedtime},
    {"clock", "Acordar", info_wake_time, open_wake_time},
};
static const submenu_t MENU_SLEEP = {"HORA DE DORMIR", ITEMS_SLEEP, 2};

/* Aplica o ajuste manual de hora. */
static void manual_time_saved(int v, bool cancelled)
{
    if (!cancelled) {
        clock_set_local(v / 60, v % 60);
    }
}

/* Abre o editor do ajuste manual. */
static void open_manual_adjust(void)
{
    int m = clock_minute_of_day();
    edit_time("AJUSTE MANUAL", m >= 0 ? m : 12 * 60, false, false, manual_time_saved);
}

/* Sincroniza a hora pelo NTP usando o serviço de rede. */
static void sync(void)
{
    if (!network_allowed()) {
        audio_error();
        ui_alert("NTP", "satellite", "Ligue o Wi-Fi em Ajustes.", "OK", NULL, NULL);
        return;
    }
    network_use();
    bool ok = network_wait(12000) && clock_sync_ntp(6000);
    network_release();
    char hour[8];
    clock_format_hhmm(hour, sizeof(hour));
    char msg[48];
    snprintf(msg, sizeof(msg), ok ? "Hora atualizada: %s" : "Falhou. Configure o Wi-Fi.", hour);
    if (!ok) {
        audio_error();
    }
    ui_alert("NTP", "satellite", msg, "OK", NULL, NULL);
}

static const submenu_item_t ITEMS_CONFIG[] = {
    {"wristwatch", "Ajuste manual", NULL, open_manual_adjust},
    {"satellite", "Sincronizar NTP", NULL, sync},
};
static const submenu_t MENU_CONFIG = {"CONFIGURAR", ITEMS_CONFIG, 2};

/* Abre a lista de despertadores. */
static void open_alarm_clocks(void)
{
    alarm_list = (ui_list_t){.total = alarms_count() + 1};
    ui_open(&SCREEN_ALARM_CLOCKS, NULL);
}
static void open_stopwatch(void) { ui_open(&SCREEN_STOPWATCH, NULL); }
static void open_timer(void) { ui_open(&SCREEN_TIMER, NULL); }
static void open_bedtime_menu(void) { submenu_open(&MENU_SLEEP); }
static void open_clock_config_menu(void) { submenu_open(&MENU_CONFIG); }

/* Quantidade de despertadores à direita do item. */
static void info_alarms(char *b, int t)
{
    if (alarms_count()) {
        snprintf(b, (size_t)t, "%d", alarms_count());
    }
}

/* Tempo restante do temporizador à direita do item. */
static void info_timer(char *b, int t)
{
    if (timer_active()) {
        uint32_t r = timer_remaining();
        snprintf(b, (size_t)t, "%02lu%02lu", (unsigned long)(r / 60 % 100), (unsigned long)(r % 60));
    }
}

static const submenu_item_t ITEMS_CLOCK[] = {
    {"alarm", "Despertador", info_alarms, open_alarm_clocks},
    {"stopwatch", "Cronômetro", NULL, open_stopwatch},
    {"timer", "Temporizador", info_timer, open_timer},
    {"moon_face", "Hora de dormir", NULL, open_bedtime_menu},
    {"gear", "Configurar", NULL, open_clock_config_menu},
};
static const submenu_t MENU_CLOCK = {"RELOGIO", ITEMS_CLOCK, 5};

/* Abre o sub-menu do Relógio. */
void clock_open_menu(void)
{
    submenu_open(&MENU_CLOCK);
}
