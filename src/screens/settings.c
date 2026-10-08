/*
 * Ajustes: Wi-Fi, Bluetooth, Energia, Armazenamento, Controle, Som, Sistema
 * (com atualização pelo cartão SD) e Reiniciar.
 */
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "audio.h"
#include "battery.h"
#include "widgets.h"
#include "config.h"
#include "display.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modal.h"
#include "ota.h"
#include "persistence.h"
#include "rtc_clock.h"
#include "sdcard.h"
#include "submenu.h"
#include "screens.h"
#include "network.h"

/* ========================================================================= */
/* Wi-Fi                                                                     */
/* ========================================================================= */

static wifi_network_t networks[WIFI_MAX_NETWORKS];
static ui_list_t network_list;
static char ssid_chosen[33];

/* Mostra uma mensagem enquanto uma operação bloqueante roda. */
static void screen_busy(const char *text)
{
    /* Feedback imediato para operações bloqueantes (busca, conexão) */
    static gfx_t g;
    gfx_init(&g);
    ui_icon(&g, "wifi", 56, 14);
    gfx_text_center(&g, 0, GFX_WIDTH, 38, text, GFX_WHITE);
    display_send(&g);
}

/* Conecta a uma rede, salva e liga o Wi-Fi. */
static void connect(const char *ssid, const char *password)
{
    screen_busy("Conectando...");
    bool ok = network_connect_new(ssid, password);
    if (ok) {
        g_cfg.wifi_on = true;
        config_save();
        ui_alert("WI-FI", "wifi", "Conectado!", "OK", NULL, NULL);
    } else {
        audio_error();
        ui_alert("WI-FI", "wifi", "Não foi possível conectar.", "OK", NULL, NULL);
    }
}

/* Recebe a senha digitada e conecta. */
static void password_typed(const char *password, void *ctx)
{
    (void)ctx;
    if (password) {
        connect(ssid_chosen, password);
    }
}

/* Item da busca: nome da rede e sinal. */
static void item_network(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    it->icon = "wifi";
    snprintf(it->text, sizeof(it->text), "%s", networks[i].ssid);
    snprintf(it->info, sizeof(it->info), "%d", networks[i].rssi);
}

/* Escolhe a rede (pede senha se for protegida). */
static void scan_key(const key_event_t *ev)
{
    if (ui_list_key(&network_list, ev, UI_LINES_VISIBLE) || ev->type != KEY_PRESSED) {
        return;
    }
    if (ev->on_key == KEY_B) {
        ui_back();
    } else if (ev->on_key == KEY_A && network_list.total) {
        snprintf(ssid_chosen, sizeof(ssid_chosen), "%s", networks[network_list.sel].ssid);
        if (networks[network_list.sel].is_open) {
            connect(ssid_chosen, "");
        } else {
            ui_keyboard("Senha", "", 63, password_typed, NULL);
        }
    }
}

/* Desenha as redes encontradas. */
static void scan_draw(gfx_t *g, uint32_t now)
{
    ui_title(g, "REDES", now);
    if (network_list.total == 0) {
        gfx_text_center(g, 0, GFX_WIDTH, 32, "Nenhuma rede", GFX_WHITE);
        return;
    }
    ui_list_draw(g, &network_list, UI_LIST_Y, UI_LINES_VISIBLE, item_network, NULL, now);
}

static const screen_t SCREEN_SCAN_WIFI = {
    .name = "busca_wifi",
    .on_key = scan_key,
    .on_draw = scan_draw,
};

/* Busca redes e abre a lista. */
static void scan_wifi(void)
{
    screen_busy("Buscando redes...");
    network_list = (ui_list_t){0};
    network_list.total = network_scan(networks, WIFI_MAX_NETWORKS);
    ui_open(&SCREEN_SCAN_WIFI, NULL);
}

static saved_network_t saved_networks[CONFIG_MAX_NETWORKS];
static ui_list_t saved_list;

/* Item da lista de redes salvas. */
static void item_saved(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    it->icon = "save";
    snprintf(it->text, sizeof(it->text), "%s", saved_networks[i].ssid);
}

