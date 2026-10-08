#include <stdio.h>
#include <string.h>

#include "app.h"
#include "audio.h"
#include "bateria.h"
#include "componentes.h"
#include "config.h"
#include "display.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modal.h"
#include "ota.h"
#include "persist.h"
#include "relogio.h"
#include "sdcard.h"
#include "submenu.h"
#include "telas.h"
#include "wifi.h"

/* ========================================================================= */
/* Wi-Fi                                                                     */
/* ========================================================================= */

static rede_wifi_t redes[WIFI_MAX_REDES];
static ui_lista_t lista_redes;
static char ssid_escolhido[33];

static void tela_ocupado(const char *texto)
{
    /* Feedback imediato para operações bloqueantes (busca, conexão) */
    static gfx_t g;
    gfx_iniciar(&g);
    ui_icone(&g, "wifi", 56, 14);
    gfx_texto_centro(&g, 0, GFX_LARGURA, 38, texto, GFX_BRANCO);
    display_enviar(&g);
}

static void conectar(const char *ssid, const char *senha)
{
    tela_ocupado("Conectando...");
    bool ok = wifi_conectar(ssid, senha, 10000);
    if (ok) {
        config_salvar_rede(ssid, senha);
        g_cfg.wifi_ligado = true;
        config_salvar();
        relogio_sincronizar_ntp(5000);
        ui_alerta("WI-FI", "wifi", "Conectado!", "OK", NULL, NULL);
    } else {
        audio_erro();
        ui_alerta("WI-FI", "wifi", "Não foi possível conectar.", "OK", NULL, NULL);
    }
}

static void senha_digitada(const char *senha, void *ctx)
{
    (void)ctx;
    if (senha) {
        conectar(ssid_escolhido, senha);
    }
}

static void item_rede(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    it->icone = "wifi";
    snprintf(it->texto, sizeof(it->texto), "%s", redes[i].ssid);
    snprintf(it->info, sizeof(it->info), "%d", redes[i].rssi);
}

static void busca_tecla(const evento_tecla_t *ev)
{
    if (ui_lista_tecla(&lista_redes, ev, UI_LINHAS_VISIVEIS) || ev->tipo != TECLA_PRESSIONOU) {
        return;
    }
    if (ev->tecla == TECLA_B) {
        ui_voltar();
    } else if (ev->tecla == TECLA_A && lista_redes.total) {
        snprintf(ssid_escolhido, sizeof(ssid_escolhido), "%s", redes[lista_redes.sel].ssid);
        if (redes[lista_redes.sel].aberta) {
            conectar(ssid_escolhido, "");
        } else {
            ui_teclado("Senha", "", 63, senha_digitada, NULL);
        }
    }
}

static void busca_desenhar(gfx_t *g, uint32_t agora)
{
    ui_titulo(g, "REDES", agora);
    if (lista_redes.total == 0) {
        gfx_texto_centro(g, 0, GFX_LARGURA, 32, "Nenhuma rede", GFX_BRANCO);
        return;
    }
    ui_lista_desenhar(g, &lista_redes, UI_TITULO_ALTURA, UI_LINHAS_VISIVEIS, item_rede, NULL, agora);
}

static const tela_t TELA_BUSCA_WIFI = {
    .nome = "busca_wifi",
    .tecla = busca_tecla,
    .desenhar = busca_desenhar,
};

static void buscar_wifi(void)
{
    tela_ocupado("Buscando redes...");
    lista_redes = (ui_lista_t){0};
    lista_redes.total = wifi_buscar(redes, WIFI_MAX_REDES);
    ui_abrir(&TELA_BUSCA_WIFI, NULL);
}

static rede_salva_t salvas[CONFIG_MAX_REDES];
static ui_lista_t lista_salvas;

static void item_salva(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    it->icone = "save";
    snprintf(it->texto, sizeof(it->texto), "%s", salvas[i].ssid);
}

