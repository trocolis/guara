#ifndef GUARA_UI_H
#define GUARA_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "buttons.h"
#include "gfx.h"

/*
 * Pilha de screens. B volta pelo caminho inverso e a tela de baixo é retomada
 * com o mesmo estado (seleção preservada), como pede o README.
 */
typedef struct screen {
    const char *name;
    bool modal;  /* desenha a tela de baixo antes (sobreposição) */
    uint8_t fps; /* 0 = padrão (20 FPS) */
    void (*on_open)(void *param);
    void (*on_resume)(void);
    void (*on_close)(void);
    void (*on_key)(const key_event_t *ev);
    void (*on_draw)(gfx_t *g, uint32_t now_ms);
    void (*on_draw_over)(gfx_t *g, uint32_t now_ms); /* opcional: desenhado por cima dos modais abertos sobre a tela */
} screen_t;

#define UI_FPS_DEFAULT 20

void ui_open(const screen_t *t, void *param);
void ui_back(void);
void ui_replace(const screen_t *t, void *param);
/* Volta até `t` ficar no topo (ou até a base). */
void ui_back_until(const screen_t *t);
void ui_clear_stack(const screen_t *base, void *param);
const screen_t *ui_top(void);
int ui_depth(void);

void ui_process_key(const key_event_t *ev);
void ui_draw(gfx_t *g, uint32_t now_ms);
int ui_fps(void);
/* Índice na pilha da tela cujo callback está executando (para screens reutilizáveis como os modais). */
int ui_current_layer(void);

#endif
