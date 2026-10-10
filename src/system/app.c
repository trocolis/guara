/*
 * Aplicação: boot, laço principal (teclas, quadros, ciclo de vida a cada minuto),
 * alarmes, deep sleep por inatividade, emergência de bateria e serviços usados
 * pelas telas (gravar, ganhar XP, nascer, renascer).
 */
#include "app.h"

#include <stdio.h>
#include <string.h>

#include "alarms.h"
#include "audio.h"
#include "battery.h"
#include "buttons.h"
#include "widgets.h"
#include "config.h"
#include "display.h"
#include "power.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "persistence.h"
#include "network.h"
#include "radio_link.h"
#include "rtc_clock.h"
#include "sdcard.h"
#include "screens.h"
#include "ui.h"

static const char *TAG = "app";

guara_t g_guara;
bool g_has_guara;
rng_t g_rng;

static gfx_t screen;
static bool sleep_blocked;
static volatile bool sleep_requested;
static uint32_t monotonic_minutes;
static int consecutive_critical;

#define IDLE_AFTER_MS 30000 /* sem botões por 30 s: modo Ocioso */
#define MENU_TIMEOUT_MS 60000 /* menu aberto sem botões por 1 min: volta à tela principal */
#define IDLE_MAX_FPS 5
static int swallowed_key = -1; /* tecla que só acordou do Ocioso: ignorada até ser solta */

/* Milissegundos desde o boot. */
uint32_t app_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Versão do firmware (GUARA_VERSION do platformio.ini). */
const char *app_version(void)
{
    return GUARA_VERSION;
}

/* Preenche o contexto de relógio usado pelo ciclo de vida. */
void app_clock_context(clock_ctx_t *clock_ctx)
{
    clock_ctx->minute_of_day = clock_minute_of_day();
    clock_ctx->bedtime_minute = g_cfg.bedtime_minute;
    clock_ctx->wake_minute = g_cfg.wake_minute;
}

/* Registra uma interação do usuário com o Guará. */
void app_interact(void)
{
    if (!g_has_guara) {
        return;
    }
    clock_ctx_t clock_ctx;
    app_clock_context(&clock_ctx);
    guara_interact(&g_guara, &clock_ctx);
}

/* O Guará mudou: espelha na RTC RAM e marca para gravar na Flash (Pilar 2). */
void app_mark_changed(void)
{
    if (g_has_guara) {
        persist_mirror(&g_guara);
        persist_mark_dirty();
    }
}

/* Grava na Flash imediatamente, atualizando o timestamp. */
void app_save(void)
{
    if (g_has_guara) {
        if (clock_is_valid()) {
            g_guara.timestamp = clock_now();
        }
        persist_save(&g_guara);
    }
}

/* Impede o deep sleep por inatividade (jogos, combate, cronômetro). */
void app_block_sleep(bool should_block)
{
    sleep_blocked = should_block;
}

/* Concede XP; ao subir de nível toca o som e grava na hora. */
int app_gain_xp(uint32_t points)
{
    uint8_t stage = g_guara.stage, nature = g_guara.nature;
    int levels = guara_gain_xp(&g_guara, points, &g_rng);
    if (levels > 0) {
        audio_level_up();
        app_save();
        if (g_guara.stage != stage || g_guara.nature != nature) {
            ESP_LOGI(TAG, "Evolução: estágio %d, natureza %s", g_guara.stage, nature_name(g_guara.nature));
        }
    } else {
        app_mark_changed();
    }
    return levels;
}

/* Cria o Guará e, nas versões 0.x.x, libera o catálogo. */
void app_birth(const char *name)
{
    guara_birth(&g_guara, name, &g_rng, clock_is_valid() ? clock_now() : 0);
    g_has_guara = true;
    if (guara_is_development_version(GUARA_VERSION)) {
        guara_unlock_catalog(&g_guara);
    }
    app_save();
}

