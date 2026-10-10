/*
 * Rádio do Encontro (ESP-NOW): suspende a rede com roteador, liga o Wi-Fi em
 * modo estação no canal RADIO_LINK_CHANNEL, anuncia a presença a cada segundo
 * em broadcast e entrega as outras mensagens por uma fila.
 */
#include "radio_link.h"

#include <string.h>

#include "esp_log.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "network.h"
#include "wifi.h"

static const char *TAG = "encontro";

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct {
    uint8_t mac[6];
    link_msg_t msg;
} received_t;

static bool active;
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

/* Presença em broadcast a cada RADIO_LINK_HELLO_MS. */
static void send_hello(void *arg)
{
    (void)arg;
    link_msg_t m = {.type = LINK_HELLO};
    radio_link_send(BROADCAST, &m);
}

/* Liga o rádio do Encontro e começa a anunciar. */
bool radio_link_start(const link_profile_t *me)
{
    if (active) {
        return true;
    }
    if (!lock) {
        lock = xSemaphoreCreateMutex();
        inbox = xQueueCreate(8, sizeof(received_t));
        const esp_timer_create_args_t args = {.callback = send_hello, .name = "encontro"};
        esp_timer_create(&args, &hello_timer);
    }
    profile = *me;
    memset(&peers, 0, sizeof(peers));
    xQueueReset(inbox);
    network_suspend(true); /* desconecta do roteador e libera o rádio */
    if (!wifi_turn_on() || !wifi_set_channel(RADIO_LINK_CHANNEL) || esp_now_init() != ESP_OK) {
        ESP_LOGE(TAG, "Rádio não iniciou");
        wifi_turn_off();
        network_suspend(false);
        return false;
    }
    esp_now_register_recv_cb(on_receive);
    add_peer(BROADCAST);
    active = true;
    send_hello(NULL);
    esp_timer_start_periodic(hello_timer, RADIO_LINK_HELLO_MS * 1000);
    ESP_LOGI(TAG, "Ativo no canal %d", RADIO_LINK_CHANNEL);
    return true;
}

/* Desliga o rádio do Encontro e devolve a rede. */
void radio_link_stop(void)
{
    if (!active) {
        return;
    }
    active = false;
    esp_timer_stop(hello_timer);
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    wifi_turn_off();
    network_suspend(false);
    ESP_LOGI(TAG, "Desligado");
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
    uint8_t buf[LINK_MESSAGE_SIZE];
    int n = link_encode(m, buf);
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