static void salva_opcao(int r, void *ctx)
{
    (void)ctx;
    if (r == 0) {
        conectar(salvas[lista_salvas.sel].ssid, salvas[lista_salvas.sel].senha);
    } else if (r == 1) {
        config_esquecer_rede(lista_salvas.sel);
        lista_salvas.total = config_redes(salvas);
    }
}

static void salvas_tecla(const evento_tecla_t *ev)
{
    if (ui_lista_tecla(&lista_salvas, ev, UI_LINHAS_VISIVEIS) || ev->tipo != TECLA_PRESSIONOU) {
        return;
    }
    if (ev->tecla == TECLA_B) {
        ui_voltar();
    } else if (ev->tecla == TECLA_A && lista_salvas.total) {
        static const char *const opcoes[] = {"Conectar", "Esquecer"};
        ui_escolha(salvas[lista_salvas.sel].ssid, opcoes, 2, salva_opcao, NULL);
    }
}

static void salvas_desenhar(gfx_t *g, uint32_t agora)
{
    ui_titulo(g, "REDES SALVAS", agora);
    if (lista_salvas.total == 0) {
        gfx_texto_centro(g, 0, GFX_LARGURA, 32, "Nenhuma rede salva", GFX_BRANCO);
        return;
    }
    ui_lista_desenhar(g, &lista_salvas, UI_TITULO_ALTURA, UI_LINHAS_VISIVEIS, item_salva, NULL, agora);
}

static const tela_t TELA_REDES_SALVAS = {
    .nome = "redes_salvas",
    .tecla = salvas_tecla,
    .desenhar = salvas_desenhar,
};

static void abrir_salvas(void)
{
    lista_salvas = (ui_lista_t){0};
    lista_salvas.total = config_redes(salvas);
    ui_abrir(&TELA_REDES_SALVAS, NULL);
}

static void info_wifi(char *b, int t) { snprintf(b, (size_t)t, "%s", g_cfg.wifi_ligado ? "ON" : "OFF"); }

static void alternar_wifi(void)
{
    g_cfg.wifi_ligado = !g_cfg.wifi_ligado;
    config_salvar();
    if (g_cfg.wifi_ligado) {
        tela_ocupado("Conectando...");
        if (!app_conectar_wifi_salvo(8000)) {
            wifi_ligar();
        }
    } else {
        wifi_desligar();
    }
}

static void status_wifi(void)
{
    char ip[20], texto[160];
    wifi_ip(ip, sizeof(ip));
    snprintf(texto, sizeof(texto), "Rádio: %s\nRede: %s\nIP: %s\nSinal: %d dBm", wifi_ligado() ? "ligado" : "desligado",
             wifi_conectado() ? wifi_ssid_atual() : "-", ip, wifi_conectado() ? wifi_rssi() : 0);
    ui_detalhes("WI-FI", texto);
}

static const submenu_item_t ITENS_WIFI[] = {
    {"on_off", "Liga/Desliga", info_wifi, alternar_wifi},
    {"info", "Status", NULL, status_wifi},
    {"search", "Buscar", NULL, buscar_wifi},
    {"save", "Redes salvas", NULL, abrir_salvas},
};
static const submenu_t MENU_WIFI = {"WI-FI", ITENS_WIFI, 4};

/* ========================================================================= */
/* Bluetooth (rádio presente; protocolo de troca ainda não especificado)     */
/* ========================================================================= */

static void info_bt(char *b, int t) { snprintf(b, (size_t)t, "%s", g_cfg.bt_ligado ? "ON" : "OFF"); }

static void bt_em_breve(void)
{
    ui_alerta("BLUETOOTH", "bluetooth", "Em breve: trocas e combates locais.", "OK", NULL, NULL);
}

static const submenu_item_t ITENS_BT[] = {
    {"on_off", "Liga/Desliga", info_bt, bt_em_breve},
    {"search", "Buscar", NULL, bt_em_breve},
    {"save", "Dispositivos pareados", NULL, bt_em_breve},
};
static const submenu_t MENU_BT = {"BLUETOOTH", ITENS_BT, 3};

/* ========================================================================= */
/* Energia                                                                   */
/* ========================================================================= */

