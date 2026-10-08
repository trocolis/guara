#include "config.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "config";
#define NAMESPACE "guara"
#define VERSAO_CONFIG 1

const uint8_t CONFIG_ECONOMIA_MIN[CONFIG_QTD_ECONOMIA] = {1, 2, 5, 10, 0};

config_t g_cfg;

static void padrao(config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->volume = 3;
    c->economia = 2; /* 5 minutos */
    c->dormir_min = 20 * 60;
    c->acordar_min = 8 * 60;
    snprintf(c->fuso, sizeof(c->fuso), "<-03>3");
}

static void iniciar_nvs(void)
{
    static bool pronto;
    if (pronto) {
        return;
    }
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS inválida, apagando");
        nvs_flash_erase();
        nvs_flash_init();
    }
    pronto = true;
}

void config_carregar(void)
{
    iniciar_nvs();
    padrao(&g_cfg);
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t versao = 0;
    size_t tam = sizeof(g_cfg);
    config_t lido;
    if (nvs_get_u8(h, "versao", &versao) == ESP_OK && versao == VERSAO_CONFIG &&
        nvs_get_blob(h, "cfg", &lido, &tam) == ESP_OK && tam == sizeof(lido)) {
        g_cfg = lido;
    }
    nvs_close(h);
}

void config_salvar(void)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, "versao", VERSAO_CONFIG);
    nvs_set_blob(h, "cfg", &g_cfg, sizeof(g_cfg));
    nvs_commit(h);
    nvs_close(h);
}

static int ler_redes(rede_salva_t redes[CONFIG_MAX_REDES])
{
    memset(redes, 0, sizeof(rede_salva_t) * CONFIG_MAX_REDES);
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return 0;
    }
    size_t tam = sizeof(rede_salva_t) * CONFIG_MAX_REDES;
    if (nvs_get_blob(h, "redes", redes, &tam) != ESP_OK) {
        memset(redes, 0, sizeof(rede_salva_t) * CONFIG_MAX_REDES);
    }
    nvs_close(h);
    int n = 0;
    while (n < CONFIG_MAX_REDES && redes[n].ssid[0]) {
        n++;
    }
    return n;
}

static void gravar_redes(const rede_salva_t redes[CONFIG_MAX_REDES])
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_blob(h, "redes", redes, sizeof(rede_salva_t) * CONFIG_MAX_REDES);
    nvs_commit(h);
    nvs_close(h);
}

int config_redes(rede_salva_t *redes)
{
    return ler_redes(redes);
}

void config_salvar_rede(const char *ssid, const char *senha)
{
    rede_salva_t redes[CONFIG_MAX_REDES];
    int n = ler_redes(redes);
    int pos = n;
    for (int i = 0; i < n; i++) {
        if (strcmp(redes[i].ssid, ssid) == 0) {
            pos = i;
        }
    }
    if (pos == CONFIG_MAX_REDES) {
        /* cheio: descarta a mais antiga */
        memmove(&redes[0], &redes[1], sizeof(rede_salva_t) * (CONFIG_MAX_REDES - 1));
        pos = CONFIG_MAX_REDES - 1;
    }
    snprintf(redes[pos].ssid, sizeof(redes[pos].ssid), "%s", ssid);
    snprintf(redes[pos].senha, sizeof(redes[pos].senha), "%s", senha ? senha : "");
    gravar_redes(redes);
}

void config_esquecer_rede(int indice)
{
    rede_salva_t redes[CONFIG_MAX_REDES];
    int n = ler_redes(redes);
    if (indice < 0 || indice >= n) {
        return;
    }
    memmove(&redes[indice], &redes[indice + 1], sizeof(rede_salva_t) * (size_t)(CONFIG_MAX_REDES - 1 - indice));
    memset(&redes[CONFIG_MAX_REDES - 1], 0, sizeof(rede_salva_t));
    gravar_redes(redes);
}
