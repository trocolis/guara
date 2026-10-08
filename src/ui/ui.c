#include "ui.h"

#include <stddef.h>

#define PROFUNDIDADE_MAX 16

static const tela_t *pilha[PROFUNDIDADE_MAX];
static int topo = -1;
static int camada;

int ui_camada_atual(void)
{
    return camada;
}

void ui_abrir(const tela_t *t, void *param)
{
    if (topo + 1 >= PROFUNDIDADE_MAX) {
        return;
    }
    pilha[++topo] = t;
    camada = topo;
    if (t->abrir) {
        t->abrir(param);
    }
}

static void desempilhar(void)
{
    if (topo < 0) {
        return;
    }
    camada = topo;
    const tela_t *t = pilha[topo--];
    if (t->fechar) {
        t->fechar();
    }
}

void ui_voltar(void)
{
    if (topo <= 0) {
        return; /* a tela base (principal) nunca sai */
    }
    desempilhar();
    camada = topo;
    if (topo >= 0 && pilha[topo]->retomar) {
        pilha[topo]->retomar();
    }
}

void ui_substituir(const tela_t *t, void *param)
{
    desempilhar();
    ui_abrir(t, param);
}

void ui_voltar_ate(const tela_t *t)
{
    while (topo > 0 && pilha[topo] != t) {
        desempilhar();
    }
    camada = topo;
    if (topo >= 0 && pilha[topo]->retomar) {
        pilha[topo]->retomar();
    }
}

void ui_limpar_pilha(const tela_t *base, void *param)
{
    while (topo >= 0) {
        desempilhar();
    }
    ui_abrir(base, param);
}

const tela_t *ui_topo(void)
{
    return topo >= 0 ? pilha[topo] : NULL;
}

int ui_profundidade(void)
{
    return topo + 1;
}

void ui_processar_tecla(const evento_tecla_t *ev)
{
    const tela_t *t = ui_topo();
    camada = topo;
    if (t && t->tecla) {
        t->tecla(ev);
    }
}

void ui_desenhar(gfx_t *g, uint32_t agora)
{
    if (topo < 0) {
        return;
    }
    int base = topo;
    while (base > 0 && pilha[base]->modal) {
        base--;
    }
    gfx_limpar(g);
    for (int i = base; i <= topo; i++) {
        gfx_sem_recorte(g);
        camada = i;
        if (pilha[i]->desenhar) {
            pilha[i]->desenhar(g, agora);
        }
    }
    gfx_sem_recorte(g);
}

int ui_fps(void)
{
    const tela_t *t = ui_topo();
    return t && t->fps ? t->fps : UI_FPS_PADRAO;
}
