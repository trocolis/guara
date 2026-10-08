#include <stdio.h>

#include "app.h"
#include "componentes.h"
#include "modal.h"
#include "telas.h"

/*
 * Tela de Equipamentos (mockup bitmaps/exemple/menu_equip.bmp): sem título nem
 * rodapé, manequim do lado direito do aparelho (esquerda do usuário) e lista
 * com os 7 slots. A região do slot selecionado pisca no manequim 2x por segundo.
 */

#define MANEQUIM_X 6
#define MANEQUIM_Y 4
#define DIVISOR_X 42
#define LISTA_X 45
#define LINHA_H 8

static const char *const NOMES_SLOT[QTD_SLOTS_EQUIP] = {"Cabeça", "Corpo", "Mão ATK", "Mão DEF",
                                                       "Calçados", "Acessório 1", "Acessório 2"};
/* Regiões no manequim 32x56 (x, y, largura, altura) */
static const int8_t REGIOES[QTD_SLOTS_EQUIP][4] = {
    {7, 3, 18, 20}, {10, 22, 12, 19}, {5, 25, 6, 16}, {21, 25, 6, 16}, {6, 46, 20, 8}, {11, 19, 10, 5}, {9, 38, 14, 4},
};

static int slot;
static marquee_t mq;

/* Sub-menu com os equipamentos possuídos do tipo do slot */
static int opcoes_ids[64];
static ui_lista_t lista_eq;

static void item_eq(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    registro_t r;
    catalogo_ler(CAT_EQUIPAMENTOS, opcoes_ids[i], &r);
    it->icone = r.icone;
    snprintf(it->texto, sizeof(it->texto), "%s", r.nome);
    bool equipado = false;
    for (int s = 0; s < QTD_SLOTS_EQUIP; s++) {
        equipado |= g_guara.equipado[s] == opcoes_ids[i];
    }
    if (equipado) {
        snprintf(it->info, sizeof(it->info), "EQP");
    }
}

static void eq_tecla(const evento_tecla_t *ev)
{
    if (ui_lista_tecla(&lista_eq, ev, UI_LINHAS_VISIVEIS) || ev->tipo != TECLA_PRESSIONOU) {
        return;
    }
    if (ev->tecla == TECLA_B) {
        ui_voltar();
    } else if (ev->tecla == TECLA_A && lista_eq.total) {
        guara_equipar(&g_guara, slot, opcoes_ids[lista_eq.sel]);
        app_alterou();
        ui_voltar();
    }
}

static void eq_desenhar(gfx_t *g, uint32_t agora)
{
    ui_titulo(g, NOMES_SLOT[slot], agora);
    if (lista_eq.total == 0) {
        gfx_texto_centro(g, 0, GFX_LARGURA, 32, "Nada para equipar", GFX_BRANCO);
        return;
    }
    ui_lista_desenhar(g, &lista_eq, UI_TITULO_ALTURA, UI_LINHAS_VISIVEIS, item_eq, NULL, agora);
}

static const tela_t TELA_LISTA_EQUIP = {
    .nome = "lista_equip",
    .tecla = eq_tecla,
    .desenhar = eq_desenhar,
};

static void abrir_lista(void)
{
    registro_t r;
    lista_eq = (ui_lista_t){0};
    for (int id = 0; id < catalogo_quantidade(CAT_EQUIPAMENTOS) && id < 64; id++) {
        if ((g_guara.equip_possui & (1ull << id)) && catalogo_ler(CAT_EQUIPAMENTOS, id, &r) &&
            r.tipo == guara_tipo_do_slot(slot)) {
            opcoes_ids[lista_eq.total++] = id;
        }
    }
    ui_abrir(&TELA_LISTA_EQUIP, NULL);
}

static void opcao(int r, void *ctx)
{
    (void)ctx;
    if (r == 0) {
        abrir_lista();
    } else if (r == 1) {
        guara_desequipar(&g_guara, slot);
        app_alterou();
    }
}

static void tecla(const evento_tecla_t *ev)
{
    if (ev->tipo == TECLA_SOLTOU) {
        return;
    }
    if (ev->tecla == TECLA_CIMA) {
        slot = (slot + QTD_SLOTS_EQUIP - 1) % QTD_SLOTS_EQUIP;
    } else if (ev->tecla == TECLA_BAIXO) {
        slot = (slot + 1) % QTD_SLOTS_EQUIP;
    } else if (ev->tipo != TECLA_PRESSIONOU) {
        return;
    } else if (ev->tecla == TECLA_B) {
        ui_voltar();
    } else if (ev->tecla == TECLA_A) {
        if (g_guara.equipado[slot] == SLOT_VAZIO) {
            abrir_lista();
        } else {
            static const char *const opcoes[] = {"Equipar", "Remover"};
            ui_escolha(NOMES_SLOT[slot], opcoes, 2, opcao, NULL);
        }
    }
}

static void desenhar(gfx_t *g, uint32_t agora)
{
    gfx_retangulo_arred(g, 0, 0, GFX_LARGURA, GFX_ALTURA, 3, GFX_BRANCO);
    gfx_linha_v(g, DIVISOR_X, 1, GFX_ALTURA - 2, GFX_BRANCO);
    ui_icone(g, "mannequin", MANEQUIM_X, MANEQUIM_Y);
    if ((agora / 250) % 2 == 0) {
        const int8_t *r = REGIOES[slot];
        gfx_retangulo(g, MANEQUIM_X + r[0], MANEQUIM_Y + r[1], r[2], r[3], GFX_INVERTER);
    }
    for (int s = 0; s < QTD_SLOTS_EQUIP; s++) {
        int y = 2 + s * LINHA_H + (s ? 1 : 0) * 0;
        char nome[40];
        registro_t r;
        if (g_guara.equipado[s] != SLOT_VAZIO && catalogo_ler(CAT_EQUIPAMENTOS, g_guara.equipado[s], &r)) {
            snprintf(nome, sizeof(nome), "%s", r.nome);
        } else {
            snprintf(nome, sizeof(nome), "- Vazio -");
        }
        bool sel = s == slot;
        if (sel) {
            gfx_retangulo_arred_cheio(g, LISTA_X - 1, y, GFX_LARGURA - LISTA_X - 1, LINHA_H + 1, 2, GFX_BRANCO);
        }
        int largura = GFX_LARGURA - LISTA_X - 5;
        int w = gfx_largura_texto(nome);
        if (w <= largura) {
            gfx_texto(g, LISTA_X + 1 + (largura - w) / 2, y + 1, nome, sel ? GFX_PRETO : GFX_BRANCO);
        } else {
            ui_texto_rolante(g, LISTA_X + 1, y + 1, largura, nome, sel ? GFX_PRETO : GFX_BRANCO, sel, &mq, agora);
        }
    }
}

const tela_t TELA_EQUIPAMENTOS = {
    .nome = "equipamentos",
    .tecla = tecla,
    .desenhar = desenhar,
};
