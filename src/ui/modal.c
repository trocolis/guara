#include "modal.h"

#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "componentes.h"
#include "ui.h"

#define MX 8
#define MY 8
#define MW 112
#define MH 48
#define LINHA_RODAPE 43
#define MAX_OPCOES 8

typedef enum { M_ALERTA, M_CONFIRMAR, M_ESCOLHA, M_QUANTIDADE, M_DETALHES } tipo_modal_t;

typedef struct {
    tipo_modal_t tipo;
    char titulo[40];
    char icone[32];
    char texto[256];
    char texto_a[16];
    char texto_b[16];
    const char *opcoes[MAX_OPCOES];
    char opcoes_buf[MAX_OPCOES][24];
    int n_opcoes;
    ui_lista_t lista;
    int minimo, maximo, valor;
    int rolagem, linhas_texto;
    ui_resposta_fn fn;
    void *ctx;
} modal_t;

static modal_t modais[16];
static modal_t novo;

static modal_t *atual(void)
{
    return &modais[ui_camada_atual() & 15];
}

static void abrir(void *param)
{
    (void)param;
    *atual() = novo;
}

static void responder(int resposta)
{
    modal_t m = *atual();
    ui_voltar();
    if (m.fn) {
        m.fn(resposta, m.ctx);
    }
}

static void tecla(const evento_tecla_t *ev)
{
    modal_t *m = atual();
    if (ev->tipo == TECLA_SOLTOU) {
        return;
    }
    if (ev->tipo == TECLA_PRESSIONOU && ev->tecla == TECLA_B) {
        responder(-1);
        return;
    }
    switch (m->tipo) {
    case M_ALERTA:
    case M_CONFIRMAR:
        if (ev->tecla == TECLA_A && ev->tipo == TECLA_PRESSIONOU) {
            responder(1);
        }
        break;
    case M_ESCOLHA:
        if (ui_lista_tecla(&m->lista, ev, 3)) {
            break;
        }
        if (ev->tecla == TECLA_A && ev->tipo == TECLA_PRESSIONOU) {
            responder(m->lista.sel);
        }
        break;
    case M_QUANTIDADE: {
        int passo = ev->tipo == TECLA_REPETIU ? 5 : 1;
        if (ev->tecla == TECLA_CIMA || ev->tecla == TECLA_DIR) {
            m->valor = m->valor + passo > m->maximo ? (ev->tipo == TECLA_REPETIU ? m->maximo : m->minimo) : m->valor + passo;
        } else if (ev->tecla == TECLA_BAIXO || ev->tecla == TECLA_ESQ) {
            m->valor = m->valor - passo < m->minimo ? (ev->tipo == TECLA_REPETIU ? m->minimo : m->maximo) : m->valor - passo;
        } else if (ev->tecla == TECLA_A && ev->tipo == TECLA_PRESSIONOU) {
            responder(m->valor);
        }
        break;
    }
    case M_DETALHES:
        if (ev->tecla == TECLA_CIMA && m->rolagem > 0) {
            m->rolagem--;
        } else if (ev->tecla == TECLA_BAIXO && m->rolagem + 3 < m->linhas_texto) {
            m->rolagem++;
        } else if (ev->tecla == TECLA_A && ev->tipo == TECLA_PRESSIONOU) {
            responder(1);
        }
        break;
    }
}

static void moldura(gfx_t *g, const char *titulo, uint32_t agora)
{
    static marquee_t mq;
    gfx_retangulo_cheio(g, MX - 2, MY - 2, MW + 4, MH + 4, GFX_PRETO);
    gfx_retangulo(g, MX, MY, MW, MH, GFX_BRANCO);
    int w = gfx_largura_texto(titulo);
    if (w <= MW - 6) {
        gfx_texto(g, MX + (MW - w) / 2, MY + 2, titulo, GFX_BRANCO);
    } else {
        ui_texto_rolante(g, MX + 3, MY + 2, MW - 6, titulo, GFX_BRANCO, true, &mq, agora);
    }
    gfx_linha_h(g, MX, MY + 11, MW, GFX_BRANCO);
}

