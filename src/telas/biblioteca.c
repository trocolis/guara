#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app.h"
#include "arquivos.h"
#include "audio.h"
#include "componentes.h"
#include "ini.h"
#include "modal.h"
#include "sdcard.h"
#include "telas.h"

/*
 * Biblioteca (/sdcard/library): .txt simples ou livros (pasta com book.ini,
 * book.txt e imagens 1-bit de até 16x16 referenciadas como {{imagem.bmp}}).
 */

#define PASTA_BIBLIOTECA SD_RAIZ "/library"
#define TEXTO_MAX (64 * 1024)
#define MAX_LINHAS 4000
#define CHARS_LINHA 20

/* ---------------- Leitor ---------------- */

typedef struct {
    uint32_t inicio;
    uint16_t bytes;
    uint8_t imagem; /* 1 = linha de imagem {{...}} */
} linha_t;

static struct {
    char *texto;
    linha_t *linhas;
    int n_linhas;
    int topo;
    char pasta[160];
    char titulo[48];
    uint32_t rolou_em;
    gfx_bitmap_t imgs[4];
    char nomes_imgs[4][32];
} leitor;

static void adicionar_linha(uint32_t ini, int bytes, bool imagem)
{
    if (leitor.n_linhas < MAX_LINHAS) {
        leitor.linhas[leitor.n_linhas++] = (linha_t){ini, (uint16_t)bytes, imagem};
    }
}

/* Quebra o texto em linhas de até 20 caracteres, preferindo espaços. */
static void paginar(void)
{
    const char *t = leitor.texto;
    const char *p = t;
    while (*p) {
        const char *fim_par = strchr(p, '\n');
        if (!fim_par) {
            fim_par = p + strlen(p);
        }
        const char *abre = strstr(p, "{{");
        if (abre && abre < fim_par) {
            const char *fecha = strstr(abre, "}}");
            if (fecha && fecha < fim_par) {
                adicionar_linha((uint32_t)(abre + 2 - t), (int)(fecha - abre - 2), true);
                p = fecha + 2;
                while (*p == ' ' || *p == '\r') {
                    p++;
                }
                if (*p == '\n') {
                    p++;
                }
                continue;
            }
        }
        if (p == fim_par || (p + 1 == fim_par && *p == '\r')) {
            adicionar_linha((uint32_t)(p - t), 0, false);
        }
        while (p < fim_par && *p != '\r') {
            const char *q = p, *quebra = NULL;
            int n = 0;
            while (q < fim_par && *q != '\r' && n < CHARS_LINHA) {
                if (*q == ' ') {
                    quebra = q;
                }
                utf8_proximo(&q);
                n++;
            }
            if (q < fim_par && *q != ' ' && *q != '\r' && quebra) {
                q = quebra;
            }
            adicionar_linha((uint32_t)(p - t), (int)(q - p), false);
            p = q;
            while (p < fim_par && *p == ' ') {
                p++;
            }
        }
        p = *fim_par ? fim_par + 1 : fim_par;
    }
}

static const gfx_bitmap_t *imagem_livro(const char *nome, int n)
{
    char chave[32];
    snprintf(chave, sizeof(chave), "%.*s", n < 31 ? n : 31, nome);
    for (int i = 0; i < 4; i++) {
        if (leitor.imgs[i].dados && strcmp(leitor.nomes_imgs[i], chave) == 0) {
            return &leitor.imgs[i];
        }
    }
    for (int i = 0; i < 4; i++) {
        if (!leitor.imgs[i].dados) {
            char c[200];
            snprintf(c, sizeof(c), "%s/%s", leitor.pasta, chave);
            if (arquivos_carregar_bmp(c, &leitor.imgs[i])) {
                if (leitor.imgs[i].largura > 16 || leitor.imgs[i].altura > 16) {
                    arquivos_liberar_bmp(&leitor.imgs[i]); /* README: máximo 16x16 */
                    return NULL;
                }
                snprintf(leitor.nomes_imgs[i], sizeof(leitor.nomes_imgs[i]), "%s", chave);
                return &leitor.imgs[i];
            }
            return NULL;
        }
    }
    return NULL;
}

#define ALTURA_LINHA 9
#define ALTURA_IMG 18