/* Conecta ou esquece a rede salva. */
static void saved_option(int r, void *ctx)
{
    (void)ctx;
    if (r == 0) {
        connect(saved_networks[saved_list.sel].ssid, saved_networks[saved_list.sel].password);
    } else if (r == 1) {
        config_forget_network(saved_list.sel);
        saved_list.total = config_networks(saved_networks);
    }
}

/* Abre as opções da rede salva. */
static void saved_key(const key_event_t *ev)
{
    if (ui_list_key(&saved_list, ev, UI_LINES_VISIBLE) || ev->type != KEY_PRESSED) {
        return;
    }
    if (ev->on_key == KEY_B) {
        ui_back();
    } else if (ev->on_key == KEY_A && saved_list.total) {
        static const char *const options[] = {"Conectar", "Esquecer"};
        ui_choice(saved_networks[saved_list.sel].ssid, options, 2, saved_option, NULL);
    }
}

/* Desenha as redes salvas. */
static void saved_draw(gfx_t *g, uint32_t now)
{
    ui_title(g, "REDES SALVAS", now);
    if (saved_list.total == 0) {
        gfx_text_center(g, 0, GFX_WIDTH, 32, "Nenhuma rede salva", GFX_WHITE);
        return;
    }
    ui_list_draw(g, &saved_list, UI_LIST_Y, UI_LINES_VISIBLE, item_saved, NULL, now);
}

static const screen_t SCREEN_NETWORKS_SAVED = {
    .name = "redes_salvas",
    .on_key = saved_key,
    .on_draw = saved_draw,
};

/* Abre a lista de redes salvas. */
static void open_saved(void)
{
    saved_list = (ui_list_t){0};
    saved_list.total = config_networks(saved_networks);
    ui_open(&SCREEN_NETWORKS_SAVED, NULL);
}

static void info_wifi(char *b, int t) { snprintf(b, (size_t)t, "%s", g_cfg.wifi_on ? "ON" : "OFF"); }

/* Alterna a permissão de rede (ON não liga o rádio na hora). */
static void toggle_wifi(void)
{
    g_cfg.wifi_on = !g_cfg.wifi_on;
    config_save();
    if (!g_cfg.wifi_on) {
        network_turn_off();
    }
    /* Com ON nada liga agora: o rádio só acorda quando alguma tarefa precisar de rede */
}

/* Mostra o status da rede. */
static void show_wifi_status(void)
{
    char text[200];
    network_status(text, sizeof(text));
    ui_details("WI-FI", text);
}

static const submenu_item_t ITEMS_WIFI[] = {
    {"on_off", "Liga/Desliga", info_wifi, toggle_wifi},
    {"info", "Status", NULL, show_wifi_status},
    {"search", "Buscar", NULL, scan_wifi},
    {"save", "Redes salvas", NULL, open_saved},
};
static const submenu_t MENU_WIFI = {"WI-FI", ITEMS_WIFI, 4};

/* ========================================================================= */
/* Bluetooth (rádio presente; protocolo de troca ainda não especificado)     */
/* ========================================================================= */

static void info_bt(char *b, int t) { snprintf(b, (size_t)t, "%s", g_cfg.bt_on ? "ON" : "OFF"); }

/* Bluetooth ainda não implementado. */
static void bt_coming_soon(void)
{
    ui_alert("BLUETOOTH", "bluetooth", "Em breve: trocas e combates locais.", "OK", NULL, NULL);
}

static const submenu_item_t ITEMS_BT[] = {
    {"on_off", "Liga/Desliga", info_bt, bt_coming_soon},
    {"search", "Buscar", NULL, bt_coming_soon},
    {"save", "Dispositivos pareados", NULL, bt_coming_soon},
};
static const submenu_t MENU_BT = {"BLUETOOTH", ITEMS_BT, 3};

/* ========================================================================= */
/* Energia                                                                   */
/* ========================================================================= */

/* Carga da bateria ou USB à direita do item. */
static void info_energy(char *b, int t)
{
    if (battery_charging()) {
        snprintf(b, (size_t)t, "USB");
    } else {
        snprintf(b, (size_t)t, "%d%%", battery_percent());
    }
}