static void item_opcao(int i, ui_item_t *it, void *ctx)
{
    modal_t *m = ctx;
    snprintf(it->texto, sizeof(it->texto), "%s", m->opcoes[i]);
}

/* Contagem de linhas de um parágrafo, desenhando-o fora da tela. */
static int contar_linhas(const char *texto, int largura)
{
    static gfx_t rascunho;
    gfx_iniciar(&rascunho);
    gfx_recortar(&rascunho, 0, 0, 0, 0);
    return ui_paragrafo(&rascunho, 0, 0, largura, 64, texto, false);
}

static void desenhar(gfx_t *g, uint32_t agora)
{
    modal_t *m = atual();
    moldura(g, m->titulo, agora);
    int corpo_y = MY + 13;
    switch (m->tipo) {
    case M_ALERTA:
    case M_CONFIRMAR: {
        int x = MX + 4, largura = MW - 8;
        if (m->icone[0] && ui_icone(g, m->icone, MX + 4, corpo_y + 2)) {
            x = MX + 24;
            largura = MW - 28;
        }
        int linhas = m->texto[0] ? (gfx_largura_texto(m->texto) > largura ? 2 : 1) : 0;
        int y = corpo_y + (LINHA_RODAPE - corpo_y - linhas * 9) / 2;
        ui_paragrafo(g, x, y, largura, 2, m->texto, true);
        ui_rodape_modal(g, LINHA_RODAPE, m->texto_b[0] ? m->texto_b : NULL, m->texto_a, agora);
        break;
    }
    case M_ESCOLHA: {
        ui_lista_ajustar(&m->lista, 3);
        for (int i = 0; i < 3 && m->lista.topo + i < m->n_opcoes; i++) {
            int idx = m->lista.topo + i, y = corpo_y + i * 11;
            ui_item_t it = {0};
            item_opcao(idx, &it, m);
            bool sel = idx == m->lista.sel;
            ui_texto_rolante(g, MX + 6, y + 2, MW - 16, it.texto, GFX_BRANCO, sel, &m->lista.mq, agora);
            if (sel) {
                gfx_retangulo_arred(g, MX + 2, y, MW - 7, 11, 2, GFX_BRANCO);
            }
        }
        if (m->n_opcoes > 3) {
            int alt = 33, tam = alt * 3 / m->n_opcoes;
            int y = corpo_y + (alt - tam) * m->lista.topo / (m->n_opcoes - 3);
            gfx_linha_v(g, MX + MW - 3, y, tam, GFX_BRANCO);
        }
        break;
    }
    case M_QUANTIDADE: {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", m->valor);
        int w = gfx_largura_texto(buf) * 2 + 1;
        int x = MX + (MW - w) / 2;
        gfx_texto_escala(g, x, corpo_y + 6, buf, GFX_BRANCO, 2);
        gfx_seta(g, x - 10, corpo_y + 9, 9, -1, GFX_BRANCO);
        gfx_seta(g, x + w + 5, corpo_y + 9, 9, 1, GFX_BRANCO);
        ui_rodape_modal(g, LINHA_RODAPE, m->texto_b, m->texto_a, agora);
        break;
    }
    case M_DETALHES: {
        gfx_t *gg = g;
        int16_t cy0 = gg->clip_y0, cy1 = gg->clip_y1;
        gfx_recortar(gg, MX + 1, corpo_y, MX + MW - 1, MY + MH - 1);
        ui_paragrafo(gg, MX + 4, corpo_y + 1 - m->rolagem * 9, MW - 12, 64, m->texto, false);
        gg->clip_y0 = cy0;
        gg->clip_y1 = cy1;
        gfx_sem_recorte(gg);
        if (m->linhas_texto > 3) {
            int alt = 33, tam = alt * 3 / m->linhas_texto;
            int y = corpo_y + (alt - tam) * m->rolagem / (m->linhas_texto - 3);
            gfx_linha_v(g, MX + MW - 3, y, tam, GFX_BRANCO);
        }
        break;
    }
    }
}