static void info_energia(char *b, int t)
{
    if (bateria_carregando()) {
        snprintf(b, (size_t)t, "USB");
    } else {
        snprintf(b, (size_t)t, "%d%%", bateria_porcento());
    }
}

static void status_energia(void)
{
    char texto[128];
    snprintf(texto, sizeof(texto), "Bateria: %d%%\nTensão: %d.%02d V\n%s", bateria_porcento(), bateria_mv() / 1000,
             bateria_mv() % 1000 / 10,
             !bateria_presente() ? "Bateria ausente" : (bateria_carregando() ? "Carregando pelo USB" : "Na bateria"));
    ui_detalhes("ENERGIA", texto);
}

static void economia_escolhida(int r, void *ctx)
{
    (void)ctx;
    if (r >= 0 && r < CONFIG_QTD_ECONOMIA) {
        g_cfg.economia = (uint8_t)r;
        config_salvar();
    }
}

static void escolher_economia(void)
{
    static const char *const opcoes[] = {"1 minuto", "2 minutos", "5 minutos", "10 minutos", "Desativado"};
    ui_escolha("ECONOMIA", opcoes, CONFIG_QTD_ECONOMIA, economia_escolhida, NULL);
}

static void info_economia(char *b, int t)
{
    uint8_t m = CONFIG_ECONOMIA_MIN[g_cfg.economia % CONFIG_QTD_ECONOMIA];
    snprintf(b, (size_t)t, m ? "%dm" : "OFF", m);
}

static const submenu_item_t ITENS_ENERGIA[] = {
    {"info", "Status", NULL, status_energia},
    {"plant", "Economia", info_economia, escolher_economia},
};
static const submenu_t MENU_ENERGIA = {"ENERGIA", ITENS_ENERGIA, 2};

/* ========================================================================= */
/* Armazenamento                                                             */
/* ========================================================================= */

static void info_armazenamento(char *b, int t) { snprintf(b, (size_t)t, "%s", sd_montado() ? "OK" : "--"); }

static void memoria_interna(void)
{
    char texto[200];
    snprintf(texto, sizeof(texto),
             "VROM: %d bitmaps\nCatálogo: %d itens, %d equip., %d habil.\nSaves gravados: %lu\nHeap livre: %u KB",
             vrom_quantidade(), catalogo_quantidade(CAT_ITENS), catalogo_quantidade(CAT_EQUIPAMENTOS),
             catalogo_quantidade(CAT_HABILIDADES), (unsigned long)persist_contador(),
             (unsigned)(esp_get_free_heap_size() / 1024));
    ui_detalhes("MEMORIA INTERNA", texto);
}

static void micro_sd(void)
{
    if (!sd_montado() && !sd_montar()) {
        ui_alerta("MICRO SD", "sd_card", "Cartão não encontrado.", "OK", NULL, NULL);
        return;
    }
    uint64_t total = 0, livre = 0;
    sd_espaco(&total, &livre);
    char texto[96];
    snprintf(texto, sizeof(texto), "Total: %lu MB\nLivre: %lu MB", (unsigned long)(total / (1024 * 1024)),
             (unsigned long)(livre / (1024 * 1024)));
    ui_detalhes("MICRO SD", texto);
}

static const submenu_item_t ITENS_ARMAZ[] = {
    {"chip", "Memória interna", NULL, memoria_interna},
    {"sd_card", "Micro SD", info_armazenamento, micro_sd},
};
static const submenu_t MENU_ARMAZ = {"ARMAZENAMENTO", ITENS_ARMAZ, 2};

/* ========================================================================= */
/* Controle: teste dos botões (segure B para sair)                           */
/* ========================================================================= */

static uint32_t b_desde;

static void controle_tecla(const evento_tecla_t *ev)
{
    if (ev->tecla == TECLA_B) {
        b_desde = ev->tipo == TECLA_PRESSIONOU ? app_ms() : 0;
    }
    if (ev->tipo == TECLA_PRESSIONOU) {
        audio_bip();
    }
}

