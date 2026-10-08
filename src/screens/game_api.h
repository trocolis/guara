/*
 * Extensões da API Lua do console virtual: gráficos da VROM (modos, escala,
 * tamanho), o Guará do jogador, o catálogo e os componentes de interface do
 * sistema (desenho e modais). Registradas por games.c ao abrir cada jogo.
 */
#ifndef GUARA_GAME_API_H
#define GUARA_GAME_API_H

#include <stdbool.h>

#include "gfx.h"
#include "lua.h"

/* Registra os módulos `ui` e `catalog` e completa `oled` e `guara`. */
void game_api_register(lua_State *L);
/* Zera o estado dos componentes (listas, letreiros) ao abrir um jogo. */
void game_api_reset(void);

/* Serviços do runtime de jogos (implementados em games.c). */
gfx_t *game_runtime_framebuffer(void);
lua_State *game_runtime_lua(void);
/* Interrompe o jogo e mostra a mensagem de erro. */
void game_runtime_fail(const char *message);

#endif
