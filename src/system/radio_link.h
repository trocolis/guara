/*
 * Rádio do Encontro: ESP-NOW entre dois Guarás, sem roteador. Enquanto ativo,
 * a rede (Wi-Fi com roteador) fica suspensa e o rádio fica num canal fixo.
 * O formato das mensagens está em lib/core/src/link_proto.h.
 */
#ifndef GUARA_RADIO_LINK_H
#define GUARA_RADIO_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "link_proto.h"

#define RADIO_LINK_CHANNEL 1          /* canal fixo: os dois aparelhos se encontram nele */
#define RADIO_LINK_HELLO_MS 1000      /* presença em broadcast */

/* Liga o rádio e começa a anunciar `me`. Falso se o rádio não iniciou. */
bool radio_link_start(const link_profile_t *me);
/* Para de anunciar, desliga o rádio e devolve a rede. */
void radio_link_stop(void);
bool radio_link_active(void);
/* Envia uma mensagem para `mac` (o perfil `from` é preenchido aqui). */
bool radio_link_send(const uint8_t mac[6], link_msg_t *m);
/* Próxima mensagem recebida (as de presença viram a lista de vizinhos). Não bloqueia. */
bool radio_link_receive(uint8_t mac[6], link_msg_t *m);
/* Copia os vizinhos vistos recentemente. Retorna quantos. */
int radio_link_peers(link_peer_t *peers, int max);

#endif