/* Factory Rebirth: apaga o save e volta para a tela de nascimento. */
void app_rebirth(void)
{
    persist_erase();
    g_has_guara = false;
    memset(&g_guara, 0, sizeof(g_guara));
    ui_clear_stack(&SCREEN_BIRTH, NULL);
}

/* ------------------------------------------------------------------------- */
/* Boot                                                                      */
/* ------------------------------------------------------------------------- */

/* Desenha a tela de boot com o logo e a barra de progresso. */
static void draw_boot(int progress)
{
    gfx_clear(&screen);
    const gfx_bitmap_t *logo = vrom("bitart_logo");
    if (logo) {
        gfx_bitmap(&screen, logo, (GFX_WIDTH - logo->width) / 2, 6, GFX_COPY, 0);
    } else {
        gfx_text_center(&screen, 0, GFX_WIDTH, 20, "bitart", GFX_WHITE);
    }
    ui_progress(&screen, 14, 52, 100, 6, progress, 100);
    display_send(&screen);
}

/* Mostra como regravar a partição storage quando ela está inválida. */
static void show_storage_error(storage_status_t st)
{
    gfx_clear(&screen);
    gfx_text(&screen, 0, 0, "STORAGE INVALIDO", GFX_WHITE);
    char buf[24];
    snprintf(buf, sizeof(buf), "codigo %d", st);
    gfx_text(&screen, 0, 12, buf, GFX_WHITE);
    gfx_text(&screen, 0, 30, "Grave com:", GFX_WHITE);
    gfx_text(&screen, 0, 40, "pio run -t", GFX_WHITE);
    gfx_text(&screen, 0, 50, "upload_storage", GFX_WHITE);
    display_send(&screen);
}

