#ifndef GUARA_BUTTONS_H
#define GUARA_BUTTONS_H

#include <stdbool.h>
#include <stdint.h>

/* Teclas lógicas. ESQ/DIR são em coordenadas de tela (ver pins.h). */
typedef enum {
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_A,
    KEY_B,
    COUNT_KEYS,
} input_key_t;

typedef enum {
    KEY_PRESSED,
    KEY_REPEATED,  /* auto-repetição enquanto segura (só direcionais) */
    KEY_RELEASED,
} key_event_type_t;

typedef struct {
    input_key_t key;
    key_event_type_t type;
} key_event_t;

void buttons_init(void);
/* Retorna true se havia um evento na fila (espera até `timeout_ms`). */
bool buttons_read(key_event_t *ev, uint32_t timeout_ms);
bool buttons_is_held(input_key_t t);
/* Momento (ms desde o boot) da última tecla pressionada. */
uint32_t buttons_last_use_ms(void);
void buttons_flush(void);
/* Simula pressionar e soltar uma tecla (console de depuração). */
void buttons_inject(input_key_t t);

#endif
