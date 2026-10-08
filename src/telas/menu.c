#include <stdio.h>

#include "app.h"
#include "componentes.h"
#include "persist.h"
#include "telas.h"

/* Menu principal em carrossel (mockup bitmaps/exemple/main_menu.bmp). */

typedef struct {
    const char *nome;
    const char *icone;
    const tela_t *tela;   /* tela a abrir, ou */
    void (*abrir)(void);  /* função que abre um sub-menu */
} item_menu_t;

static const item_menu_t ITENS[] = {
    {"GUARA", "guara", &TELA_GUARA, NULL},
    {"INVENTARIO", "inventory", &TELA_INVENTARIO, NULL},
    {"EQUIPAMENTOS", "equipment", &TELA_EQUIPAMENTOS, NULL},
    {"HABILIDADES", "mp", &TELA_HABILIDADES, NULL},
    {"ARENA", "arena", NULL, arena_abrir_menu},
    {"BIBLIOTECA", "library", &TELA_BIBLIOTECA, NULL},
    {"JOGOS", "games", &TELA_JOGOS, NULL},
    {"MUSICAS", "music", &TELA_MUSICAS, NULL},
    {"IMAGENS", "images", &TELA_IMAGENS, NULL},
    {"RELOGIO", "clock", NULL, relogio_abrir_menu},
    {"CARTAO SD", "sd_card", &TELA_CARTAO_SD, NULL},
    {"AJUSTES", "adjustments", NULL, ajustes_abrir_menu},
};
#define QTD ((int)(sizeof(ITENS) / sizeof(ITENS[0])))

#define ANIM_MS 160
#define CENTRO_X 48   /* ícone 2x (32x32) */
#define CENTRO_Y 6
#define LADO_DIST 40  /* distância entre os centros dos ícones */
#define LADO_Y 14

static int sel;
static int anim_dir;  /* -1 veio do anterior, +1 do próximo */
static uint32_t anim_inicio;

static void tecla(const evento_tecla_t *ev)
{
    if (ev->tipo == TECLA_SOLTOU) {
        return;
    }
    switch (ev->tecla) {
    case TECLA_ESQ: /* item da esquerda (anterior) vem para o centro */
        sel = (sel - 1 + QTD) % QTD;
        anim_dir = -1;
        anim_inicio = app_ms();
        break;
    case TECLA_DIR:
        sel = (sel + 1) % QTD;
        anim_dir = 1;
        anim_inicio = app_ms();
        break;
    case TECLA_A:
        if (ev->tipo == TECLA_PRESSIONOU) {
            if (ITENS[sel].tela) {
                ui_abrir(ITENS[sel].tela, NULL);
            } else {
                ITENS[sel].abrir();
            }
        }
        break;
    case TECLA_B:
        if (ev->tipo == TECLA_PRESSIONOU) {
            ui_voltar();
        }
        break;
    default:
        break;
    }
}

static void icone(gfx_t *g, int indice, int cx, bool grande)
{
    const gfx_bitmap_t *b = vrom(ITENS[(indice + QTD) % QTD].icone);
    if (!b) {
        return;
    }
    if (grande) {
        gfx_bitmap(g, b, cx - b->largura, CENTRO_Y, GFX_OU, GFX_ESCALA2);
    } else {
        gfx_bitmap(g, b, cx - b->largura / 2, LADO_Y, GFX_OU, 0);
    }
}

static void desenhar(gfx_t *g, uint32_t agora)
{
    int desloc = 0;
    uint32_t t = agora - anim_inicio;
    if (anim_dir && t < ANIM_MS) {
        desloc = anim_dir * LADO_DIST * (int)(ANIM_MS - t) / ANIM_MS;
    } else {
        anim_dir = 0;
    }
    bool metade = anim_dir == 0 || t >= ANIM_MS / 2;
    int centro = 64 + desloc;

    gfx_recortar(g, 9, 0, 119, 44);
    for (int k = -2; k <= 2; k++) {
        int cx = centro + k * LADO_DIST;
        bool grande = (k == 0 && metade) || (k == -anim_dir && anim_dir != 0 && !metade);
        if (anim_dir == 0) {
            grande = k == 0;
        }
        icone(g, sel + k, cx, grande);
    }
    gfx_sem_recorte(g);

    /* Setas: lado direito do aparelho (esquerda do usuário) aponta para fora */
    gfx_seta(g, 2, 17, 9, -1, GFX_BRANCO);
    gfx_seta(g, 121, 17, 9, 1, GFX_BRANCO);

    /* Nome do item em caixa branca */
    const char *nome = ITENS[sel].nome;
    int w = gfx_largura_texto(nome) + 6;
    gfx_retangulo_cheio(g, 64 - w / 2, 44, w, 11, GFX_BRANCO);
    gfx_texto_centro(g, 0, GFX_LARGURA, 46, nome, GFX_PRETO);

    /* Indicador de pontos */
    int largura = QTD * 6 - 2, x0 = 64 - largura / 2;
    for (int i = 0; i < QTD; i++) {
        if (i == sel) {
            gfx_retangulo_cheio(g, x0 + i * 6 - 1, 58, 4, 4, GFX_BRANCO);
        } else {
            gfx_pixel(g, x0 + i * 6, 59, GFX_BRANCO);
            gfx_pixel(g, x0 + i * 6 + 1, 59, GFX_BRANCO);
        }
    }
}

static void retomar(void)
{
    persist_salvar_se_sujo(&g_guara);
}

const tela_t TELA_MENU = {
    .nome = "menu",
    .tecla = tecla,
    .desenhar = desenhar,
    .retomar = retomar,
};
