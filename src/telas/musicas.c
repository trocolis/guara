#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "arquivos.h"
#include "audio.h"
#include "componentes.h"
#include "midi.h"
#include "modal.h"
#include "sdcard.h"
#include "telas.h"

/* Músicas MIDI de /sdcard/music, tocadas no buzzer em segundo plano. */

#define PASTA_MUSICAS SD_RAIZ "/music"
#define MIDI_MAX (96 * 1024)

/* ---------------- Fonte de música para a task de áudio ---------------- */

typedef struct {
    uint8_t *dados;
    size_t tamanho;
    midi_t midi;
    volatile uint32_t tocado_ms;
    uint32_t total_ms;
    char nome[64];
    char caminho[160];
} faixa_t;

static faixa_t *faixa; /* faixa atual (pertence à task de áudio enquanto toca) */

static bool proxima_nota(void *ctx, nota_t *n)
{
    faixa_t *f = ctx;
    midi_nota_t m;
    if (!midi_proxima(&f->midi, &m)) {
        return false;
    }
    n->freq = m.freq;
    n->ms = m.ms;
    f->tocado_ms += m.ms;
    return true;
}

static void fechar_faixa(void *ctx)
{
    faixa_t *f = ctx;
    if (faixa == f) {
        faixa = NULL;
    }
    free(f->dados);
    free(f);
}

static bool tocar(const char *caminho)
{
    faixa_t *f = calloc(1, sizeof(faixa_t));
    if (!f) {
        return false;
    }
    f->dados = arquivos_ler(caminho, &f->tamanho, MIDI_MAX);
    if (!f->dados || !midi_abrir(&f->midi, f->dados, f->tamanho)) {
        free(f->dados);
        free(f);
        return false;
    }
    f->total_ms = midi_duracao_ms(f->dados, f->tamanho);
    snprintf(f->caminho, sizeof(f->caminho), "%s", caminho);
    const char *base = strrchr(caminho, '/');
    arquivos_nome_amigavel(base ? base + 1 : caminho, f->nome, sizeof(f->nome));
    fonte_musica_t fonte = {proxima_nota, fechar_faixa, f};
    audio_tocar_musica(&fonte);
    faixa = f;
    /* README: Amaldiçoado se cura escutando música */
    if (g_tem_guara && guara_tem(&g_guara, EST_AMALDICOADO)) {
        guara_por_estado(&g_guara, EST_AMALDICOADO, false);
        app_alterou();
    }
    return true;
}

/* ---------------- Lista ---------------- */

static lista_arquivos_t *itens;
static ui_lista_t lista;

static void recarregar(void)
{
    static const char *const ext[] = {".mid", ".midi", NULL};
    free(itens);
    itens = arquivos_listar(PASTA_MUSICAS, ext, false, 64);
    lista.total = itens ? itens->quantidade : 0;
    ui_lista_ajustar(&lista, UI_LINHAS_VISIVEIS);
}

static void caminho_indice(int i, char *buf, int tam)
{
    snprintf(buf, (size_t)tam, "%s/%s", PASTA_MUSICAS, itens->itens[i].nome);
}

static void item(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    it->icone = "tape";
    arquivos_nome_amigavel(itens->itens[i].nome, it->texto, sizeof(it->texto));
    char c[160];
    caminho_indice(i, c, sizeof(c));
    if (faixa && audio_musica_ativa() && strcmp(faixa->caminho, c) == 0) {
        snprintf(it->info, sizeof(it->info), audio_musica_pausada() ? "||" : ">");
    }
}

/* ---------------- Tocador ---------------- */

static void trocar_faixa(int delta)
{
    if (!itens || itens->quantidade == 0) {
        return;
    }
    lista.sel = (lista.sel + delta + itens->quantidade) % itens->quantidade;
    char c[160];
    caminho_indice(lista.sel, c, sizeof(c));
    tocar(c);
}