static int altura(int i)
{
    return leitor.linhas[i].imagem ? ALTURA_IMG : ALTURA_LINHA;
}

static void leitor_tecla(const evento_tecla_t *ev)
{
    if (ev->tipo == TECLA_SOLTOU) {
        return;
    }
    int antes = leitor.topo;
    switch (ev->tecla) {
    case TECLA_CIMA: leitor.topo--; break;
    case TECLA_BAIXO: leitor.topo++; break;
    case TECLA_ESQ: leitor.topo -= 6; break;
    case TECLA_DIR: leitor.topo += 6; break;
    case TECLA_B:
        if (ev->tipo == TECLA_PRESSIONOU) {
            ui_voltar();
        }
        return;
    default: break;
    }
    if (leitor.topo > leitor.n_linhas - 1) {
        leitor.topo = leitor.n_linhas - 1;
    }
    if (leitor.topo < 0) {
        leitor.topo = 0;
    }
    if (antes == 0 && leitor.topo > 0) {
        leitor.rolou_em = app_ms();
    }
}

static void leitor_desenhar(gfx_t *g, uint32_t agora)
{
    /* A barra de título some suavemente quando o texto rola para baixo */
    int titulo_h = UI_TITULO_ALTURA;
    if (leitor.topo > 0) {
        uint32_t t = agora - leitor.rolou_em;
        titulo_h = t >= 240 ? 0 : UI_TITULO_ALTURA - (int)t / 20;
    }
    int y = titulo_h + 1;
    for (int i = leitor.topo; i < leitor.n_linhas && y < GFX_ALTURA; i++) {
        linha_t *l = &leitor.linhas[i];
        if (l->imagem) {
            const gfx_bitmap_t *b = imagem_livro(leitor.texto + l->inicio, l->bytes);
            if (b) {
                gfx_bitmap(g, b, (GFX_LARGURA - b->largura) / 2, y + 1, GFX_COPIA, 0);
            }
        } else if (l->bytes) {
            char buf[96];
            int n = l->bytes < (int)sizeof(buf) - 1 ? l->bytes : (int)sizeof(buf) - 1;
            memcpy(buf, leitor.texto + l->inicio, (size_t)n);
            buf[n] = '\0';
            gfx_texto(g, 2, y, buf, GFX_BRANCO);
        }
        y += altura(i);
    }
    if (titulo_h > 0) {
        gfx_recortar(g, 0, 0, GFX_LARGURA, titulo_h);
        gfx_t *gg = g;
        ui_titulo(gg, leitor.titulo, agora);
        gfx_sem_recorte(g);
    }
    ui_rolagem(g, titulo_h, GFX_ALTURA - titulo_h, leitor.n_linhas, 6, leitor.topo > leitor.n_linhas - 6 ? leitor.n_linhas - 6 : leitor.topo);
}

static void leitor_fechar(void)
{
    free(leitor.texto);
    free(leitor.linhas);
    for (int i = 0; i < 4; i++) {
        arquivos_liberar_bmp(&leitor.imgs[i]);
    }
    memset(&leitor, 0, sizeof(leitor));
}

static const tela_t TELA_LEITOR = {
    .nome = "leitor",
    .tecla = leitor_tecla,
    .desenhar = leitor_desenhar,
    .fechar = leitor_fechar,
};

static void abrir_leitor(const char *arquivo, const char *pasta, const char *titulo)
{
    size_t tam;
    leitor_fechar();
    leitor.texto = (char *)arquivos_ler(arquivo, &tam, TEXTO_MAX);
    leitor.linhas = malloc(sizeof(linha_t) * MAX_LINHAS);
    if (!leitor.texto || !leitor.linhas) {
        leitor_fechar();
        audio_erro();
        ui_alerta("BIBLIOTECA", "book", "Não foi possível abrir (máx. 64 KB).", "OK", NULL, NULL);
        return;
    }
    snprintf(leitor.pasta, sizeof(leitor.pasta), "%s", pasta);
    snprintf(leitor.titulo, sizeof(leitor.titulo), "%s", titulo);
    paginar();
    ui_abrir(&TELA_LEITOR, NULL);
}