/* Mostra carga, tensão e situação da bateria. */
static void show_power_status(void)
{
    char text[128];
    snprintf(text, sizeof(text), "Bateria: %d%%\nTensão: %d.%02d V\n%s", battery_percent(), battery_mv() / 1000,
             battery_mv() % 1000 / 10,
             !battery_present() ? "Bateria ausente" : (battery_charging() ? "Carregando pelo USB" : "Na bateria"));
    ui_details("ENERGIA", text);
}

/* Aplica o tempo de economia escolhido. */
static void power_save_chosen(int r, void *ctx)
{
    (void)ctx;
    if (r >= 0 && r < CONFIG_COUNT_POWER_SAVE) {
        g_cfg.power_save = (uint8_t)r;
        config_save();
    }
}

/* Abre a escolha do tempo de economia. */
static void choose_power_save(void)
{
    static const char *const options[] = {"1 minuto", "2 minutos", "5 minutos", "10 minutos", "Desativado"};
    ui_choice("ECONOMIA", options, CONFIG_COUNT_POWER_SAVE, power_save_chosen, NULL);
}

/* Tempo de economia à direita do item. */
static void info_power_save(char *b, int t)
{
    uint8_t m = CONFIG_POWER_SAVE_MIN[g_cfg.power_save % CONFIG_COUNT_POWER_SAVE];
    snprintf(b, (size_t)t, m ? "%dm" : "OFF", m);
}

static const submenu_item_t ITEMS_ENERGY[] = {
    {"info", "Status", NULL, show_power_status},
    {"plant", "Economia", info_power_save, choose_power_save},
};
static const submenu_t MENU_ENERGY = {"ENERGIA", ITEMS_ENERGY, 2};

/* ========================================================================= */
/* Armazenamento                                                             */
/* ========================================================================= */

static void info_storage(char *b, int t) { snprintf(b, (size_t)t, "%s", sd_mounted() ? "OK" : "--"); }

/* Mostra VROM, catálogo, gravações e memória livre. */
static void show_internal_memory(void)
{
    char text[200];
    snprintf(text, sizeof(text),
             "VROM: %d bitmaps\nCatálogo: %d itens, %d equip., %d habil.\nSaves gravados: %lu\nHeap livre: %u KB",
             vrom_count(), catalog_record_count(CAT_ITEMS), catalog_record_count(CAT_EQUIPMENT),
             catalog_record_count(CAT_SKILLS), (unsigned long)persist_counter(),
             (unsigned)(esp_get_free_heap_size() / 1024));
    ui_details("MEMORIA INTERNA", text);
}

/* Mostra capacidade e espaço livre do cartão. */
static void show_sd_card_info(void)
{
    if (!sd_mounted() && !sd_mount()) {
        ui_alert("MICRO SD", "sd_card", "Cartão não encontrado.", "OK", NULL, NULL);
        return;
    }
    uint64_t total = 0, free_bytes = 0;
    sd_get_space(&total, &free_bytes);
    char text[96];
    snprintf(text, sizeof(text), "Total: %lu MB\nLivre: %lu MB", (unsigned long)(total / (1024 * 1024)),
             (unsigned long)(free_bytes / (1024 * 1024)));
    ui_details("MICRO SD", text);
}

static const submenu_item_t ITEMS_STORAGE[] = {
    {"chip", "Memória interna", NULL, show_internal_memory},
    {"sd_card", "Micro SD", info_storage, show_sd_card_info},
};
static const submenu_t MENU_STORAGE = {"ARMAZENAMENTO", ITEMS_STORAGE, 2};

/* ========================================================================= */
/* Controle: teste dos botões (segure B para sair)                           */
/* ========================================================================= */

static uint32_t b_pressed_at;

/* Bipa a cada tecla; segurar B por 1 s sai. */
static void controller_key(const key_event_t *ev)
{
    if (ev->on_key == KEY_B) {
        b_pressed_at = ev->type == KEY_PRESSED ? app_ms() : 0;
    }
    if (ev->type == KEY_PRESSED) {
        audio_beep();
    }
}