/* Boot: periféricos, storage, relógio, cartão SD, save e recuperação temporal. */
void app_init(void)
{
    rng_seed(&g_rng, esp_random());
    power_init();
    config_load();
    audio_init();
    audio_mute(g_cfg.mute);
    audio_volume(g_cfg.volume);

    wake_reason_t reason = power_wake_reason();
    bool cold = reason == WOKE_COLD;
    gfx_init(&screen);
    if (!display_init()) {
        ESP_LOGE(TAG, "Sem display; seguindo sem vídeo");
    }

    storage_status_t st = persist_open_storage();
    if (st != STORAGE_OK) {
        show_storage_error(st);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    if (cold) {
        draw_boot(10);
    }

    clock_init(g_cfg.timezone);
    buttons_init();
    battery_init();
    if (cold) {
        draw_boot(30);
    }

    sd_mount();
    alarms_load();
    if (cold) {
        draw_boot(50);
    }

    persist_init();
    g_has_guara = persist_load(&g_guara);
    /* Regra das versões 0.x.x: todo o catálogo liberado (itens com 255 unidades) a cada boot */
    if (g_has_guara && guara_is_development_version(GUARA_VERSION)) {
        guara_unlock_catalog(&g_guara);
        app_mark_changed();
    }
    if (cold) {
        draw_boot(65);
    }

    /* Wi-Fi sob demanda: a hora é sincronizada em segundo plano, sem prender o boot */
    network_init();

    if (g_has_guara && clock_is_valid()) {
        uint32_t min = guara_catch_up_time(&g_guara, clock_now(), clock_minute_of_day(), g_cfg.bedtime_minute,
                                             g_cfg.wake_minute);
        if (min > 0) {
            ESP_LOGI(TAG, "Recuperação temporal: %lu minutos", (unsigned long)min);
            app_mark_changed();
        }
    }

    if (cold) {
        draw_boot(100);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (g_has_guara) {
        ui_clear_stack(&SCREEN_HOME, NULL);
    } else {
        ui_clear_stack(&SCREEN_BIRTH, NULL);
    }
    if (reason == WOKE_TIMER) {
        /* acordou para um despertador/temporizador: o laço trata o disparo */
        ESP_LOGI(TAG, "Acordou pelo timer");
    }
}

/* ------------------------------------------------------------------------- */
/* Laço principal                                                            */
/* ------------------------------------------------------------------------- */

/* Avança um minuto do Guará e grava na hora se ele desmaiou. */
static void run_pet_minute(void)
{
    if (!g_has_guara) {
        return;
    }
    clock_ctx_t clock_ctx;
    app_clock_context(&clock_ctx);
    uint8_t states_before = guara_has_state(&g_guara, ST_FAINTED);
    guara_tick_minute(&g_guara, &clock_ctx, false);
    if (clock_is_valid()) {
        g_guara.timestamp = clock_now();
    }
    persist_mirror(&g_guara);
    if (!states_before && guara_has_state(&g_guara, ST_FAINTED)) {
        app_save(); /* desmaio é um evento crítico */
    }
}

/* Pede o deep sleep imediato; atendido pelo laço principal (console de depuração). */
void app_request_sleep(void)
{
    sleep_requested = true;
}

/* Grava o que estiver pendente e entra em deep sleep até o próximo alarme. */
/* Para a presença do Conectar antes do deep sleep (o network_turn_off devolve o rádio). */
static void stop_link(void)
{
    link_profile_t me;
    link_profile_from_guara(&g_guara, &me);
    radio_link_background(false, &me);
}

/*
 * Conectar em segundo plano (a cada segundo): com Conectar > Visível em ON, o
 * Guará fica visível para outros enquanto o aparelho está em uso (o ESP-NOW não
 * depende da opção Wi-Fi); o rádio desliga no modo Ocioso e religa sozinho ao
 * interagir. Uma oferta ou um desafio que chegar
 * abre a busca do Conectar, menos nas telas que seguram o sono (jogos,
 * combate, cronômetro), onde espera.
 */
static void update_link(void)
{
    link_profile_t me;
    link_profile_from_guara(&g_guara, &me);
    radio_link_background(g_cfg.link_visible && g_has_guara && !power_is_idle(), &me);
    if (!radio_link_held() && !sleep_blocked && radio_link_has_invite()) {
        screens_open_meet(-1, 0);
    }
}

static void sleep_due_to_inactivity(void)
{
    persist_save_if_dirty(&g_guara);
    if (g_has_guara) {
        persist_mirror(&g_guara);
    }
    uint32_t wake = alarms_seconds_until_next();
    uint32_t t = timer_remaining();
    if (timer_active() && (wake == 0 || t < wake)) {
        wake = t ? t : 1;
    }
    sd_unmount();
    stop_link();
    network_turn_off();
    power_deep_sleep(wake);
}

/* Entra em deep sleep após o tempo de economia sem uso (se nada impedir). */
static void check_inactivity(uint32_t now)
{
    uint8_t minutes = CONFIG_POWER_SAVE_MIN[g_cfg.power_save < CONFIG_COUNT_POWER_SAVE ? g_cfg.power_save : 0];
    if (minutes == 0 || sleep_blocked || audio_music_active() || screens_alarm_ringing()) {
        return;
    }
    if (now - buttons_last_use_ms() > (uint32_t)minutes * 60000u) {
        ESP_LOGI(TAG, "Inatividade: entrando em deep sleep");
        sleep_due_to_inactivity();
    }
}

/* Três leituras críticas seguidas disparam o salvamento de emergência e o sono. */
static void check_battery(void)
{
    battery_update();
    consecutive_critical = battery_critical() ? consecutive_critical + 1 : 0;
    if (consecutive_critical >= 3) {
        ESP_LOGW(TAG, "Bateria crítica (%d mV): salvamento de emergência", battery_mv());
        app_save();
        gfx_clear(&screen);
        ui_icon(&screen, "battery", 56, 14);
        gfx_text_center(&screen, 0, GFX_WIDTH, 40, "Bateria fraca", GFX_WHITE);
        gfx_text_center(&screen, 0, GFX_WIDTH, 50, "Conecte o carregador", GFX_WHITE);
        display_send(&screen);
        vTaskDelay(pdMS_TO_TICKS(3000));
        sd_unmount();
        stop_link();
        network_turn_off();
        power_deep_sleep(0);
    }
}

/*
 * Menu aberto por MENU_TIMEOUT_MS sem nenhum botão: volta à tela principal.
 * Não vale em jogos, combate e cronômetro (bloqueiam o sono), com alarme
 * tocando, antes do nascimento e nas telas de leitura (keep_open).
 */
static void check_menu_timeout(uint32_t now)
{
    const screen_t *top = ui_top();
    if (!g_has_guara || top == &SCREEN_HOME || (top && top->keep_open) || sleep_blocked ||
        screens_alarm_ringing() || now - buttons_last_use_ms() < MENU_TIMEOUT_MS) {
        return;
    }
    ESP_LOGI(TAG, "Menu ocioso: voltando à tela principal");
    ui_back_until(&SCREEN_HOME);
}

/* Entra no modo Ocioso após 30 s sem botões, exceto nas telas que impedem o sono. */
static void check_idle(uint32_t now)
{
    bool should_idle = !sleep_blocked && !screens_alarm_ringing() && now - buttons_last_use_ms() > IDLE_AFTER_MS;
    if (should_idle && !power_is_idle()) {
        power_set_idle(true);
        buttons_set_idle(true);
    } else if (!should_idle && power_is_idle() && (sleep_blocked || screens_alarm_ringing())) {
        power_set_idle(false); /* um jogo ou alarme começou */
        buttons_set_idle(false);
    }
}

/*
 * Trata uma tecla. No modo Ocioso, o primeiro toque só acorda a tela: a tecla
 * é ignorada até ser solta.
 */
static void handle_key(const key_event_t *ev)
{
    if (power_is_idle()) {
        power_set_idle(false);
        buttons_set_idle(false);
        if (ev->type == KEY_PRESSED) {
            swallowed_key = ev->key;
        }
        return;
    }
    if (swallowed_key == (int)ev->key) {
        if (ev->type == KEY_RELEASED) {
            swallowed_key = -1;
        }
        return;
    }
    ui_process_key(ev);
}

/* Laço principal: teclas, quadros na taxa da tela atual, tarefas por segundo e por minuto. */
void app_run(void)
{
    uint32_t last_frame = 0, last_second = 0, last_minute_ms = app_ms();
    for (;;) {
        int fps = ui_fps();
        if (power_is_idle() && fps > IDLE_MAX_FPS) {
            fps = IDLE_MAX_FPS;
        }
        uint32_t period = 1000u / (uint32_t)fps;
        uint32_t now = app_ms();
        uint32_t wait = now - last_frame >= period ? 0 : period - (now - last_frame);

        key_event_t ev;
        if (buttons_read(&ev, wait)) {
            handle_key(&ev);
            while (buttons_read(&ev, 0)) {
                handle_key(&ev);
            }
        }

        now = app_ms();
        if (now - last_frame >= period) {
            last_frame = now;
            ui_draw(&screen, now);
            display_send(&screen);
        }

        if (now - last_second >= 1000) {
            last_second = now;
            static int counter;
            if (++counter % 5 == 0) {
                check_battery();
            }
            if (alarms_check(clock_minute_of_day())) {
                screens_trigger_alarm("Despertador");
            }
            if (timer_check()) {
                screens_trigger_alarm("Temporizador");
            }
            /* Release nova achada em segundo plano: oferece só na tela principal, acordado */
            const char *release = network_new_release();
            if (release && ui_top() == &SCREEN_HOME && !power_is_idle()) {
                screens_offer_update(release);
            }
            if (sleep_requested) {
                sleep_due_to_inactivity();
            }
            check_inactivity(now);
            check_menu_timeout(now);
            update_link();
            check_idle(now);
        }

        while (now - last_minute_ms >= 60000) {
            last_minute_ms += 60000;
            monotonic_minutes++;
            run_pet_minute();
        }
    }
}
