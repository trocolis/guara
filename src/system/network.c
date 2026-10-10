/*
 * Wi-Fi sob demanda (README > Conectividade): o rádio só liga enquanto alguém
 * usa a rede, desliga após 30 s ocioso, e o NTP e a verificação de nova
 * release no GitHub rodam em segundo plano.
 */
#include "network.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ota.h"
#include "rtc_clock.h"

static const char *TAG = "rede";

#define CONNECTION_FAST_MS 4000
#define CONNECTION_FULL_MS 8000
#define NTP_WAIT_MS 5000
#define NTP_NEW_ATTEMPT_MS (30 * 60 * 1000) /* após falha */
#define UPDATE_NEW_ATTEMPT_MS (60 * 60 * 1000) /* após falha */

typedef struct {
    network_job_fn fn;
    void *ctx;
} network_job_t;

static SemaphoreHandle_t lock;
static QueueHandle_t queue;
static volatile int users;
static volatile uint32_t last_use_ms;
static volatile bool connecting, suspended;
/* Rádio emprestado ao Conectar (ESP-NOW): a rede o pede de volta pelo `reclaim` quando precisar */
static volatile bool lent;
static network_reclaim_fn reclaim;
static volatile int bars;
static uint32_t ntp_tried_ms;
static bool ntp_tried;
static uint32_t update_tried_ms;
static bool update_tried;
/* Sobrevivem ao deep sleep: epoch da última verificação e a release achada */
static RTC_DATA_ATTR uint32_t update_checked_at;
static RTC_DATA_ATTR char update_tag[24];
static volatile bool update_found;

/* Milissegundos desde o boot. */
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/*
 * Pede o rádio de volta ao Conectar, se estiver emprestado (sem a trava: o
 * Conectar devolve chamando network_lend(false)).
 */
static void take_back(void)
{
    if (lent && reclaim) {
        reclaim();
    }
}

/*
 * Empresta (ou devolve) o rádio ao Conectar: emprestado, a rede não usa nem
 * desliga o rádio. Recusa o empréstimo se a rede estiver em uso (decidido sob a trava).
 */
bool network_lend(bool lend)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (lend && (users > 0 || connecting)) {
        xSemaphoreGive(lock);
        return false;
    }
    if (lend) {
        wifi_turn_off(); /* sai do roteador: o Conectar religa o rádio no canal dele */
        bars = 0;
    }
    lent = lend;
    xSemaphoreGive(lock);
    return true;
}

/* Quem chamar quando a rede precisar do rádio emprestado. */
void network_set_reclaim(network_reclaim_fn fn)
{
    reclaim = fn;
}

static bool ntp_due(void);
static bool update_due(void);

/*
 * Verdadeiro se a rede está em uso (alguém pediu, conectando, tarefa na fila)
 * ou vai usar em seguida (NTP ou verificação de release vencidos). O rádio
 * ligado só esperando os 30 s de folga não conta: o Conectar pode pegá-lo.
 */
bool network_busy(void)
{
    if (users > 0 || connecting || (queue && uxQueueMessagesWaiting(queue) > 0)) {
        return true;
    }
    /* Trabalho automático vencido (NTP ou verificação de release ainda não tentados): a rede vai precisar
       do rádio em seguida, então o Conectar espera, para não ligar e desligar o Wi-Fi no meio */
    uint32_t now = now_ms();
    bool ntp_pending = ntp_due() && (!ntp_tried || now - ntp_tried_ms >= NTP_NEW_ATTEMPT_MS);
    bool update_pending = update_due() && (!update_tried || now - update_tried_ms >= UPDATE_NEW_ATTEMPT_MS);
    if (!(ntp_pending || update_pending) || !network_allowed()) {
        return false;
    }
    /* Há rede salva? (lido da NVS no máximo a cada 5 s: a task do Conectar pergunta 4 vezes por segundo) */
    static uint32_t checked_at;
    static bool has_saved;
    if (checked_at == 0 || now - checked_at >= 5000) {
        static saved_network_t saved[CONFIG_MAX_NETWORKS];
        has_saved = config_networks(saved) > 0;
        checked_at = now;
    }
    return has_saved;
}

