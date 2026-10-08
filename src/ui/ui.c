/*
 * Pilha de telas: abrir, voltar (preservando o estado da tela de baixo), desenhar
 * as camadas (modais sobre a tela de baixo) e despachar as teclas para o topo.
 */
#include "ui.h"

#include <stddef.h>

#define DEPTH_MAX 16

static const screen_t *stack[DEPTH_MAX];
static int top = -1;
static int layer;

/* Índice da tela cujo callback está executando (estado por camada dos modais). */
int ui_current_layer(void)
{
    return layer;
}

/* Empilha uma tela e chama seu on_open. */
void ui_open(const screen_t *t, void *param)
{
    if (top + 1 >= DEPTH_MAX) {
        return;
    }
    stack[++top] = t;
    layer = top;
    if (t->on_open) {
        t->on_open(param);
    }
}

/* Desempilha a tela do topo e chama seu on_close. */
static void pop(void)
{
    if (top < 0) {
        return;
    }
    layer = top;
    const screen_t *t = stack[top--];
    if (t->on_close) {
        t->on_close();
    }
}

/* Volta para a tela anterior (a base nunca sai) e a retoma. */
void ui_back(void)
{
    if (top <= 0) {
        return; /* a tela base (principal) nunca sai */
    }
    pop();
    layer = top;
    if (top >= 0 && stack[top]->on_resume) {
        stack[top]->on_resume();
    }
}

/* Troca a tela do topo por outra. */
void ui_replace(const screen_t *t, void *param)
{
    pop();
    ui_open(t, param);
}

/* Desempilha até `screen` ficar no topo. */
void ui_back_until(const screen_t *t)
{
    while (top > 0 && stack[top] != t) {
        pop();
    }
    layer = top;
    if (top >= 0 && stack[top]->on_resume) {
        stack[top]->on_resume();
    }
}

/* Esvazia a pilha e abre `base` como única tela. */
void ui_clear_stack(const screen_t *base, void *param)
{
    while (top >= 0) {
        pop();
    }
    ui_open(base, param);
}

/* Tela no topo da pilha. */
const screen_t *ui_top(void)
{
    return top >= 0 ? stack[top] : NULL;
}

/* Quantidade de telas na pilha. */
int ui_depth(void)
{
    return top + 1;
}

/* Entrega o evento de tecla à tela do topo. */
void ui_process_key(const key_event_t *ev)
{
    const screen_t *t = ui_top();
    layer = top;
    if (t && t->on_key) {
        t->on_key(ev);
    }
}

/* Desenha a tela do topo; se for modal, desenha antes as telas de baixo. */
void ui_draw(gfx_t *g, uint32_t now)
{
    if (top < 0) {
        return;
    }
    int base = top;
    while (base > 0 && stack[base]->modal) {
        base--;
    }
    gfx_clear(g);
    for (int i = base; i <= top; i++) {
        gfx_reset_clip(g);
        layer = i;
        if (stack[i]->on_draw) {
            stack[i]->on_draw(g, now);
        }
    }
    gfx_reset_clip(g);
}

/* Taxa de quadros pedida pela tela do topo. */
int ui_fps(void)
{
    const screen_t *t = ui_top();
    return t && t->fps ? t->fps : UI_FPS_DEFAULT;
}
