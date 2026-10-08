#include "componentes.h"

#include <stdio.h>
#include <string.h>

#include "storage.h"

#define MARQUEE_PAUSA_MS 1000
#define MARQUEE_MS_POR_PX 40

uint32_t ui_hash(const char *texto)
{
    uint32_t h = 2166136261u;
    while (texto && *texto) {
        h = (h ^ (uint8_t)*texto++) * 16777619u;
    }
    return h;
}

void ui_truncar(char *dst, int tam, const char *src, int largura)
{
    int max_chars = (largura + 1) / FONTE_5X8.avanco;
    const char *fim = utf8_avancar(src, max_chars);
    int n = (int)(fim - src);
    if (n >= tam) {
        n = tam - 1;
    }
    memcpy(dst, src, (size_t)n);
    dst[n] = '\0';
}

void ui_texto_rolante(gfx_t *g, int x, int y, int largura, const char *texto, gfx_cor_t cor, bool rolar,
                      marquee_t *m, uint32_t agora)
{
    int w = gfx_largura_texto(texto);
    int16_t cx0 = g->clip_x0, cy0 = g->clip_y0, cx1 = g->clip_x1, cy1 = g->clip_y1;
    if (w <= largura) {
        gfx_texto(g, x, y, texto, cor);
        return;
    }
    if (!rolar || !m) {
        char buf[96];
        ui_truncar(buf, sizeof(buf), texto, largura);
        gfx_texto(g, x, y, buf, cor);
        return;
    }
    uint32_t chave = ui_hash(texto);
    if (m->chave != chave) {
        m->chave = chave;
        m->inicio = agora;
    }
    int excesso = w - largura;
    uint32_t ciclo = MARQUEE_PAUSA_MS * 2 + (uint32_t)excesso * MARQUEE_MS_POR_PX;
    uint32_t t = (agora - m->inicio) % ciclo;
    int desloc = 0;
    if (t > MARQUEE_PAUSA_MS) {
        desloc = (int)((t - MARQUEE_PAUSA_MS) / MARQUEE_MS_POR_PX);
        if (desloc > excesso) {
            desloc = excesso;
        }
    }
    gfx_recortar(g, x > cx0 ? x : cx0, cy0, x + largura < cx1 ? x + largura : cx1, cy1);
    gfx_texto(g, x - desloc, y, texto, cor);
    g->clip_x0 = cx0;
    g->clip_y0 = cy0;
    g->clip_x1 = cx1;
    g->clip_y1 = cy1;
}

void ui_titulo(gfx_t *g, const char *texto, uint32_t agora)
{
    static marquee_t mq;
    gfx_retangulo_cheio(g, 0, 0, GFX_LARGURA, UI_TITULO_ALTURA, GFX_BRANCO);
    int w = gfx_largura_texto(texto);
    if (w <= GFX_LARGURA - 4) {
        gfx_texto(g, (GFX_LARGURA - w) / 2, 2, texto, GFX_PRETO);
    } else {
        ui_texto_rolante(g, 2, 2, GFX_LARGURA - 4, texto, GFX_PRETO, true, &mq, agora);
    }
}

#define ABA_CHARS_INATIVA 4
#define ABA_CHARS_ATIVA 10
#define ABA_MARGEM 3

void ui_abas(gfx_t *g, const char *const *nomes, int n, int sel, uint32_t agora)
{
    static marquee_t mq;
    int xs[12], ws[12];
    int total = 0;
    if (n > 12) {
        n = 12;
    }
    for (int i = 0; i < n; i++) {
        int chars = utf8_tamanho(nomes[i]);
        int lim = i == sel ? ABA_CHARS_ATIVA : ABA_CHARS_INATIVA;
        if (chars > lim) {
            chars = lim;
        }
        ws[i] = chars * FONTE_5X8.avanco - 1 + ABA_MARGEM * 2;
        xs[i] = total;
        total += ws[i] - 1; /* abas vizinhas compartilham a borda */
    }
    total++;
    int desloc = 0;
    if (total > GFX_LARGURA) {
        desloc = xs[sel] + ws[sel] / 2 - GFX_LARGURA / 2;
        if (desloc < 0) {
            desloc = 0;
        }
        if (desloc > total - GFX_LARGURA) {
            desloc = total - GFX_LARGURA;
        }
    }
    gfx_linha_h(g, 0, UI_TITULO_ALTURA - 1, GFX_LARGURA, GFX_BRANCO);
    for (int i = 0; i < n; i++) {
        int x = xs[i] - desloc;
        if (x + ws[i] < 0 || x >= GFX_LARGURA) {
            continue;
        }
        bool ativa = i == sel;
        gfx_aba(g, x, 0, ws[i], UI_TITULO_ALTURA - 1, 2, ativa, GFX_BRANCO);
        if (ativa) {
            ui_texto_rolante(g, x + ABA_MARGEM, 2, ws[i] - ABA_MARGEM * 2, nomes[i], GFX_PRETO, true, &mq, agora);
        } else {
            char buf[24];
            ui_truncar(buf, sizeof(buf), nomes[i], ws[i] - ABA_MARGEM * 2);
            gfx_texto(g, x + ABA_MARGEM, 2, buf, GFX_BRANCO);
        }
    }
    /* Setas cheias nas extremidades quando há abas fora da tela */
    if (desloc > 0) {
        gfx_retangulo_cheio(g, 0, 0, 6, UI_TITULO_ALTURA - 1, GFX_PRETO);
        gfx_seta(g, 1, 2, 7, -1, GFX_BRANCO);
    }
    if (desloc < total - GFX_LARGURA) {
        gfx_retangulo_cheio(g, GFX_LARGURA - 6, 0, 6, UI_TITULO_ALTURA - 1, GFX_PRETO);
        gfx_seta(g, GFX_LARGURA - 5, 2, 7, 1, GFX_BRANCO);
    }
}