static void botao(gfx_t *g, int x, int y, int r, bool aceso, const char *rotulo)
{
    if (aceso) {
        gfx_circulo_cheio(g, x, y, r, GFX_BRANCO);
    } else {
        gfx_circulo(g, x, y, r, GFX_BRANCO);
    }
    if (rotulo) {
        gfx_texto(g, x - 2, y - 3, rotulo, aceso ? GFX_PRETO : GFX_BRANCO);
    }
}

static void controle_desenhar(gfx_t *g, uint32_t agora)
{
    ui_titulo(g, "CONTROLE", agora);
    botao(g, 30, 24, 5, botoes_segurando(TECLA_CIMA), NULL);
    botao(g, 30, 46, 5, botoes_segurando(TECLA_BAIXO), NULL);
    botao(g, 19, 35, 5, botoes_segurando(TECLA_ESQ), NULL);
    botao(g, 41, 35, 5, botoes_segurando(TECLA_DIR), NULL);
    botao(g, 84, 40, 7, botoes_segurando(TECLA_B), "B");
    botao(g, 104, 30, 7, botoes_segurando(TECLA_A), "A");
    gfx_texto(g, 62, 54, "Segure B: sair", GFX_BRANCO);
    if (b_desde && agora - b_desde > 1000) {
        b_desde = 0;
        ui_voltar();
    }
}

static const tela_t TELA_CONTROLE = {
    .nome = "controle",
    .tecla = controle_tecla,
    .desenhar = controle_desenhar,
};

static void abrir_controle(void)
{
    b_desde = 0;
    ui_abrir(&TELA_CONTROLE, NULL);
}

/* ========================================================================= */
/* Som                                                                       */
/* ========================================================================= */

static void info_mudo(char *b, int t) { snprintf(b, (size_t)t, "%s", g_cfg.mudo ? "MUDO" : ""); }

static void alternar_mudo(void)
{
    g_cfg.mudo = !g_cfg.mudo;
    audio_mudo(g_cfg.mudo);
    config_salvar();
}

static void teste_som(void)
{
    static const nota_t escala[] = {{523, 150}, {587, 150}, {659, 150}, {698, 150},
                                    {784, 150}, {880, 150}, {988, 150}, {1047, 300}};
    audio_efeito(escala, 8, false);
}

static const submenu_item_t ITENS_SOM[] = {
    {"mute", "Mudo", info_mudo, alternar_mudo},
    {"boom_box", "Teste de som", NULL, teste_som},
};
static const submenu_t MENU_SOM = {"SOM", ITENS_SOM, 2};

/* ========================================================================= */
/* Sistema e atualização                                                     */
/* ========================================================================= */

