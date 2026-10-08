#include "submenu.h"

#include <stdio.h>

#include "componentes.h"
#include "ui.h"

typedef struct {
    const submenu_t *def;
    ui_lista_t lista;
} estado_t;

static estado_t estados[16];
static const submenu_t *proximo;

static estado_t *atual(void)
{
    return &estados[ui_camada_atual() & 15];
}

static void abrir(void *param)
{
    (void)param;
    estado_t *e = atual();
    e->def = proximo;
    e->lista = (ui_lista_t){.total = proximo->quantidade};
}

static void item(int i, ui_item_t *it, void *ctx)
{
    const submenu_t *d = ctx;
    it->icone = d->itens[i].icone;
    snprintf(it->texto, sizeof(it->texto), "%s", d->itens[i].texto);
    if (d->itens[i].info) {
        d->itens[i].info(it->info, sizeof(it->info));
    }
}

static void tecla(const evento_tecla_t *ev)
{
    estado_t *e = atual();
    if (ui_lista_tecla(&e->lista, ev, UI_LINHAS_VISIVEIS) || ev->tipo != TECLA_PRESSIONOU) {
        return;
    }
    if (ev->tecla == TECLA_B) {
        ui_voltar();
    } else if (ev->tecla == TECLA_A && e->def->itens[e->lista.sel].acao) {
        e->def->itens[e->lista.sel].acao();
    }
}

static void desenhar(gfx_t *g, uint32_t agora)
{
    estado_t *e = atual();
    ui_titulo(g, e->def->titulo, agora);
    ui_lista_desenhar(g, &e->lista, UI_TITULO_ALTURA, UI_LINHAS_VISIVEIS, item, (void *)e->def, agora);
}

static const tela_t TELA_SUBMENU = {
    .nome = "submenu",
    .tecla = tecla,
    .desenhar = desenhar,
    .abrir = abrir,
};

void submenu_abrir(const submenu_t *def)
{
    proximo = def;
    ui_abrir(&TELA_SUBMENU, NULL);
}