static void player_tecla(const evento_tecla_t *ev)
{
    if (ev->tipo != TECLA_PRESSIONOU) {
        return;
    }
    switch (ev->tecla) {
    case TECLA_A:
        if (audio_musica_ativa()) {
            audio_pausar_musica(!audio_musica_pausada());
        } else if (faixa == NULL && itens && itens->quantidade) {
            trocar_faixa(0);
        }
        break;
    case TECLA_B:
        ui_voltar(); /* volta à lista sem interromper o áudio */
        break;
    case TECLA_CIMA:
        trocar_faixa(-1);
        break;
    case TECLA_BAIXO:
        trocar_faixa(1);
        break;
    case TECLA_DIR:
        audio_volume(audio_nivel_volume() + 1);
        break;
    case TECLA_ESQ:
        audio_volume(audio_nivel_volume() - 1);
        break;
    default:
        break;
    }
}

static void tempo(char *buf, int tam, uint32_t ms)
{
    snprintf(buf, (size_t)tam, "%lu:%02lu", (unsigned long)(ms / 60000), (unsigned long)(ms / 1000 % 60));
}

static void player_desenhar(gfx_t *g, uint32_t agora)
{
    faixa_t *f = audio_musica_ativa() ? faixa : NULL;
    ui_titulo(g, f ? f->nome : "Parado", agora);
    if (!f) {
        gfx_texto_centro(g, 0, GFX_LARGURA, 30, "A: tocar", GFX_BRANCO);
        return;
    }
    /* Ícone de reprodução (▶) ou pausa (⏸) */
    if (audio_musica_pausada()) {
        gfx_retangulo_cheio(g, 56, 18, 5, 14, GFX_BRANCO);
        gfx_retangulo_cheio(g, 66, 18, 5, 14, GFX_BRANCO);
    } else {
        gfx_seta(g, 58, 18, 15, 1, GFX_BRANCO);
    }
    ui_progresso(g, 8, 38, 112, 6, (int)(f->tocado_ms / 100), (int)(f->total_ms / 100 ? f->total_ms / 100 : 1));
    char a[12], b[12], linha[28];
    tempo(a, sizeof(a), f->tocado_ms);
    tempo(b, sizeof(b), f->total_ms);
    snprintf(linha, sizeof(linha), "%s / %s", a, b);
    gfx_texto_centro(g, 0, GFX_LARGURA, 47, linha, GFX_BRANCO);
    for (int v = 0; v < audio_nivel_volume(); v++) {
        gfx_retangulo_cheio(g, 4 + v * 4, 55 - v * 2, 3, 3 + v * 2, GFX_BRANCO);
    }
}

static const tela_t TELA_PLAYER = {
    .nome = "player",
    .tecla = player_tecla,
    .desenhar = player_desenhar,
};

bool telas_tocar_musica_fundo(const char *caminho)
{
    return tocar(caminho);
}

void telas_tocar_musica(const char *caminho)
{
    if (!tocar(caminho)) {
        audio_erro();
        ui_alerta("MUSICA", "tape", "Arquivo MIDI inválido.", "OK", NULL, NULL);
        return;
    }
    ui_abrir(&TELA_PLAYER, NULL);
}

static void tecla(const evento_tecla_t *ev)
{
    if (ui_lista_tecla(&lista, ev, UI_LINHAS_VISIVEIS) || ev->tipo != TECLA_PRESSIONOU) {
        return;
    }
    if (ev->tecla == TECLA_B) {
        ui_voltar();
    } else if (ev->tecla == TECLA_A && lista.total) {
        char c[160];
        caminho_indice(lista.sel, c, sizeof(c));
        if (faixa && audio_musica_ativa() && strcmp(faixa->caminho, c) == 0) {
            ui_abrir(&TELA_PLAYER, NULL);
        } else {
            telas_tocar_musica(c);
        }
    }
}

static void desenhar(gfx_t *g, uint32_t agora)
{
    ui_titulo(g, "MUSICAS", agora);
    if (!sd_montado()) {
        gfx_texto_centro(g, 0, GFX_LARGURA, 32, "Insira o cartão SD", GFX_BRANCO);
    } else if (lista.total == 0) {
        gfx_texto_centro(g, 0, GFX_LARGURA, 28, "Nenhuma música em", GFX_BRANCO);
        gfx_texto_centro(g, 0, GFX_LARGURA, 38, "/music", GFX_BRANCO);
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

const tela_t TELA_MUSICAS = {
    .nome = "musicas",
    .abrir = abrir,
    .tecla = tecla,
    .desenhar = desenhar,
};
