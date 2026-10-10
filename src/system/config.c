/*
 * Configurações do sistema e redes Wi-Fi salvas, persistidas na NVS.
 */
#include "config.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "config";
#define NAMESPACE "guara"
#define VERSION_CONFIG 1

const uint8_t CONFIG_POWER_SAVE_MIN[CONFIG_COUNT_POWER_SAVE] = {1, 2, 5, 10, 0};

config_t g_cfg;

/* `reserved` ocupa o byte de alinhamento: o blob gravado pelas versões anteriores continua válido */
_Static_assert(sizeof(config_t) == 42, "config_t mudou de tamanho: suba VERSION_CONFIG ou migre o blob");

/* Valores padrão das configurações. */
static void fallback(config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->volume = 3;
    c->power_save = 2; /* 5 minutos */
    c->bedtime_minute = 20 * 60;
    c->wake_minute = 8 * 60;
    snprintf(c->timezone, sizeof(c->timezone), "<-03>3");
}

/* Inicializa a NVS (apaga e recria se estiver inválida). */
static void init_nvs(void)
{
    static bool ready;
    if (ready) {
        return;
    }
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS inválida, apagando");
        nvs_flash_erase();
        nvs_flash_init();
    }
    ready = true;
}

/* Carrega as configurações (ou os padrões, se não houver ou a versão mudou). */
void config_load(void)
{
    init_nvs();
    fallback(&g_cfg);
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t version = 0;
    size_t length = sizeof(g_cfg);
    config_t bytes_read;
    if (nvs_get_u8(h, "versao", &version) == ESP_OK && version == VERSION_CONFIG &&
        nvs_get_blob(h, "cfg", &bytes_read, &length) == ESP_OK && length == sizeof(bytes_read)) {
        g_cfg = bytes_read;
        g_cfg.reserved = 0; /* a v0.18.0 guardava o Visível aqui */
    }
    nvs_close(h);
}

/* Grava as configurações. */
void config_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, "versao", VERSION_CONFIG);
    nvs_set_blob(h, "cfg", &g_cfg, sizeof(g_cfg));
    nvs_commit(h);
    nvs_close(h);
}

/* Lê a lista de redes salvas. Retorna quantas existem. */
static int read_networks(saved_network_t networks[CONFIG_MAX_NETWORKS])
{
    memset(networks, 0, sizeof(saved_network_t) * CONFIG_MAX_NETWORKS);
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return 0;
    }
    size_t length = sizeof(saved_network_t) * CONFIG_MAX_NETWORKS;
    if (nvs_get_blob(h, "redes", networks, &length) != ESP_OK) {
        memset(networks, 0, sizeof(saved_network_t) * CONFIG_MAX_NETWORKS);
    }
    nvs_close(h);
    int n = 0;
    while (n < CONFIG_MAX_NETWORKS && networks[n].ssid[0]) {
        n++;
    }
    return n;
}

/* Grava a lista de redes salvas. */
static void write_networks(const saved_network_t networks[CONFIG_MAX_NETWORKS])
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_blob(h, "redes", networks, sizeof(saved_network_t) * CONFIG_MAX_NETWORKS);
    nvs_commit(h);
    nvs_close(h);
}

/* Redes salvas, da mais antiga para a mais recente. */
int config_networks(saved_network_t *networks)
{
    return read_networks(networks);
}

/* Salva ou atualiza uma rede (descarta a mais antiga se a lista estiver cheia). */
void config_save_network(const char *ssid, const char *password)
{
    saved_network_t networks[CONFIG_MAX_NETWORKS];
    int n = read_networks(networks);
    int pos = n;
    for (int i = 0; i < n; i++) {
        if (strcmp(networks[i].ssid, ssid) == 0) {
            pos = i;
        }
    }
    if (pos == CONFIG_MAX_NETWORKS) {
        /* cheio: descarta a mais antiga */
        memmove(&networks[0], &networks[1], sizeof(saved_network_t) * (CONFIG_MAX_NETWORKS - 1));
        pos = CONFIG_MAX_NETWORKS - 1;
    }
    snprintf(networks[pos].ssid, sizeof(networks[pos].ssid), "%s", ssid);
    snprintf(networks[pos].password, sizeof(networks[pos].password), "%s", password ? password : "");
    write_networks(networks);
}

/* Remove uma rede salva. */
void config_forget_network(int position)
{
    saved_network_t networks[CONFIG_MAX_NETWORKS];
    int n = read_networks(networks);
    if (position < 0 || position >= n) {
        return;
    }
    memmove(&networks[position], &networks[position + 1], sizeof(saved_network_t) * (size_t)(CONFIG_MAX_NETWORKS - 1 - position));
    memset(&networks[CONFIG_MAX_NETWORKS - 1], 0, sizeof(saved_network_t));
    write_networks(networks);
}

/* Lê o BSSID e o canal do último roteador. */
bool config_load_fast_reconnect(fast_reconnect_t *r)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t length = sizeof(*r);
    bool ok = nvs_get_blob(h, "rapida", r, &length) == ESP_OK && length == sizeof(*r) && r->ssid[0];
    nvs_close(h);
    return ok;
}

/* Grava o último roteador (só se mudou, para poupar a NVS). */
void config_save_fast_reconnect(const fast_reconnect_t *r)
{
    fast_reconnect_t current;
    if (config_load_fast_reconnect(&current) && memcmp(&current, r, sizeof(current)) == 0) {
        return; /* evita gravar a NVS a cada reconexão */
    }
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_blob(h, "rapida", r, sizeof(*r));
    nvs_commit(h);
    nvs_close(h);
}

/* Lista de conhecidos da v0.18.0 na NVS: copia, apaga a chave e retorna quantos havia. */
int config_take_legacy_connections(known_t *out, int max)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return 0;
    }
    known_t legacy[KNOWN_MAX] = {0};
    size_t length = sizeof(legacy);
    int n = 0;
    if (nvs_get_blob(h, "conexoes", legacy, &length) == ESP_OK) {
        while (n < max && n < KNOWN_MAX && legacy[n].uid) {
            out[n] = legacy[n];
            n++;
        }
        nvs_erase_key(h, "conexoes");
        nvs_commit(h);
    }
    nvs_close(h);
    return n;
}