/* Desenha um botão aceso (pressionado) ou apagado. */
static void button(gfx_t *g, int x, int y, int r, bool lit, const char *label)
{
    if (lit) {
        gfx_circle_filled(g, x, y, r, GFX_WHITE);
    } else {
        gfx_circle(g, x, y, r, GFX_WHITE);
    }
    if (label) {
        gfx_text(g, x - 2, y - 3, label, lit ? GFX_BLACK : GFX_WHITE);
    }
}

/* Desenha o direcional e os botões A e B com o estado atual. */
static void controller_draw(gfx_t *g, uint32_t now)
{
    ui_title(g, "CONTROLE", now);
    button(g, 30, 24, 5, buttons_is_held(KEY_UP), NULL);
    button(g, 30, 46, 5, buttons_is_held(KEY_DOWN), NULL);
    button(g, 19, 35, 5, buttons_is_held(KEY_LEFT), NULL);
    button(g, 41, 35, 5, buttons_is_held(KEY_RIGHT), NULL);
    button(g, 84, 40, 7, buttons_is_held(KEY_B), "B");
    button(g, 104, 30, 7, buttons_is_held(KEY_A), "A");
    gfx_text(g, 62, 54, "Segure B: sair", GFX_WHITE);
    if (b_pressed_at && now - b_pressed_at > 1000) {
        b_pressed_at = 0;
        ui_back();
    }
}

static const screen_t SCREEN_CONTROLLER = {
    .name = "controle",
    .on_key = controller_key,
    .on_draw = controller_draw,
};

/* Abre o teste de botões. */
static void open_controller(void)
{
    b_pressed_at = 0;
    ui_open(&SCREEN_CONTROLLER, NULL);
}

/* ========================================================================= */
/* Som                                                                       */
/* ========================================================================= */

static void info_mute(char *b, int t) { snprintf(b, (size_t)t, "%s", g_cfg.mute ? "MUDO" : ""); }

/* Alterna o modo silencioso. */
static void toggle_mute(void)
{
    g_cfg.mute = !g_cfg.mute;
    audio_mute(g_cfg.mute);
    config_save();
}

/* Toca uma escala para testar o buzzer. */
static void test_sound(void)
{
    static const note_t scale[] = {{523, 150}, {587, 150}, {659, 150}, {698, 150},
                                    {784, 150}, {880, 150}, {988, 150}, {1047, 300}};
    audio_effect(scale, 8, false);
}

static const submenu_item_t ITEMS_SOUND[] = {
    {"mute", "Mudo", info_mute, toggle_mute},
    {"boom_box", "Teste de som", NULL, test_sound},
};
static const submenu_t MENU_SOUND = {"SOM", ITEMS_SOUND, 2};

/* ========================================================================= */
/* Sistema e atualização                                                     */
/* ========================================================================= */

