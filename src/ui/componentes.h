#ifndef GUARA_COMPONENTES_H
#define GUARA_COMPONENTES_H

#include <stdbool.h>
#include <stdint.h>

#include "botoes.h"
#include "gfx.h"

/* Componentes padrão da UI (README > UI > Padrões do sistema). */

#define UI_TITULO_ALTURA 12   /* Y=0..11 */
#define UI_LINHA_ALTURA 16
#define UI_LINHAS_VISIVEIS 3  /* Y=12..59 */
#define UI_RODAPE_Y 54

/* Letreiro (marquee): rola o texto quando não cabe; reinicia quando a chave muda. */
typedef struct {
    uint32_t inicio;
    uint32_t chave;
} marquee_t;

uint32_t ui_hash(const char *texto);
void ui_texto_rolante(gfx_t *g, int x, int y, int largura, const char *texto, gfx_cor_t cor, bool rolar,
                      marquee_t *m, uint32_t agora);
/* Trunca para caber em `largura` px (em caracteres). */
void ui_truncar(char *dst, int tam, const char *src, int largura);

void ui_titulo(gfx_t *g, const char *texto, uint32_t agora);
void ui_abas(gfx_t *g, const char *const *nomes, int quantidade, int selecionada, uint32_t agora);
void ui_rolagem(gfx_t *g, int y0, int altura, int total, int visiveis, int topo);
void ui_rodape(gfx_t *g, const char *texto_b, const char *texto_a, uint32_t agora);
void ui_rodape_modal(gfx_t *g, int y_linha, const char *texto_b, const char *texto_a, uint32_t agora);
void ui_progresso(gfx_t *g, int x, int y, int largura, int altura, int valor, int maximo);
/* Desenha um bitmap da VROM pelo nome; retorna false se não existir. */
bool ui_icone(gfx_t *g, const char *nome, int x, int y);
void ui_icone_modo(gfx_t *g, const char *nome, int x, int y, gfx_modo_t modo, unsigned flags);
/* Quebra o texto em linhas de até `largura` px. Retorna o número de linhas desenhadas. */
int ui_paragrafo(gfx_t *g, int x, int y, int largura, int max_linhas, const char *texto, bool centralizar);

/* Lista padrão de sub-menu: 3 linhas de 16 px com ícone, texto e informação à direita. */
typedef struct {
    const char *icone;
    char texto[64];
    char info[8];
} ui_item_t;

typedef void (*ui_item_fn)(int indice, ui_item_t *item, void *ctx);

typedef struct {
    int total;
    int sel;
    int topo;
    marquee_t mq;
} ui_lista_t;

void ui_lista_ajustar(ui_lista_t *l, int linhas);
void ui_lista_desenhar(gfx_t *g, ui_lista_t *l, int y0, int linhas, ui_item_fn item, void *ctx, uint32_t agora);
/* Trata Cima/Baixo. Retorna true se consumiu o evento. */
bool ui_lista_tecla(ui_lista_t *l, const evento_tecla_t *ev, int linhas);

#endif
