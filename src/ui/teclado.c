#include <stdio.h>
#include <string.h>

#include "componentes.h"
#include "modal.h"
#include "ui.h"

/*
 * Teclado virtual: 3 linhas de 10 teclas + linha de atalhos
 * (modo, espaço, apagar, concluir). A insere, B cancela.
 */

#define COLUNAS 10
#define LINHAS_CHARS 3
#define CEL_L 12
#define CEL_A 10
#define X0 4
#define Y0 14
#define Y_ATALHOS 46

static const char *const PAGINAS[] = {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ.,-_",
    "abcdefghijklmnopqrstuvwxyz.,-_",
    "1234567890!?@#$%&*()+=/:;'\"<>",
};
static const char *const NOME_PAGINA[] = {"ABC", "abc", "123"};
#define QTD_PAGINAS 3

static const char *const ATALHOS[] = {NULL, "ESPACO", "APAGAR", "OK"};

static struct {
    char titulo[32];
    char texto[72];
    int max;
    int pagina;
    int lin, col; /* lin 3 = atalhos (col 0..3) */
    ui_texto_fn fn;
    void *ctx;
} kb;

static void responder(const char *texto)
{
    ui_texto_fn fn = kb.fn;
    void *ctx = kb.ctx;
    char copia[sizeof(kb.texto)];
    snprintf(copia, sizeof(copia), "%s", texto ? texto : "");
    ui_voltar();
    if (fn) {
        fn(texto ? copia : NULL, ctx);
    }
}

static char char_em(int lin, int col)
{
    const char *p = PAGINAS[kb.pagina];
    int i = lin * COLUNAS + col;
    return i < (int)strlen(p) ? p[i] : ' ';
}

static void inserir(char c)
{
    int n = (int)strlen(kb.texto);
    if (n < kb.max && n < (int)sizeof(kb.texto) - 1) {
        kb.texto[n] = c;
        kb.texto[n + 1] = '\0';
    }
}

static void tecla(const evento_tecla_t *ev)
{
    if (ev->tipo == TECLA_SOLTOU) {
        return;
    }
    switch (ev->tecla) {
    case TECLA_B:
        if (ev->tipo == TECLA_PRESSIONOU) {
            responder(NULL);
        }
        return;
    case TECLA_CIMA:
        kb.lin = (kb.lin + 3) % 4;
        break;
    case TECLA_BAIXO:
        kb.lin = (kb.lin + 1) % 4;
        break;
    case TECLA_ESQ:
        kb.col = kb.lin == 3 ? (kb.col + 3) % 4 : (kb.col + COLUNAS - 1) % COLUNAS;
        break;
    case TECLA_DIR:
        kb.col = kb.lin == 3 ? (kb.col + 1) % 4 : (kb.col + 1) % COLUNAS;
        break;
    case TECLA_A:
        if (kb.lin < 3) {
            inserir(char_em(kb.lin, kb.col));
        } else if (kb.col == 0) {
            kb.pagina = (kb.pagina + 1) % QTD_PAGINAS;
        } else if (kb.col == 1) {
            inserir(' ');
        } else if (kb.col == 2) {
            const char *p = kb.texto + strlen(kb.texto);
            /* apaga um caractere UTF-8 inteiro */
            while (p > kb.texto) {
                p--;
                if (((uint8_t)*p & 0xC0) != 0x80) {
                    break;
                }
            }
            kb.texto[p - kb.texto] = '\0';
        } else if (ev->tipo == TECLA_PRESSIONOU) {
            responder(kb.texto);
            return;
        }
        break;
    default:
        break;
    }
    if (kb.lin == 3 && kb.col > 3) {
        kb.col = 3;
    }
}

static void desenhar(gfx_t *g, uint32_t agora)
{
    gfx_limpar(g);
    /* Barra superior com o texto digitado (mostra o fim se for longo) */
    gfx_retangulo_cheio(g, 0, 0, GFX_LARGURA, UI_TITULO_ALTURA, GFX_BRANCO);
    const char *mostrar = kb.texto[0] ? kb.texto : kb.titulo;
    int n = utf8_tamanho(mostrar);
    if (n > 19) {
        mostrar = utf8_avancar(mostrar, n - 19);
    }
    int w = gfx_texto(g, 2, 2, mostrar, GFX_PRETO);
    if (kb.texto[0] && (agora / 400) % 2) {
        gfx_linha_v(g, 2 + w + 1, 2, 8, GFX_PRETO);
    }

    for (int lin = 0; lin < LINHAS_CHARS; lin++) {
        for (int col = 0; col < COLUNAS; col++) {
            int x = X0 + col * CEL_L, y = Y0 + lin * CEL_A;
            char s[2] = {char_em(lin, col), 0};
            bool sel = kb.lin == lin && kb.col == col;
            if (sel) {
                gfx_retangulo_arred_cheio(g, x, y, CEL_L, CEL_A, 2, GFX_BRANCO);
            }
            gfx_texto(g, x + 4, y + 1, s, sel ? GFX_PRETO : GFX_BRANCO);
        }
    }
    for (int i = 0; i < 4; i++) {
        int x = i * 32, y = Y_ATALHOS;
        const char *r = i == 0 ? NOME_PAGINA[(kb.pagina + 1) % QTD_PAGINAS] : ATALHOS[i];
        bool sel = kb.lin == 3 && kb.col == i;
        if (sel) {
            gfx_retangulo_arred_cheio(g, x + 1, y, 30, 11, 2, GFX_BRANCO);
        } else {
            gfx_retangulo_arred(g, x + 1, y, 30, 11, 2, GFX_BRANCO);
        }
        char buf[8];
        ui_truncar(buf, sizeof(buf), r, 28);
        gfx_texto_centro(g, x + 1, 30, y + 2, buf, sel ? GFX_PRETO : GFX_BRANCO);
    }
}

static const tela_t TELA_TECLADO = {
    .nome = "teclado",
    .tecla = tecla,
    .desenhar = desenhar,
};

void ui_teclado(const char *titulo, const char *inicial, int tamanho_max, ui_texto_fn fn, void *ctx)
{
    memset(&kb, 0, sizeof(kb));
    snprintf(kb.titulo, sizeof(kb.titulo), "%s", titulo ? titulo : "");
    snprintf(kb.texto, sizeof(kb.texto), "%s", inicial ? inicial : "");
    kb.max = tamanho_max > 0 && tamanho_max < (int)sizeof(kb.texto) ? tamanho_max : (int)sizeof(kb.texto) - 1;
    kb.fn = fn;
    kb.ctx = ctx;
    ui_abrir(&TELA_TECLADO, NULL);
}