/* Mostra versão, compilação, ESP-IDF e memória. */
static void show_system_status(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    char text[220];
    snprintf(text, sizeof(text), "Guará v%s\nCompilado: %s %s\nESP-IDF: %s\nHeap livre: %u KB\nMaior bloco: %u KB",
             app_version(), app->date, app->time, app->idf_ver, (unsigned)(esp_get_free_heap_size() / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
    ui_details("SISTEMA", text);
}

/* Tela de progresso da atualização. */
static void progress_ota(int pct)
{
    static gfx_t g;
    gfx_init(&g);
    gfx_text_center(&g, 0, GFX_WIDTH, 20, "Atualizando...", GFX_WHITE);
    ui_progress(&g, 14, 34, 100, 8, pct, 100);
    display_send(&g);
}

/* Reinicia o aparelho. */
static void restart_now(int r, void *ctx)
{
    (void)r;
    (void)ctx;
    esp_restart();
}

/* Aplica firmware.bin e/ou storage.bin de /update no cartão. */
static void update_sd_confirmed(int r, void *ctx)
{
    (void)ctx;
    if (r != 1) {
        return;
    }
    char error[48] = "";
    FILE *f = fopen(OTA_STORAGE_SD, "rb");
    bool has_storage = f != NULL;
    if (f) {
        fclose(f);
    }
    f = fopen(OTA_FIRMWARE_SD, "rb");
    bool has_firmware = f != NULL;
    if (f) {
        fclose(f);
    }
    if (!has_storage && !has_firmware) {
        ui_alert("ATUALIZAR", "sd_card", "Coloque firmware.bin ou storage.bin em /update", "OK", NULL, NULL);
        return;
    }
    app_save();
    bool ok = true;
    if (has_firmware) {
        ok = ota_firmware_from_sd(OTA_FIRMWARE_SD, progress_ota, error, sizeof(error));
    }
    if (ok && has_storage) {
        ok = ota_storage_from_sd(OTA_STORAGE_SD, progress_ota, error, sizeof(error));
    }
    if (ok) {
        ui_alert("ATUALIZAR", "info", "Concluído. Reiniciando.", "OK", restart_now, NULL);
    } else {
        audio_error();
        ui_alert("ATUALIZAR", "info", error, "OK", NULL, NULL);
    }
}

/* Pede confirmação para atualizar pelo cartão SD. */
static void update_sd(void)
{
    if (!sd_mounted() && !sd_mount()) {
        ui_alert("ATUALIZAR", "sd_card", "Cartão não encontrado.", "OK", NULL, NULL);
        return;
    }
    ui_confirm("ATUALIZAR", "sd_card", "Atualizar pelo cartão SD?", "NAO", "SIM", update_sd_confirmed, NULL);
}

/* Atualização pelo GitHub (ainda não implementada). */
static void update_github(void)
{
    /* README: TODO "falta implementar a atualização pelo github" */
    ui_alert("GITHUB", "github", "Em breve: atualização pelo GitHub.", "OK", NULL, NULL);
}

static const submenu_item_t ITEMS_UPDATE[] = {
    {"github", "GitHub", NULL, update_github},
    {"sd_card", "Micro SD", NULL, update_sd},
};
static const submenu_t MENU_UPDATE = {"ATUALIZAR", ITEMS_UPDATE, 2};
static void open_update(void) { submenu_open(&MENU_UPDATE); }

static const submenu_item_t ITEMS_SYSTEM[] = {
    {"info", "Status", NULL, show_system_status},
    {"satellite", "Atualizar", NULL, open_update},
};
static const submenu_t MENU_SYSTEM = {"SISTEMA", ITEMS_SYSTEM, 2};

/* ========================================================================= */
/* Reiniciar (Factory Rebirth) com dupla confirmação                         */
/* ========================================================================= */

/* Segunda confirmação: apaga o Guará. */
static void rebirth_2(int r, void *ctx)
{
    (void)ctx;
    if (r == 1) {
        app_rebirth();
    }
}

/* Primeira confirmação: pede a segunda. */
static void rebirth_1(int r, void *ctx)
{
    (void)ctx;
    if (r == 1) {
        ui_confirm("ATENCAO", "kill", "Perda irreversível! Tem certeza?", "NAO", "APAGAR", rebirth_2, NULL);
    }
}

/* Factory Rebirth com dupla confirmação. */
static void restart(void)
{
    ui_confirm("REINICIAR", "kill", "Apagar o Guará e começar de novo?", "NAO", "SIM", rebirth_1, NULL);
}

/* ========================================================================= */

static void open_wifi(void) { submenu_open(&MENU_WIFI); }
static void open_bt(void) { submenu_open(&MENU_BT); }
static void open_energy(void) { submenu_open(&MENU_ENERGY); }
static void open_storage(void) { submenu_open(&MENU_STORAGE); }
static void open_sound(void) { submenu_open(&MENU_SOUND); }
static void open_system(void) { submenu_open(&MENU_SYSTEM); }

static const submenu_item_t ITEMS_SETTINGS[] = {
    {"wifi", "Wi-Fi", info_wifi, open_wifi},
    {"bluetooth", "Bluetooth", info_bt, open_bt},
    {"battery", "Energia", info_energy, open_energy},
    {"box", "Armazenamento", info_storage, open_storage},
    {"joystick", "Controle", NULL, open_controller},
    {"sound", "Som", info_mute, open_sound},
    {"info", "Sistema", NULL, open_system},
    {"kill", "Reiniciar", NULL, restart},
};
static const submenu_t MENU_SETTINGS = {"AJUSTES", ITEMS_SETTINGS, 8};

/* Abre o sub-menu Ajustes. */
void settings_open_menu(void)
{
    submenu_open(&MENU_SETTINGS);
}
