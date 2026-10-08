#include "app.h"

#include <stdio.h>
#include <string.h>

#include "alarmes.h"
#include "audio.h"
#include "bateria.h"
#include "botoes.h"
#include "componentes.h"
#include "config.h"
#include "display.h"
#include "energia.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "persist.h"
#include "relogio.h"
#include "sdcard.h"
#include "telas.h"
#include "ui.h"
#include "wifi.h"

static const char *TAG = "app";

guara_t g_guara;
bool g_tem_guara;
rng_t g_rng;

static gfx_t tela;
static bool sono_bloqueado;
static uint32_t minutos_monotonicos;
static int criticas_seguidas;

uint32_t app_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

const char *app_versao(void)
{
    return GUARA_VERSAO;
}

void app_relogio(relogio_ctx_t *rel)
{
    rel->minuto_do_dia = relogio_minuto_do_dia();
    rel->dormir_min = g_cfg.dormir_min;
    rel->acordar_min = g_cfg.acordar_min;
}

void app_interagir(void)
{
    if (!g_tem_guara) {
        return;
    }
    relogio_ctx_t rel;
    app_relogio(&rel);
    guara_interagir(&g_guara, &rel);
}

void app_alterou(void)
{
    if (g_tem_guara) {
        persist_espelhar(&g_guara);
        persist_marcar_sujo();
    }
}

void app_salvar(void)
{
    if (g_tem_guara) {
        if (relogio_valido()) {
            g_guara.timestamp = relogio_agora();
        }
        persist_salvar(&g_guara);
    }
}

void app_bloquear_sono(bool bloquear)
{
    sono_bloqueado = bloquear;
}

int app_ganhar_xp(uint32_t pontos)
{
    uint8_t estagio = g_guara.estagio, natureza = g_guara.natureza;
    int niveis = guara_ganhar_xp(&g_guara, pontos, &g_rng);
    if (niveis > 0) {
        audio_level_up();
        app_salvar();
        if (g_guara.estagio != estagio || g_guara.natureza != natureza) {
            ESP_LOGI(TAG, "Evolução: estágio %d, natureza %s", g_guara.estagio, natureza_nome(g_guara.natureza));
        }
    } else {
        app_alterou();
    }
    return niveis;
}

void app_nascer(const char *nome)
{
    guara_nascer(&g_guara, nome, &g_rng, relogio_valido() ? relogio_agora() : 0);
    g_tem_guara = true;
    if (guara_versao_desenvolvimento(GUARA_VERSAO)) {
        guara_liberar_catalogo(&g_guara);
    }
    app_salvar();
}

void app_renascer(void)
{
    persist_apagar();
    g_tem_guara = false;
    memset(&g_guara, 0, sizeof(g_guara));
    ui_limpar_pilha(&TELA_NASCIMENTO, NULL);
}

