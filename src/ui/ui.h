#ifndef GUARA_UI_H
#define GUARA_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "botoes.h"
#include "gfx.h"

/*
 * Pilha de telas. B volta pelo caminho inverso e a tela de baixo é retomada
 * com o mesmo estado (seleção preservada), como pede o README.
 */
typedef struct tela {
    const char *nome;
    bool modal;      /* desenha a tela de baixo antes (sobreposição) */
    uint8_t fps;     /* 0 = padrão (20 FPS) */
    void (*abrir)(void *param);
    void (*retomar)(void);
    void (*fechar)(void);
    void (*tecla)(const evento_tecla_t *ev);
    void (*desenhar)(gfx_t *g, uint32_t agora_ms);
} tela_t;

#define UI_FPS_PADRAO 20

void ui_abrir(const tela_t *t, void *param);
void ui_voltar(void);
void ui_substituir(const tela_t *t, void *param);
/* Volta até `t` ficar no topo (ou até a base). */
void ui_voltar_ate(const tela_t *t);
void ui_limpar_pilha(const tela_t *base, void *param);
const tela_t *ui_topo(void);
int ui_profundidade(void);

void ui_processar_tecla(const evento_tecla_t *ev);
void ui_desenhar(gfx_t *g, uint32_t agora_ms);
int ui_fps(void);
/* Índice na pilha da tela cujo callback está executando (para telas reutilizáveis como os modais). */
int ui_camada_atual(void);

#endif