void ui_rolagem(gfx_t *g, int y0, int altura, int total, int visiveis, int topo)
{
    gfx_linha_v(g, GFX_LARGURA - 1, y0, altura, GFX_PRETO);
    if (total <= visiveis || total <= 0) {
        return;
    }
    int tam = altura * visiveis / total;
    if (tam < 3) {
        tam = 3;
    }
    int y = y0 + (altura - tam) * topo / (total - visiveis);
    gfx_linha_v(g, GFX_LARGURA - 1, y, tam, GFX_BRANCO);
}

static void legendas(gfx_t *g, int x_b, int x_a_fim, int y, int largura, const char *texto_b, const char *texto_a,
                     uint32_t agora)
{
    static marquee_t mq_b, mq_a;
    if (texto_b) {
        ui_texto_rolante(g, x_b, y, largura, texto_b, GFX_BRANCO, true, &mq_b, agora);
    }
    if (texto_a) {
        int w = gfx_largura_texto(texto_a);
        if (w <= largura) {
            gfx_texto(g, x_a_fim - w + 1, y, texto_a, GFX_BRANCO);
        } else {
            ui_texto_rolante(g, x_a_fim - largura + 1, y, largura, texto_a, GFX_BRANCO, true, &mq_a, agora);
        }
    }
}

void ui_rodape(gfx_t *g, const char *texto_b, const char *texto_a, uint32_t agora)
{
    gfx_retangulo_cheio(g, 0, UI_RODAPE_Y, GFX_LARGURA, GFX_ALTURA - UI_RODAPE_Y, GFX_PRETO);
    gfx_linha_h(g, 0, UI_RODAPE_Y - 1, GFX_LARGURA, GFX_BRANCO);
    if (texto_b) {
        ui_icone_modo(g, "button_b", 2, UI_RODAPE_Y, GFX_COPIA, 0);
    }
    if (texto_a) {
        ui_icone_modo(g, "button_a", 116, UI_RODAPE_Y, GFX_COPIA, 0);
    }
    legendas(g, 15, 112, UI_RODAPE_Y + 1, 47, texto_b, texto_a, agora);
}

void ui_rodape_modal(gfx_t *g, int y_linha, const char *texto_b, const char *texto_a, uint32_t agora)
{
    gfx_retangulo_cheio(g, 9, y_linha + 1, 110, 55 - y_linha - 1, GFX_PRETO);
    gfx_linha_h(g, 9, y_linha, 110, GFX_BRANCO);
    if (texto_b) {
        ui_icone_modo(g, "button_b", 11, 45, GFX_COPIA, 0);
    }
    if (texto_a) {
        ui_icone_modo(g, "button_a", 107, 45, GFX_COPIA, 0);
    }
    legendas(g, 24, 104, 46, 38, texto_b, texto_a, agora);
}

void ui_progresso(gfx_t *g, int x, int y, int largura, int altura, int valor, int maximo)
{
    gfx_retangulo(g, x, y, largura, altura, GFX_BRANCO);
    if (maximo <= 0) {
        return;
    }
    if (valor < 0) {
        valor = 0;
    }
    if (valor > maximo) {
        valor = maximo;
    }
    int interno = largura - 4;
    int cheio = interno * valor / maximo;
    if (altura > 4) {
        gfx_retangulo_cheio(g, x + 2, y + 2, cheio, altura - 4, GFX_BRANCO);
    } else {
        gfx_linha_h(g, x + 1, y + altura / 2, (largura - 2) * valor / maximo, GFX_BRANCO);
    }
}