static const tela_t TELA_MODAL = {
    .nome = "modal",
    .modal = true,
    .abrir = abrir,
    .tecla = tecla,
    .desenhar = desenhar,
};

static void preparar(tipo_modal_t tipo, const char *titulo, ui_resposta_fn fn, void *ctx)
{
    memset(&novo, 0, sizeof(novo));
    novo.tipo = tipo;
    snprintf(novo.titulo, sizeof(novo.titulo), "%s", titulo ? titulo : "");
    novo.fn = fn;
    novo.ctx = ctx;
}

void ui_alerta(const char *titulo, const char *icone, const char *mensagem, const char *texto_a, ui_resposta_fn fn,
               void *ctx)
{
    preparar(M_ALERTA, titulo, fn, ctx);
    snprintf(novo.icone, sizeof(novo.icone), "%s", icone ? icone : "");
    snprintf(novo.texto, sizeof(novo.texto), "%s", mensagem ? mensagem : "");
    snprintf(novo.texto_a, sizeof(novo.texto_a), "%s", texto_a ? texto_a : "OK");
    ui_abrir(&TELA_MODAL, NULL);
}

void ui_confirmar(const char *titulo, const char *icone, const char *mensagem, const char *texto_nao,
                  const char *texto_sim, ui_resposta_fn fn, void *ctx)
{
    preparar(M_CONFIRMAR, titulo, fn, ctx);
    snprintf(novo.icone, sizeof(novo.icone), "%s", icone ? icone : "");
    snprintf(novo.texto, sizeof(novo.texto), "%s", mensagem ? mensagem : "");
    snprintf(novo.texto_a, sizeof(novo.texto_a), "%s", texto_sim ? texto_sim : "SIM");
    snprintf(novo.texto_b, sizeof(novo.texto_b), "%s", texto_nao ? texto_nao : "NAO");
    ui_abrir(&TELA_MODAL, NULL);
}

void ui_escolha(const char *titulo, const char *const *opcoes, int quantidade, ui_resposta_fn fn, void *ctx)
{
    preparar(M_ESCOLHA, titulo, fn, ctx);
    novo.n_opcoes = quantidade > MAX_OPCOES ? MAX_OPCOES : quantidade;
    for (int i = 0; i < novo.n_opcoes; i++) {
        snprintf(novo.opcoes_buf[i], sizeof(novo.opcoes_buf[i]), "%s", opcoes[i]);
    }
    novo.lista.total = novo.n_opcoes;
    ui_abrir(&TELA_MODAL, NULL);
    /* os ponteiros precisam apontar para a cópia já instalada na camada */
    modal_t *m = &modais[ui_profundidade() - 1];
    for (int i = 0; i < m->n_opcoes; i++) {
        m->opcoes[i] = m->opcoes_buf[i];
    }
}

void ui_quantidade(const char *titulo, int minimo, int maximo, int inicial, ui_resposta_fn fn, void *ctx)
{
    preparar(M_QUANTIDADE, titulo, fn, ctx);
    novo.minimo = minimo;
    novo.maximo = maximo;
    novo.valor = inicial;
    snprintf(novo.texto_a, sizeof(novo.texto_a), "OK");
    snprintf(novo.texto_b, sizeof(novo.texto_b), "VOLTAR");
    ui_abrir(&TELA_MODAL, NULL);
}

void ui_detalhes(const char *titulo, const char *texto)
{
    preparar(M_DETALHES, titulo, NULL, NULL);
    snprintf(novo.texto, sizeof(novo.texto), "%s", texto);
    novo.linhas_texto = contar_linhas(novo.texto, MW - 12);
    ui_abrir(&TELA_MODAL, NULL);
}