/* Verdadeiro se um jogo Lua suspendeu a rede (o Conectar também não pode usar o rádio). */
bool network_suspended(void)
{
    return suspended;
}

/* Rede permitida: Wi-Fi em ON e não suspenso por um jogo. */
bool network_allowed(void)
{
    return g_cfg.wifi_on && !suspended;
}

/* Estado da rede para o ícone da barra superior. */
network_state_t network_state(void)
{
    if (!g_cfg.wifi_on) {
        return NETWORK_DISABLED;
    }
    if (connecting) {
        return NETWORK_CONNECTING;
    }
    return wifi_connected() ? NETWORK_CONNECTED : NETWORK_IDLE;
}

/* Barras de sinal em cache (seguro para a interface). */
int network_bars(void)
{
    return bars;
}

/* Salva o BSSID e o canal do roteador atual para a reconexão rápida. */
static void remember_router(const char *ssid)
{
    fast_reconnect_t r = {0};
    if (wifi_current_ap(r.bssid, &r.channel)) {
        snprintf(r.ssid, sizeof(r.ssid), "%s", ssid);
        config_save_fast_reconnect(&r);
    }
}

/* Deve ser chamada com a trava. Tenta o último roteador (rápido) e depois as redes salvas. */
static bool ensure_connection(uint32_t timeout_ms)
{
    if (wifi_connected()) {
        return true;
    }
    if (!network_allowed()) {
        return false;
    }
    connecting = true;
    uint32_t start = now_ms();
    saved_network_t saved_networks[CONFIG_MAX_NETWORKS];
    int n = config_networks(saved_networks);
    fast_reconnect_t fast;
    bool ok = false;
    if (config_load_fast_reconnect(&fast)) {
        for (int i = 0; i < n && !ok; i++) {
            if (strcmp(saved_networks[i].ssid, fast.ssid) == 0) {
                ok = wifi_connect(saved_networks[i].ssid, saved_networks[i].password, CONNECTION_FAST_MS, fast.bssid, fast.channel);
            }
        }
    }
    for (int i = n - 1; i >= 0 && !ok && now_ms() - start < timeout_ms; i--) {
        ok = wifi_connect(saved_networks[i].ssid, saved_networks[i].password, CONNECTION_FULL_MS, NULL, 0);
        if (ok) {
            remember_router(saved_networks[i].ssid);
        }
    }
    if (!ok && users == 0) {
        wifi_turn_off();
    }
    connecting = false;
    last_use_ms = now_ms();
    if (ok) {
        ESP_LOGI(TAG, "Conectado em %lu ms", (unsigned long)(now_ms() - start));
    } else {
        ESP_LOGW(TAG, "Sem conexão (%lu ms)", (unsigned long)(now_ms() - start));
    }
    return ok;
}

/* Registra um usuário da rede (o rádio não desliga enquanto houver usuários). */
void network_use(void)
{
    take_back();
    xSemaphoreTake(lock, portMAX_DELAY);
    users++;
    last_use_ms = now_ms();
    xSemaphoreGive(lock);
}

/* Garante a conexão, esperando até `timeout_ms`. */
bool network_wait(uint32_t timeout_ms)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    bool ok = ensure_connection(timeout_ms);
    xSemaphoreGive(lock);
    return ok;
}

/* Libera o uso; o rádio desliga após o tempo ocioso. */
void network_release(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (users > 0) {
        users--;
    }
    last_use_ms = now_ms();
    xSemaphoreGive(lock);
}

/* Enfileira uma tarefa de rede para a task em segundo plano. */
bool network_run(network_job_fn fn, void *ctx)
{
    network_job_t t = {fn, ctx};
    return queue && xQueueSend(queue, &t, 0) == pdTRUE;
}