static void status_sistema(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    char texto[220];
    snprintf(texto, sizeof(texto), "Guará v%s\nCompilado: %s %s\nESP-IDF: %s\nHeap livre: %u KB\nMaior bloco: %u KB",
             app_versao(), app->date, app->time, app->idf_ver, (unsigned)(esp_get_free_heap_size() / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
    ui_detalhes("SISTEMA", texto);
}

static void progresso_ota(int pct)
{
    static gfx_t g;
    gfx_iniciar(&g);
    gfx_texto_centro(&g, 0, GFX_LARGURA, 20, "Atualizando...", GFX_BRANCO);
    ui_progresso(&g, 14, 34, 100, 8, pct, 100);
    display_enviar(&g);
}

static void reiniciar_agora(int r, void *ctx)
{
    (void)r;
    (void)ctx;
    esp_restart();
}

static void atualizar_sd_confirmado(int r, void *ctx)
{
    (void)ctx;
    if (r != 1) {
        return;
    }
    char erro[48] = "";
    FILE *f = fopen(OTA_STORAGE_SD, "rb");
    bool tem_storage = f != NULL;
    if (f) {
        fclose(f);
    }
    f = fopen(OTA_FIRMWARE_SD, "rb");
    bool tem_firmware = f != NULL;
    if (f) {
        fclose(f);
    }
    if (!tem_storage && !tem_firmware) {
        ui_alerta("ATUALIZAR", "sd_card", "Coloque firmware.bin ou storage.bin em /update", "OK", NULL, NULL);
        return;
    }
    app_salvar();
    bool ok = true;
    if (tem_firmware) {
        ok = ota_firmware_do_sd(OTA_FIRMWARE_SD, progresso_ota, erro, sizeof(erro));
    }
    if (ok && tem_storage) {
        ok = ota_storage_do_sd(OTA_STORAGE_SD, progresso_ota, erro, sizeof(erro));
    }
    if (ok) {
        ui_alerta("ATUALIZAR", "info", "Concluído. Reiniciando.", "OK", reiniciar_agora, NULL);
    } else {
        audio_erro();
        ui_alerta("ATUALIZAR", "info", erro, "OK", NULL, NULL);
    }
}

static void atualizar_sd(void)
{
    if (!sd_montado() && !sd_montar()) {
        ui_alerta("ATUALIZAR", "sd_card", "Cartão não encontrado.", "OK", NULL, NULL);
        return;
    }
    ui_confirmar("ATUALIZAR", "sd_card", "Atualizar pelo cartão SD?", "NAO", "SIM", atualizar_sd_confirmado, NULL);
}

static void atualizar_github(void)
{
    /* README: TODO "falta implementar a atualização pelo github" */
    ui_alerta("GITHUB", "github", "Em breve: atualização pelo GitHub.", "OK", NULL, NULL);
}

static const submenu_item_t ITENS_ATUALIZAR[] = {
    {"github", "GitHub", NULL, atualizar_github},
    {"sd_card", "Micro SD", NULL, atualizar_sd},
};
static const submenu_t MENU_ATUALIZAR = {"ATUALIZAR", ITENS_ATUALIZAR, 2};
static void abrir_atualizar(void) { submenu_abrir(&MENU_ATUALIZAR); }

static const submenu_item_t ITENS_SISTEMA[] = {
    {"info", "Status", NULL, status_sistema},
    {"satellite", "Atualizar", NULL, abrir_atualizar},
};
static const submenu_t MENU_SISTEMA = {"SISTEMA", ITENS_SISTEMA, 2};

/* ========================================================================= */
/* Reiniciar (Factory Rebirth) com dupla confirmação                         */
/* ========================================================================= */

static void renascer_2(int r, void *ctx)
{
    (void)ctx;
    if (r == 1) {
        app_renascer();
    }
}

static void renascer_1(int r, void *ctx)
{
    (void)ctx;
    if (r == 1) {
        ui_confirmar("ATENCAO", "kill", "Perda irreversível! Tem certeza?", "NAO", "APAGAR", renascer_2, NULL);
    }
}

static void reiniciar(void)
{
    ui_confirmar("REINICIAR", "kill", "Apagar o Guará e começar de novo?", "NAO", "SIM", renascer_1, NULL);
}

/* ========================================================================= */

static void abrir_wifi(void) { submenu_abrir(&MENU_WIFI); }
static void abrir_bt(void) { submenu_abrir(&MENU_BT); }
static void abrir_energia(void) { submenu_abrir(&MENU_ENERGIA); }
static void abrir_armaz(void) { submenu_abrir(&MENU_ARMAZ); }
static void abrir_som(void) { submenu_abrir(&MENU_SOM); }
static void abrir_sistema(void) { submenu_abrir(&MENU_SISTEMA); }

static const submenu_item_t ITENS_AJUSTES[] = {
    {"wifi", "Wi-Fi", info_wifi, abrir_wifi},
    {"bluetooth", "Bluetooth", info_bt, abrir_bt},
    {"battery", "Energia", info_energia, abrir_energia},
    {"box", "Armazenamento", info_armazenamento, abrir_armaz},
    {"joystick", "Controle", NULL, abrir_controle},
    {"sound", "Som", info_mudo, abrir_som},
    {"info", "Sistema", NULL, abrir_sistema},
    {"kill", "Reiniciar", NULL, reiniciar},
};
static const submenu_t MENU_AJUSTES = {"AJUSTES", ITENS_AJUSTES, 8};

void ajustes_abrir_menu(void)
{
    submenu_abrir(&MENU_AJUSTES);
}
