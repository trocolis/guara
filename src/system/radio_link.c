/*
 * Rádio do Conectar (ESP-NOW): pega o rádio emprestado da rede com roteador,
 * liga o Wi-Fi em modo estação no canal RADIO_LINK_CHANNEL, anuncia a presença
 * a cada segundo em broadcast e entrega as outras mensagens por uma fila.
 *
 * Fica ligado enquanto a tela Conectar está aberta ("segurado") ou, em
 * segundo plano, enquanto o app pedir (Wi-Fi em ON e fora do modo Ocioso).
 * Uma task própria liga e desliga a presença de segundo plano sem prender a
 * tela; quando a rede precisa do rádio, ele é devolvido na hora.
 */
#include "radio_link.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "network.h"
#include "wifi.h"

static const char *TAG = "conectar";

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct {
    uint8_t mac[6];
    link_msg_t msg;
} received_t;

static volatile bool active;
static volatile bool held;       /* tela Conectar aberta */
static volatile bool background; /* o app quer presença em segundo plano */
static volatile bool watching;   /* sub-menu Conectar: só escuta, sem anunciar */
static RTC_DATA_ATTR bool visible; /* Conectar > Visível: OFF ao ligar ou reiniciar, mantido no deep sleep */
static SemaphoreHandle_t state_lock; /* liga/desliga: tela, task de fundo e devolução à rede */
static link_profile_t profile;
static link_peers_t peers;
static SemaphoreHandle_t lock;
static QueueHandle_t inbox;
static esp_timer_handle_t hello_timer;

/* Milissegundos desde o boot. */
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Registra `mac` como destino de unicast (o ESP-NOW exige o par cadastrado). */
static void add_peer(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(mac)) {
        return;
    }
    esp_now_peer_info_t p = {0};
    memcpy(p.peer_addr, mac, 6);
    p.channel = 0; /* o canal atual */
    p.ifidx = WIFI_IF_STA;
    p.encrypt = false;
    esp_now_add_peer(&p);
}

/* Recebido (task do Wi-Fi): presença atualiza os vizinhos; o resto vai para a fila. */
static void on_receive(const esp_now_recv_info_t *info, const uint8_t *data, int length)
{
    received_t r;
    if (!link_decode(data, length, &r.msg) || r.msg.from.uid == profile.uid) {
        return;
    }
    memcpy(r.mac, info->src_addr, 6);
    if (r.msg.type == LINK_HELLO) {
        xSemaphoreTake(lock, portMAX_DELAY);
        link_peers_seen(&peers, r.mac, &r.msg.from, now_ms());
        xSemaphoreGive(lock);
        return;
    }
    xQueueSend(inbox, &r, 0);
}

/* Presença em broadcast a cada RADIO_LINK_HELLO_MS (só escutando, não anuncia). */
static void send_hello(void *arg)
{
    (void)arg;
    if (!held && !background) {
        return;
    }
    link_msg_t m = {.type = LINK_HELLO};
    radio_link_send(BROADCAST, &m);
}

/* Cria as estruturas na primeira vez. */
static void ensure_init(void)
{
    if (!lock) {
        lock = xSemaphoreCreateMutex();
        state_lock = xSemaphoreCreateMutex();
        inbox = xQueueCreate(8, sizeof(received_t));
        const esp_timer_create_args_t args = {.callback = send_hello, .name = "conectar"};
        esp_timer_create(&args, &hello_timer);
    }
}

/* Liga o ESP-NOW (com state_lock). */
static bool turn_on(void)
{
    if (active) {
        return true;
    }
    if (network_suspended()) {
        return false; /* um jogo Lua está com a memória do driver */
    }
    memset(&peers, 0, sizeof(peers));
    xQueueReset(inbox);
    if (!network_lend(true)) { /* sai do roteador e pega o rádio, se a rede não estiver em uso */
        return false;
    }
    if (!wifi_turn_on() || !wifi_set_channel(RADIO_LINK_CHANNEL) || esp_now_init() != ESP_OK) {
        ESP_LOGE(TAG, "Rádio não iniciou");
        wifi_turn_off();
        network_lend(false);
        return false;
    }
    esp_now_register_recv_cb(on_receive);
    add_peer(BROADCAST);
    active = true;
    send_hello(NULL);
    esp_timer_start_periodic(hello_timer, RADIO_LINK_HELLO_MS * 1000);
    ESP_LOGI(TAG, "Ativo no canal %d%s", RADIO_LINK_CHANNEL, held ? "" : background ? " (segundo plano)" : " (só escuta)");
    return true;
}

/* Desliga o ESP-NOW e devolve o rádio à rede (com state_lock). */
static void turn_off(void)
{
    if (!active) {
        return;
    }
    active = false;
    esp_timer_stop(hello_timer);
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    wifi_turn_off();
    network_lend(false);
    ESP_LOGI(TAG, "Desligado");
}

