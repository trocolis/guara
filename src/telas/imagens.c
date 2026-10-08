#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "arquivos.h"
#include "audio.h"
#include "componentes.h"
#include "modal.h"
#include "sdcard.h"
#include "telas.h"

/* Imagens: .bmp da pasta /sdcard/image e visualizador (também usado pelo Cartão SD). */

#define PASTA_IMAGENS SD_RAIZ "/image"

/* ---------------- Visualizador ---------------- */

static gfx_bitmap_t imagem;
static int vx, vy;

static void vis_tecla(const evento_tecla_t *ev)
{
    if (ev->tipo == TECLA_SOLTOU) {
        return;
    }
    int passo = 8;
    switch (ev->tecla) {
    case TECLA_ESQ: vx -= passo; break;
    case TECLA_DIR: vx += passo; break;
    case TECLA_CIMA: vy -= passo; break;
    case TECLA_BAIXO: vy += passo; break;
    case TECLA_B:
    case TECLA_A:
        if (ev->tipo == TECLA_PRESSIONOU) {
            ui_voltar();
        }
        return;
    default: break;
    }
    int max_x = imagem.largura > GFX_LARGURA ? imagem.largura - GFX_LARGURA : 0;
    int max_y = imagem.altura > GFX_ALTURA ? imagem.altura - GFX_ALTURA : 0;
    vx = vx < 0 ? 0 : (vx > max_x ? max_x : vx);
    vy = vy < 0 ? 0 : (vy > max_y ? max_y : vy);
}

static void vis_desenhar(gfx_t *g, uint32_t agora)
{
    (void)agora;
    int x = imagem.largura < GFX_LARGURA ? (GFX_LARGURA - imagem.largura) / 2 : -vx;
    int y = imagem.altura < GFX_ALTURA ? (GFX_ALTURA - imagem.altura) / 2 : -vy;
    gfx_bitmap(g, &imagem, x, y, GFX_COPIA, 0);
}

static void vis_fechar(void)
{
    arquivos_liberar_bmp(&imagem);
}

static const tela_t TELA_VISUALIZADOR = {
    .nome = "visualizador",
    .tecla = vis_tecla,
    .desenhar = vis_desenhar,
    .fechar = vis_fechar,
};

void telas_ver_imagem(const char *caminho)
{
    if (!arquivos_carregar_bmp(caminho, &imagem)) {
        audio_erro();
        ui_alerta("IMAGEM", "images", "Use BMP monocromático (1 bit).", "OK", NULL, NULL);
        return;
    }
    vx = vy = 0;
    ui_abrir(&TELA_VISUALIZADOR, NULL);
}

/* ---------------- Lista ---------------- */

static lista_arquivos_t *itens;
static ui_lista_t lista;

static void recarregar(void)
{
    static const char *const ext[] = {".bmp", NULL};
    free(itens);
    itens = arquivos_listar(PASTA_IMAGENS, ext, false, 64);
    lista.total = itens ? itens->quantidade : 0;
    ui_lista_ajustar(&lista, UI_LINHAS_VISIVEIS);
}

static void caminho_sel(char *buf, int tam)
{
    snprintf(buf, (size_t)tam, "%s/%s", PASTA_IMAGENS, itens->itens[lista.sel].nome);
}

static void item(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    it->icone = "images";
    arquivos_nome_amigavel(itens->itens[i].nome, it->texto, sizeof(it->texto));
    arquivos_formatar_tamanho(itens->itens[i].tamanho, it->info, sizeof(it->info));
}

static void excluir(int r, void *ctx)
{
    (void)ctx;
    if (r == 1) {
        char c[160];
        caminho_sel(c, sizeof(c));
        remove(c);
        recarregar();
    }
}

static void opcao(int r, void *ctx)
{
    (void)ctx;
    char c[160];
    caminho_sel(c, sizeof(c));
    const entrada_t *e = &itens->itens[lista.sel];
    if (r == 0) {
        telas_ver_imagem(c);
    } else if (r == 1) {
        char texto[160];
        gfx_bitmap_t b;
        if (arquivos_carregar_bmp(c, &b)) {
            snprintf(texto, sizeof(texto), "%s\n%dx%d px\n%lu bytes", e->nome, b.largura, b.altura,
                     (unsigned long)e->tamanho);
            arquivos_liberar_bmp(&b);
        } else {
            snprintf(texto, sizeof(texto), "%s\n%lu bytes\nFormato não suportado", e->nome, (unsigned long)e->tamanho);
        }
        ui_detalhes("DETALHES", texto);
    } else if (r == 2) {
        ui_confirmar("EXCLUIR", "images", e->nome, "NAO", "SIM", excluir, NULL);
    }
}

static void tecla(const evento_tecla_t *ev)
{
    if (ui_lista_tecla(&lista, ev, UI_LINHAS_VISIVEIS) || ev->tipo != TECLA_PRESSIONOU) {
        return;
    }
    if (ev->tecla == TECLA_B) {
        ui_voltar();
    } else if (ev->tecla == TECLA_A && lista.total) {
        static const char *const opcoes[] = {"Abrir", "Detalhes", "Excluir"};
        ui_escolha(itens->itens[lista.sel].nome, opcoes, 3, opcao, NULL);
    }
}

static void desenhar(gfx_t *g, uint32_t agora)
{
    ui_titulo(g, "IMAGENS", agora);
    if (!sd_montado()) {
        gfx_texto_centro(g, 0, GFX_LARGURA, 32, "Insira o cartão SD", GFX_BRANCO);
    } else if (lista.total == 0) {
        gfx_texto_centro(g, 0, GFX_LARGURA, 28, "Nenhuma imagem em", GFX_BRANCO);
        gfx_texto_centro(g, 0, GFX_LARGURA, 38, "/image", GFX_BRANCO);
    } else {
        ui_lista_desenhar(g, &lista, UI_TITULO_ALTURA, UI_LINHAS_VISIVEIS, item, NULL, agora);
    }
}

static void abrir(void *param)
{
    (void)param;
    if (!sd_montado()) {
        sd_montar();
    }
    lista = (ui_lista_t){0};
    recarregar();
}

static void fechar(void)
{
    free(itens);
    itens = NULL;
}

const tela_t TELA_IMAGENS = {
    .nome = "imagens",
    .abrir = abrir,
    .fechar = fechar,
    .tecla = tecla,
    .desenhar = desenhar,
};
