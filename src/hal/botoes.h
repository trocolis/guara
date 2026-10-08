#ifndef GUARA_BOTOES_H
#define GUARA_BOTOES_H

#include <stdbool.h>
#include <stdint.h>

/* Teclas lógicas. ESQ/DIR são em coordenadas de tela (ver pinos.h). */
typedef enum {
    TECLA_CIMA,
    TECLA_BAIXO,
    TECLA_ESQ,
    TECLA_DIR,
    TECLA_A,
    TECLA_B,
    QTD_TECLAS,
} tecla_t;

typedef enum {
    TECLA_PRESSIONOU,
    TECLA_REPETIU,  /* auto-repetição enquanto segura (só direcionais) */
    TECLA_SOLTOU,
} tipo_evento_t;

typedef struct {
    tecla_t tecla;
    tipo_evento_t tipo;
} evento_tecla_t;

void botoes_iniciar(void);
/* Retorna true se havia um evento na fila (espera até `espera_ms`). */
bool botoes_ler(evento_tecla_t *ev, uint32_t espera_ms);
bool botoes_segurando(tecla_t t);
/* Momento (ms desde o boot) da última tecla pressionada. */
uint32_t botoes_ultimo_uso_ms(void);
void botoes_esvaziar(void);
/* Simula pressionar e soltar uma tecla (console de depuração). */
void botoes_injetar(tecla_t t);

#endif