void telas_ler_texto(const char *caminho)
{
    char pasta[160], titulo[48];
    snprintf(pasta, sizeof(pasta), "%s", caminho);
    char *barra = strrchr(pasta, '/');
    if (barra) {
        *barra = '\0';
    }
    const char *base = strrchr(caminho, '/');
    arquivos_nome_amigavel(base ? base + 1 : caminho, titulo, sizeof(titulo));
    abrir_leitor(caminho, pasta, titulo);
}

/* ---------------- Lista ---------------- */

typedef struct {
    char nome[64];
    char titulo[48];
    char icone[32];
    bool livro;
} obra_t;

static obra_t *obras;
static ui_lista_t lista;

static void recarregar(void)
{
    static const char *const ext[] = {".txt", NULL};
    free(obras);
    obras = NULL;
    lista.total = 0;
    lista_arquivos_t *l = arquivos_listar(PASTA_BIBLIOTECA, ext, true, 64);
    if (!l) {
        return;
    }
    obras = calloc((size_t)(l->quantidade ? l->quantidade : 1), sizeof(obra_t));
    for (int i = 0; obras && i < l->quantidade; i++) {
        obra_t *o = &obras[lista.total];
        const entrada_t *e = &l->itens[i];
        snprintf(o->nome, sizeof(o->nome), "%s", e->nome);
        if (e->pasta) {
            char ini_c[200];
            size_t tam;
            snprintf(ini_c, sizeof(ini_c), "%s/%s/book.ini", PASTA_BIBLIOTECA, e->nome);
            char *ini = (char *)arquivos_ler(ini_c, &tam, 4096);
            if (!ini) {
                continue; /* pasta sem book.ini não é livro */
            }
            o->livro = true;
            if (!ini_texto(ini, "BOOK", "title", o->titulo, sizeof(o->titulo)) &&
                !ini_texto(ini, NULL, "titulo", o->titulo, sizeof(o->titulo))) {
                arquivos_nome_amigavel(e->nome, o->titulo, sizeof(o->titulo));
            }
            snprintf(o->icone, sizeof(o->icone), "book");
            free(ini);
        } else {
            arquivos_nome_amigavel(e->nome, o->titulo, sizeof(o->titulo));
            snprintf(o->icone, sizeof(o->icone), "txt");
        }
        lista.total++;
    }
    free(l);
}

static void item(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    it->icone = obras[i].icone;
    snprintf(it->texto, sizeof(it->texto), "%s", obras[i].titulo);
}

static void tecla(const evento_tecla_t *ev)
{
    if (ui_lista_tecla(&lista, ev, UI_LINHAS_VISIVEIS) || ev->tipo != TECLA_PRESSIONOU) {
        return;
    }
    if (ev->tecla == TECLA_B) {
        ui_voltar();
    } else if (ev->tecla == TECLA_A && lista.total) {
        obra_t *o = &obras[lista.sel];
        char arquivo[200], pasta[160];
        if (o->livro) {
            snprintf(pasta, sizeof(pasta), "%s/%s", PASTA_BIBLIOTECA, o->nome);
            snprintf(arquivo, sizeof(arquivo), "%s/book.txt", pasta);
        } else {
            snprintf(pasta, sizeof(pasta), "%s", PASTA_BIBLIOTECA);
            snprintf(arquivo, sizeof(arquivo), "%s/%s", PASTA_BIBLIOTECA, o->nome);
        }
        abrir_leitor(arquivo, pasta, o->titulo);
    }
}

static void desenhar(gfx_t *g, uint32_t agora)
{
    ui_titulo(g, "BIBLIOTECA", agora);
    if (!sd_montado()) {
        gfx_texto_centro(g, 0, GFX_LARGURA, 32, "Insira o cartão SD", GFX_BRANCO);
    } else if (lista.total == 0) {
        gfx_texto_centro(g, 0, GFX_LARGURA, 28, "Nada em /library", GFX_BRANCO);
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
    free(obras);
    obras = NULL;
}

const tela_t TELA_BIBLIOTECA = {
    .nome = "biblioteca",
    .abrir = abrir,
    .fechar = fechar,
    .tecla = tecla,
    .desenhar = desenhar,
};