/* Busca redes ligando o rádio sob demanda. */
int network_scan(wifi_network_t *networks, int max)
{
    take_back();
    xSemaphoreTake(lock, portMAX_DELAY);
    int n = suspended ? 0 : wifi_scan(networks, max);
    last_use_ms = now_ms();
    xSemaphoreGive(lock);
    return n;
}

/* Conecta a uma rede nova, salva e sincroniza a hora. */
bool network_connect_new(const char *ssid, const char *password)
{
    take_back();
    xSemaphoreTake(lock, portMAX_DELAY);
    connecting = true;
    bool ok = !suspended && wifi_connect(ssid, password, 10000, NULL, 0);
    connecting = false;
    if (ok) {
        config_save_network(ssid, password);
        remember_router(ssid);
        clock_sync_ntp(NTP_WAIT_MS);
    }
    last_use_ms = now_ms();
    xSemaphoreGive(lock);
    return ok;
}

/* Texto de status para Ajustes > Wi-Fi > Status. */
void network_status(char *buf, int length)
{
    static const char *const names[] = {"desativada (OFF)", "permitida, rádio desligado", "conectando", "conectada"};
    if (xSemaphoreTake(lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        snprintf(buf, (size_t)length, "Rede: conectando...");
        return;
    }
    network_state_t e = network_state();
    char ip[20];
    wifi_ip(ip, sizeof(ip));
    uint32_t ntp = clock_last_ntp();
    char ntp_txt[24] = "nunca";
    if (ntp) {
        struct tm tm;
        time_t t = ntp;
        localtime_r(&t, &tm);
        snprintf(ntp_txt, sizeof(ntp_txt), "%02d/%02d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min);
    }
    snprintf(buf, (size_t)length, "Rede: %s\nRoteador: %s\nIP: %s\nSinal: %d dBm\nÚltimo NTP: %s", names[e],
             e == NETWORK_CONNECTED ? wifi_current_ssid() : "-", ip, e == NETWORK_CONNECTED ? wifi_rssi() : 0, ntp_txt);
    xSemaphoreGive(lock);
}

/* Desliga o rádio imediatamente. */
void network_turn_off(void)
{
    take_back();
    xSemaphoreTake(lock, portMAX_DELAY);
    wifi_turn_off();
    bars = 0;
    xSemaphoreGive(lock);
}

/* Suspende o uso da rede (libera a memória do driver para os jogos Lua). */
void network_suspend(bool suspend)
{
    if (suspend) {
        take_back(); /* o jogo precisa da memória do driver: o Conectar desliga antes */
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    suspended = suspend;
    if (suspend) {
        wifi_turn_off();
        bars = 0;
    }
    xSemaphoreGive(lock);
}

/* ------------------------------------------------------------------------- */

/* Tarefa em segundo plano: sincroniza a hora se conectou. */
static void job_ntp(bool connected, void *ctx)
{
    (void)ctx;
    if (connected) {
        clock_sync_ntp(NTP_WAIT_MS);
    }
}

/* Verdadeiro se a hora é inválida ou a última sincronização passou de NETWORK_NTP_INTERVAL_S. */
static bool ntp_due(void)
{
    if (!clock_is_valid() || clock_last_ntp() == 0) {
        return true;
    }
    return clock_now() - clock_last_ntp() >= NETWORK_NTP_INTERVAL_S;
}

/* Tarefa em segundo plano: procura no GitHub uma release mais nova que o firmware. */
static void job_update(bool connected, void *ctx)
{
    (void)ctx;
    char tag[sizeof(update_tag)], error[48];
    if (!connected || !ota_github_latest(tag, sizeof(tag), error, sizeof(error))) {
        return; /* tenta de novo após UPDATE_NEW_ATTEMPT_MS */
    }
    update_checked_at = clock_now();
    if (ota_is_newer(tag)) {
        ESP_LOGI(TAG, "Nova release disponível: %s", tag);
        snprintf(update_tag, sizeof(update_tag), "%s", tag);
        update_found = true;
    }
}

/* Verdadeiro se a hora é válida e a última verificação de release tem mais de 24 h. */
static bool update_due(void)
{
    return clock_is_valid() && (update_checked_at == 0 || clock_now() - update_checked_at >= NETWORK_UPDATE_INTERVAL_S);
}

/* Release mais nova achada pela verificação automática (NULL = nenhuma). */
const char *network_new_release(void)
{
    return update_found ? update_tag : NULL;
}

/* Descarta o aviso da release achada. */
void network_forget_release(void)
{
    update_found = false;
}

/* Desliga o rádio ocioso, atualiza o cache de sinal e agenda o NTP e a verificação de release. */
static void maintenance(void)
{
    uint32_t now = now_ms();
    xSemaphoreTake(lock, portMAX_DELAY);
    /* Suspensa ou emprestada ao Conectar, o rádio não é da rede: não mexe nele */
    if (!suspended && !lent && wifi_on() && users == 0 && !connecting && now - last_use_ms >= NETWORK_IDLE_MS) {
        ESP_LOGI(TAG, "Ociosa: desligando o rádio");
        wifi_turn_off();
    }
    bars = wifi_connected() ? wifi_bars() : 0;
    /* A release só é procurada com a hora certa, para contar o intervalo pelo relógio */
    bool schedule = ntp_due() && (!ntp_tried || now - ntp_tried_ms >= NTP_NEW_ATTEMPT_MS);
    bool check_update =
        !schedule && update_due() && (!update_tried || now - update_tried_ms >= UPDATE_NEW_ATTEMPT_MS);
    static saved_network_t saved_networks[CONFIG_MAX_NETWORKS];
    /* Com o rádio no Conectar, as tarefas automáticas esperam (rodam quando ele desligar, no modo Ocioso) */
    if ((schedule || check_update) && (lent || !network_allowed() || config_networks(saved_networks) == 0)) {
        schedule = check_update = false;
    }
    if (schedule) {
        ntp_tried = true;
        ntp_tried_ms = now;
    }
    if (check_update) {
        update_tried = true;
        update_tried_ms = now;
    }
    xSemaphoreGive(lock);
    if (schedule) {
        network_run(job_ntp, NULL);
    }
    if (check_update) {
        network_run(job_update, NULL);
    }
}

/* Task da rede: executa as tarefas da fila e faz a manutenção a cada segundo. */
static void job_network(void *arg)
{
    (void)arg;
    network_job_t t;
    for (;;) {
        if (xQueueReceive(queue, &t, pdMS_TO_TICKS(1000)) == pdTRUE) {
            while (suspended) {
                vTaskDelay(pdMS_TO_TICKS(500)); /* espera o jogo terminar */
            }
            network_use();
            bool ok = network_wait(CONNECTION_FAST_MS + CONNECTION_FULL_MS);
            t.fn(ok, t.ctx);
            network_release();
        }
        maintenance();
    }
}

/*
 * Cria a trava, a fila e a task da rede (pilha maior para o TLS do GitHub).
 * O aperto de mão do TLS calcula por mais de 5 s seguidos: com prioridade
 * acima da task ociosa, ela não rodava e o watchdog disparava. Na mesma
 * prioridade da ociosa (0), as duas se revezam a cada tick; no núcleo 1, longe
 * do laço principal da tela.
 */
void network_init(void)
{
    lock = xSemaphoreCreateMutex();
    queue = xQueueCreate(8, sizeof(network_job_t));
    last_use_ms = now_ms();
    xTaskCreatePinnedToCore(job_network, "rede", 8192, NULL, tskIDLE_PRIORITY, NULL, 1);
}
