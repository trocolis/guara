#ifndef GUARA_ALARMES_H
#define GUARA_ALARMES_H

#include <stdbool.h>
#include <stdint.h>

/* Despertadores (gravados no cartão SD) e temporizador (RTC RAM). */
#define ALARMES_MAX 8
#define ALARMES_ARQUIVO "/sdcard/guara/alarmes.txt"

void alarmes_carregar(void);
int alarmes_quantidade(void);
int alarmes_minuto(int indice);
/* Retorna false se não houver cartão SD ou a lista estiver cheia. */
bool alarmes_adicionar(int minuto_do_dia);
bool alarmes_editar(int indice, int minuto_do_dia);
bool alarmes_remover(int indice);
/* Verdadeiro uma vez quando algum despertador bate neste minuto. */
bool alarmes_verificar(int minuto_do_dia);
/* Segundos até o próximo despertador (0 se nenhum ou hora desconhecida). */
uint32_t alarmes_segundos_ate_proximo(void);

/* Temporizador */
void temporizador_iniciar(uint32_t segundos);
void temporizador_parar(void);
bool temporizador_ativo(void);
uint32_t temporizador_restante(void);
uint32_t temporizador_configurado(void);
/* Verdadeiro uma vez quando o temporizador termina. */
bool temporizador_verificar(void);

#endif
