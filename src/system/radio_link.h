/*
 * Rádio do Conectar: ESP-NOW entre dois Guarás, sem roteador. Enquanto ativo,
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
#define RADIO_LINK_BACKGROUND_MS 250  /* a task de segundo plano confere o pedido do app */

/* Tela Conectar aberta: liga o rádio (se preciso) e anuncia `me`. Falso se o rádio não iniciou. */
bool radio_link_start(const link_profile_t *me);
/* Tela Conectar fechada: desliga, a menos que o segundo plano esteja pedido. */
void radio_link_stop(void);
/*
 * Presença em segundo plano, pedida pelo app a cada segundo (Visível em ON, Guará
 * nascido, fora do modo Ocioso). Uma task liga e desliga sem prender a tela; a
 * rede tem prioridade e pega o rádio de volta quando precisa.
 */
void radio_link_background(bool want, const link_profile_t *me);
/*
 * Conectar > Visível: só na RTC RAM (sobrevive ao deep sleep, volta a OFF ao
 * ligar ou reiniciar), para não gravar na Flash uma opção que muda sempre.
 */
bool radio_link_visible(void);
void radio_link_set_visible(bool on);
/*
 * Só escuta (sub-menu Conectar aberto, para mostrar os conhecidos online): liga
 * o rádio como o segundo plano, mas sem anunciar a presença; quem está com
 * Visível em OFF continua invisível.
 */
void radio_link_watch(bool want);
/* Verdadeiro se o Guará `uid` foi visto há pouco (falso com o rádio desligado). */
bool radio_link_seen(uint32_t uid);
/* Verdadeiro se chegou uma oferta ou um desafio (para o app abrir a tela Conectar). */
bool radio_link_has_invite(void);
/* Verdadeiro com a tela Conectar aberta (ela mesma trata as mensagens). */
bool radio_link_held(void);
bool radio_link_active(void);
/* Envia uma mensagem para `mac` (o perfil `from` é preenchido aqui). */
bool radio_link_send(const uint8_t mac[6], link_msg_t *m);
/* Próxima mensagem recebida (as de presença viram a lista de vizinhos). Não bloqueia. */
bool radio_link_receive(uint8_t mac[6], link_msg_t *m);
/* Copia os vizinhos vistos recentemente. Retorna quantos. */
int radio_link_peers(link_peer_t *peers, int max);

#endif
