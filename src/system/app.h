#ifndef GUARA_APP_H
#define GUARA_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "guara.h"
#include "rng.h"

/* Estado global da aplicação e serviços usados pelas screens. */
extern guara_t g_guara;
extern bool g_has_guara;
extern rng_t g_rng;

void app_init(void);
void app_run(void); /* não retorna */

uint32_t app_ms(void);
void app_clock_context(clock_ctx_t *clock_ctx);
/* Interação do usuário com o Guará (zera o ócio, medidor Dia×Noite). */
void app_interact(void);
/* O Guará mudou: espelha na RTC RAM e marca para gravar na Flash ao sair do menu. */
void app_mark_changed(void);
/* Grava imediatamente na Flash (level up, evolução, emergência). */
void app_save(void);
/* Impede o deep sleep por inatividade (jogos, cronômetro na tela, etc.). */
void app_block_sleep(bool should_block);
/* Pede o deep sleep imediato (console de depuração); o laço principal atende no próximo segundo. */
void app_request_sleep(void);
/* Concede XP; trata level up (som + gravação). Retorna os níveis ganhos. */
int app_gain_xp(uint32_t points);
void app_birth(const char *name);
/* Factory Rebirth: apaga o save e volta para o nascimento. */
void app_rebirth(void);
const char *app_version(void);

#endif