/* A rede pediu o rádio (NTP, GitHub, busca de redes, jogo): devolve, mesmo com a tela aberta. */
static void give_back(void)
{
    xSemaphoreTake(state_lock, portMAX_DELAY);
    turn_off();
    xSemaphoreGive(state_lock);
}

/* Task de segundo plano: liga quando o app quer e a rede está livre; desliga quando não quer mais. */
static void job_background(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(RADIO_LINK_BACKGROUND_MS));
        xSemaphoreTake(state_lock, portMAX_DELAY);
        if ((background || watching) && !active && !network_busy()) {
            turn_on();
        } else if (!background && !watching && !held && active) {
            turn_off();
        }
        xSemaphoreGive(state_lock);
    }
}

/* Pedido do app, a cada segundo: presença em segundo plano (ou não) e o perfil atual. */
void radio_link_background(bool want, const link_profile_t *me)
{
    ensure_init();
    static bool task_started;
    if (!task_started) {
        task_started = true;
        network_set_reclaim(give_back);
        xTaskCreatePinnedToCore(job_background, "conectar", 3072, NULL, 3, NULL, 0);
    }
    profile = *me; /* nome e nível atualizados na próxima presença */
    background = want;
}

/* Tela Conectar aberta: liga na hora (ou aproveita o segundo plano) e segura até fechar. */
bool radio_link_start(const link_profile_t *me)
{
    ensure_init();
    xSemaphoreTake(state_lock, portMAX_DELAY);
    profile = *me;
    held = true;
    bool ok = turn_on();
    xSemaphoreGive(state_lock);
    return ok;
}

/* Tela Conectar fechada: continua só se o segundo plano quiser. */
void radio_link_stop(void)
{
    if (!lock) {
        return;
    }
    xSemaphoreTake(state_lock, portMAX_DELAY);
    held = false;
    if (!background && !watching) {
        turn_off();
    }
    xSemaphoreGive(state_lock);
}

bool radio_link_visible(void)
{
    return visible;
}

/* Liga ou desliga o Visível (o app aplica no próximo segundo). */
void radio_link_set_visible(bool on)
{
    visible = on;
}

/* Sub-menu Conectar aberto ou fechado: a task de segundo plano liga ou desliga em até 250 ms. */
void radio_link_watch(bool want)
{
    watching = want;
}

/* Procura `uid` entre os vizinhos vistos nos últimos LINK_PEER_TIMEOUT_MS (só a interface chama). */
bool radio_link_seen(uint32_t uid)
{
    static link_peer_t seen[LINK_MAX_PEERS];
    int n = radio_link_peers(seen, LINK_MAX_PEERS);
    for (int i = 0; i < n; i++) {
        if (seen[i].profile.uid == uid) {
            return true;
        }
    }
    return false;
}

/* Verdadeiro com a tela Conectar aberta. */
bool radio_link_held(void)
{
    return held;
}

/* Verdadeiro se há uma oferta ou um desafio esperando (outras mensagens soltas são descartadas). */
bool radio_link_has_invite(void)
{
    received_t r;
    while (active && xQueuePeek(inbox, &r, 0) == pdTRUE) {
        if (r.msg.type == LINK_OFFER || r.msg.type == LINK_CHALLENGE) {
            return true;
        }
        xQueueReceive(inbox, &r, 0);
    }
    return false;
}

bool radio_link_active(void)
{
    return active;
}

/* Envia para `mac` com o perfil deste Guará. */
bool radio_link_send(const uint8_t mac[6], link_msg_t *m)
{
    if (!active) {
        return false;
    }
    m->from = profile;
    uint8_t buf[LINK_MAX_SIZE];
    int n = link_encode(m, buf);
    if (n == 0) {
        return false;
    }
    add_peer(mac);
    return esp_now_send(mac, buf, (size_t)n) == ESP_OK;
}

/* Próxima mensagem da fila, sem esperar. */
bool radio_link_receive(uint8_t mac[6], link_msg_t *m)
{
    received_t r;
    if (!active || xQueueReceive(inbox, &r, 0) != pdTRUE) {
        return false;
    }
    memcpy(mac, r.mac, 6);
    *m = r.msg;
    return true;
}

/* Copia os vizinhos, removendo os que sumiram. */
int radio_link_peers(link_peer_t *out, int max)
{
    if (!active) {
        return 0;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    link_peers_expire(&peers, now_ms());
    int n = peers.count < max ? peers.count : max;
    memcpy(out, peers.peers, sizeof(link_peer_t) * (size_t)n);
    xSemaphoreGive(lock);
    return n;
}