void ui_icone_modo(gfx_t *g, const char *nome, int x, int y, gfx_modo_t modo, unsigned flags)
{
    const gfx_bitmap_t *b = vrom(nome);
    if (b) {
        gfx_bitmap(g, b, x, y, modo, flags);
    }
}

bool ui_icone(gfx_t *g, const char *nome, int x, int y)
{
    const gfx_bitmap_t *b = vrom(nome);
    if (!b) {
        return false;
    }
    gfx_bitmap(g, b, x, y, GFX_OU, 0);
    return true;
}

int ui_paragrafo(gfx_t *g, int x, int y, int largura, int max_linhas, const char *texto, bool centralizar)
{
    int max_chars = (largura + 1) / FONTE_5X8.avanco;
    int linhas = 0;
    const char *p = texto;
    while (*p && linhas < max_linhas) {
        while (*p == ' ') {
            p++;
        }
        /* Avança até max_chars, recuando para o último espaço */
        const char *fim = p, *quebra = NULL;
        int n = 0;
        while (*fim && *fim != '\n' && n < max_chars) {
            const char *ant = fim;
            utf8_proximo(&fim);
            if (*ant == ' ') {
                quebra = ant;
            }
            n++;
        }
        if (*fim && *fim != '\n' && *fim != ' ' && quebra) {
            fim = quebra;
        }
        char buf[64];
        int tam = (int)(fim - p) < (int)sizeof(buf) - 1 ? (int)(fim - p) : (int)sizeof(buf) - 1;
        memcpy(buf, p, (size_t)tam);
        buf[tam] = '\0';
        if (centralizar) {
            gfx_texto_centro(g, x, largura, y + linhas * FONTE_5X8.entrelinha, buf, GFX_BRANCO);
        } else {
            gfx_texto(g, x, y + linhas * FONTE_5X8.entrelinha, buf, GFX_BRANCO);
        }
        linhas++;
        p = fim;
        if (*p == '\n') {
            p++;
        }
    }
    return linhas;
}

void ui_lista_ajustar(ui_lista_t *l, int linhas)
{
    if (l->total <= 0) {
        l->sel = l->topo = 0;
        return;
    }
    if (l->sel >= l->total) {
        l->sel = l->total - 1;
    }
    if (l->sel < 0) {
        l->sel = 0;
    }
    if (l->sel < l->topo) {
        l->topo = l->sel;
    }
    if (l->sel >= l->topo + linhas) {
        l->topo = l->sel - linhas + 1;
    }
    if (l->topo > l->total - linhas) {
        l->topo = l->total - linhas > 0 ? l->total - linhas : 0;
    }
}

void ui_lista_desenhar(gfx_t *g, ui_lista_t *l, int y0, int linhas, ui_item_fn item_fn, void *ctx, uint32_t agora)
{
    ui_lista_ajustar(l, linhas);
    for (int i = 0; i < linhas && l->topo + i < l->total; i++) {
        int indice = l->topo + i;
        int y = y0 + i * UI_LINHA_ALTURA;
        ui_item_t it = {0};
        item_fn(indice, &it, ctx);
        if (it.icone) {
            ui_icone(g, it.icone, 4, y);
        }
        int fim_texto = 123;
        if (it.info[0]) {
            int wi = gfx_largura_texto(it.info);
            gfx_texto(g, 123 - wi + 1, y + 4, it.info, GFX_BRANCO);
            fim_texto = 123 - wi - 4;
        }
        int x_texto = it.icone ? 22 : 6;
        bool sel = indice == l->sel;
        ui_texto_rolante(g, x_texto, y + 4, fim_texto - x_texto, it.texto, GFX_BRANCO, sel, &l->mq, agora);
        if (sel) {
            gfx_retangulo_arred(g, 2, y, 124, UI_LINHA_ALTURA, 2, GFX_BRANCO);
        }
    }
    ui_rolagem(g, y0, linhas * UI_LINHA_ALTURA, l->total, linhas, l->topo);
}

bool ui_lista_tecla(ui_lista_t *l, const evento_tecla_t *ev, int linhas)
{
    if (ev->tipo == TECLA_SOLTOU || l->total <= 0) {
        return false;
    }
    if (ev->tecla == TECLA_CIMA) {
        l->sel = (l->sel - 1 + l->total) % l->total;
    } else if (ev->tecla == TECLA_BAIXO) {
        l->sel = (l->sel + 1) % l->total;
    } else {
        return false;
    }
    ui_lista_ajustar(l, linhas);
    return true;
}