bool app_conectar_wifi_salvo(uint32_t espera_ms)
{
    rede_salva_t redes[CONFIG_MAX_REDES];
    int n = config_redes(redes);
    for (int i = n - 1; i >= 0; i--) {
        if (wifi_conectar(redes[i].ssid, redes[i].senha, espera_ms)) {
            relogio_sincronizar_ntp(5000);
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------------- */
/* Boot                                                                      */
/* ------------------------------------------------------------------------- */

static void desenhar_boot(int progresso)
{
    gfx_limpar(&tela);
    const gfx_bitmap_t *logo = vrom("bitart_logo");
    if (logo) {
        gfx_bitmap(&tela, logo, (GFX_LARGURA - logo->largura) / 2, 6, GFX_COPIA, 0);
    } else {
        gfx_texto_centro(&tela, 0, GFX_LARGURA, 20, "bitart", GFX_BRANCO);
    }
    ui_progresso(&tela, 14, 52, 100, 6, progresso, 100);
    display_enviar(&tela);
}

static void erro_storage(storage_status_t st)
{
    gfx_limpar(&tela);
    gfx_texto(&tela, 0, 0, "STORAGE INVALIDO", GFX_BRANCO);
    char buf[24];
    snprintf(buf, sizeof(buf), "codigo %d", st);
    gfx_texto(&tela, 0, 12, buf, GFX_BRANCO);
    gfx_texto(&tela, 0, 30, "Grave com:", GFX_BRANCO);
    gfx_texto(&tela, 0, 40, "pio run -t", GFX_BRANCO);
    gfx_texto(&tela, 0, 50, "upload_storage", GFX_BRANCO);
    display_enviar(&tela);
}

void app_iniciar(void)
{
    rng_semear(&g_rng, esp_random());
    config_carregar();
    audio_iniciar();
    audio_mudo(g_cfg.mudo);
    audio_volume(g_cfg.volume);

    motivo_acordar_t motivo = energia_motivo();
    bool frio = motivo == ACORDOU_FRIO;
    gfx_iniciar(&tela);
    if (!display_iniciar()) {
        ESP_LOGE(TAG, "Sem display; seguindo sem vídeo");
    }

    storage_status_t st = persist_abrir_storage();
    if (st != STORAGE_OK) {
        erro_storage(st);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    if (frio) {
        desenhar_boot(10);
    }

    relogio_iniciar(g_cfg.fuso);
    botoes_iniciar();
    bateria_iniciar();
    if (frio) {
        desenhar_boot(30);
    }

    sd_montar();
    alarmes_carregar();
    if (frio) {
        desenhar_boot(50);
    }

    persist_iniciar();
    g_tem_guara = persist_carregar(&g_guara);
    /* Regra das versões 0.x.x: todo o catálogo liberado (itens com 255 unidades) a cada boot */
    if (g_tem_guara && guara_versao_desenvolvimento(GUARA_VERSAO)) {
        guara_liberar_catalogo(&g_guara);
        app_alterou();
    }
    if (frio) {
        desenhar_boot(65);
    }

    /* Wi-Fi configurado: atualiza a hora durante o boot (README > Tela de boot) */
    if (frio && g_cfg.wifi_ligado) {
        if (app_conectar_wifi_salvo(6000)) {
            desenhar_boot(85);
        }
    }

    if (g_tem_guara && relogio_valido()) {
        uint32_t min = guara_recuperar_tempo(&g_guara, relogio_agora(), relogio_minuto_do_dia(), g_cfg.dormir_min,
                                             g_cfg.acordar_min);
        if (min > 0) {
            ESP_LOGI(TAG, "Recuperação temporal: %lu minutos", (unsigned long)min);
            app_alterou();
        }
    }

    if (frio) {
        desenhar_boot(100);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (g_tem_guara) {
        ui_limpar_pilha(&TELA_PRINCIPAL, NULL);
    } else {
        ui_limpar_pilha(&TELA_NASCIMENTO, NULL);
    }
    if (motivo == ACORDOU_TIMER) {
        /* acordou para um despertador/temporizador: o laço trata o disparo */
        ESP_LOGI(TAG, "Acordou pelo timer");
    }
}

/* ------------------------------------------------------------------------- */
/* Laço principal                                                            */
/* ------------------------------------------------------------------------- */

static void minuto_do_guara(void)
{
    if (!g_tem_guara) {
        return;
    }
    relogio_ctx_t rel;
    app_relogio(&rel);
    uint8_t estados_antes = guara_tem(&g_guara, EST_DESMAIADO);
    guara_minuto(&g_guara, &rel, false);
    if (relogio_valido()) {
        g_guara.timestamp = relogio_agora();
    }
    persist_espelhar(&g_guara);
    if (!estados_antes && guara_tem(&g_guara, EST_DESMAIADO)) {
        app_salvar(); /* desmaio é um evento crítico */
    }
}

static void dormir_por_inatividade(void)
{
    persist_salvar_se_sujo(&g_guara);
    if (g_tem_guara) {
        persist_espelhar(&g_guara);
    }
    uint32_t acordar = alarmes_segundos_ate_proximo();
    uint32_t t = temporizador_restante();
    if (temporizador_ativo() && (acordar == 0 || t < acordar)) {
        acordar = t ? t : 1;
    }
    sd_desmontar();
    wifi_desligar();
    energia_dormir(acordar);
}

static void verificar_inatividade(uint32_t agora)
{
    uint8_t minutos = CONFIG_ECONOMIA_MIN[g_cfg.economia < CONFIG_QTD_ECONOMIA ? g_cfg.economia : 0];
    if (minutos == 0 || sono_bloqueado || audio_musica_ativa() || telas_alarme_tocando()) {
        return;
    }
    if (agora - botoes_ultimo_uso_ms() > (uint32_t)minutos * 60000u) {
        ESP_LOGI(TAG, "Inatividade: entrando em deep sleep");
        dormir_por_inatividade();
    }
}

static void verificar_bateria(void)
{
    bateria_atualizar();
    criticas_seguidas = bateria_critica() ? criticas_seguidas + 1 : 0;
    if (criticas_seguidas >= 3) {
        ESP_LOGW(TAG, "Bateria crítica (%d mV): salvamento de emergência", bateria_mv());
        app_salvar();
        gfx_limpar(&tela);
        ui_icone(&tela, "battery", 56, 14);
        gfx_texto_centro(&tela, 0, GFX_LARGURA, 40, "Bateria fraca", GFX_BRANCO);
        gfx_texto_centro(&tela, 0, GFX_LARGURA, 50, "Conecte o carregador", GFX_BRANCO);
        display_enviar(&tela);
        vTaskDelay(pdMS_TO_TICKS(3000));
        sd_desmontar();
        wifi_desligar();
        energia_dormir(0);
    }
}

void app_executar(void)
{
    uint32_t ultimo_quadro = 0, ultimo_segundo = 0, ultimo_minuto_ms = app_ms();
    for (;;) {
        uint32_t periodo = 1000u / (uint32_t)ui_fps();
        uint32_t agora = app_ms();
        uint32_t espera = agora - ultimo_quadro >= periodo ? 0 : periodo - (agora - ultimo_quadro);

        evento_tecla_t ev;
        if (botoes_ler(&ev, espera)) {
            ui_processar_tecla(&ev);
            while (botoes_ler(&ev, 0)) {
                ui_processar_tecla(&ev);
            }
        }

        agora = app_ms();
        if (agora - ultimo_quadro >= periodo) {
            ultimo_quadro = agora;
            ui_desenhar(&tela, agora);
            display_enviar(&tela);
        }

        if (agora - ultimo_segundo >= 1000) {
            ultimo_segundo = agora;
            static int contador;
            if (++contador % 5 == 0) {
                verificar_bateria();
            }
            if (alarmes_verificar(relogio_minuto_do_dia())) {
                telas_disparar_alarme("Despertador");
            }
            if (temporizador_verificar()) {
                telas_disparar_alarme("Temporizador");
            }
            verificar_inatividade(agora);
        }

        while (agora - ultimo_minuto_ms >= 60000) {
            ultimo_minuto_ms += 60000;
            minutos_monotonicos++;
            minuto_do_guara();
        }
    }
}
