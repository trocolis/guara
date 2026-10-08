#ifndef GUARA_APP_H
#define GUARA_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "guara.h"
#include "rng.h"

/* Estado global da aplicação e serviços usados pelas telas. */
extern guara_t g_guara;
extern bool g_tem_guara;
extern rng_t g_rng;

void app_iniciar(void);
void app_executar(void); /* não retorna */

uint32_t app_ms(void);
void app_relogio(relogio_ctx_t *rel);
/* Interação do usuário com o Guará (zera o ócio, medidor Dia×Noite). */
void app_interagir(void);
/* O Guará mudou: espelha na RTC RAM e marca para gravar na Flash ao sair do menu. */
void app_alterou(void);
/* Grava imediatamente na Flash (level up, evolução, emergência). */
void app_salvar(void);
/* Impede o deep sleep por inatividade (jogos, cronômetro na tela, etc.). */
void app_bloquear_sono(bool bloquear);
/* Concede XP; trata level up (som + gravação). Retorna os níveis ganhos. */
int app_ganhar_xp(uint32_t pontos);
void app_nascer(const char *nome);
/* Factory Rebirth: apaga o save e volta para o nascimento. */
void app_renascer(void);
/* Tenta reconectar ao Wi-Fi salvo e sincronizar a hora (bloqueante). */
bool app_conectar_wifi_salvo(uint32_t espera_ms);
const char *app_versao(void);

#endif
