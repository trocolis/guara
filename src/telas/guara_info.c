#include <stdio.h>

#include "app.h"
#include "componentes.h"
#include "regras.h"
#include "sprite.h"
#include "telas.h"

/* Tela do Guará: abas GUARA, ESTADO, NIVEL e NATUREZA (README > Menu principal > Guará). */

static const char *const ABAS[] = {"GUARA", "ESTADO", "NIVEL", "NATUREZA"};
static int aba;

static void tecla(const evento_tecla_t *ev)
{
    if (ev->tipo == TECLA_SOLTOU) {
        return;
    }
    if (ev->tecla == TECLA_ESQ) {
        aba = (aba + 3) % 4;
    } else if (ev->tecla == TECLA_DIR) {
        aba = (aba + 1) % 4;
    } else if (ev->tecla == TECLA_B && ev->tipo == TECLA_PRESSIONOU) {
        ui_voltar();
    }
}

static void aba_guara(gfx_t *g, uint32_t agora)
{
    const guara_t *gu = &g_guara;
    char buf[32];
    sprite_guara(g, gu->cor, gu->cabeca, gu->estagio, OLHO_NORMAL, 10, 50, sprite_quadro(agora), 0);

    ui_icone(g, "id", 22, 13);
    gfx_texto(g, 40, 13, gu->nome, GFX_BRANCO);
    snprintf(buf, sizeof(buf), "ID: %08lX", (unsigned long)gu->uid);
    gfx_texto(g, 40, 22, buf, GFX_BRANCO);

    ui_icone(g, "level", 22, 31);
    snprintf(buf, sizeof(buf), "NV: %d | XP: %lu", gu->nivel, (unsigned long)gu->xp);
    gfx_texto(g, 40, 31, buf, GFX_BRANCO);
    ui_progresso(g, 40, 40, 84, 5, (int)gu->xp, (int)guara_xp_proximo(gu->nivel));

    ui_icone(g, "calendar", 22, 47);
    snprintf(buf, sizeof(buf), "Dias: %lu", (unsigned long)(gu->idade_min / 1440));
    gfx_texto(g, 40, 47, buf, GFX_BRANCO);
    snprintf(buf, sizeof(buf), "Horas: %02lu:%02lu", (unsigned long)(gu->idade_min % 1440 / 60),
             (unsigned long)(gu->idade_min % 60));
    gfx_texto(g, 40, 56, buf, GFX_BRANCO);
}

static void celula(gfx_t *g, int col, int lin, const char *icone, const char *texto, int valor, int maximo)
{
    int x = col * 64, y = 13 + lin * 17;
    ui_icone(g, icone, x + 1, y);
    gfx_texto(g, x + 19, y, texto, GFX_BRANCO);
    if (maximo > 0) {
        ui_progresso(g, x + 19, y + 10, 42, 5, valor, maximo);
    }
}

static void aba_estado(gfx_t *g)
{
    const guara_t *gu = &g_guara;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", gu->hp);
    celula(g, 0, 0, "hp", buf, gu->hp, gu->hp_max);
    snprintf(buf, sizeof(buf), "%d", gu->mp);
    celula(g, 1, 0, "mp", buf, gu->mp, gu->mp_max);
    snprintf(buf, sizeof(buf), "%d%%", gu->energia / 100);
    celula(g, 0, 1, "energy", buf, gu->energia, VITAL_MAX);
    snprintf(buf, sizeof(buf), "%d%%", gu->humor / 100);
    celula(g, 1, 1, "humor", buf, gu->humor, VITAL_MAX);
    snprintf(buf, sizeof(buf), "%d%%", gu->alimentacao / 100);
    celula(g, 0, 2, "hamburger", buf, gu->alimentacao, VITAL_MAX);
    snprintf(buf, sizeof(buf), "%d%%", gu->saude / 100);
    celula(g, 1, 2, "health", buf, gu->saude, VITAL_MAX);
}

static void aba_nivel(gfx_t *g)
{
    static const char *const icones[] = {"attack", "defense", "strength", "agility", "intelligence", "luck"};
    static const char *const siglas[] = {"ATK", "DEF", "FOR", "AGI", "INT", "SOR"};
    for (int i = 0; i < QTD_ATRIBUTOS; i++) {
        int x = (i % 2) * 64, y = 13 + (i / 2) * 17;
        char buf[8];
        ui_icone(g, icones[i], x + 1, y);
        gfx_texto(g, x + 19, y, siglas[i], GFX_BRANCO);
        snprintf(buf, sizeof(buf), "%d", guara_atributo(&g_guara, (alvo_efeito_t)(ATR_ATK + i)));
        gfx_texto(g, x + 19, y + 9, buf, GFX_BRANCO);
    }
}

static void aba_natureza(gfx_t *g)
{
    const guara_t *gu = &g_guara;
    ui_icone(g, natureza_icone(gu->natureza), 1, 13);
    gfx_texto(g, 19, 17, natureza_nome(gu->natureza), GFX_BRANCO);

    if (!guara_afinidade_ativa(gu)) {
        /* Natureza secundária: nascimento + afinidade que a gerou */
        unsigned e = natureza_elementos(gu->natureza) & ~(1u << gu->natureza_nascimento);
        int origem = NAT_NEUTRA;
        for (int n = NAT_FOGO; n <= NAT_AR; n++) {
            if (e & (1u << n)) {
                origem = n;
            }
        }
        ui_icone(g, natureza_icone(gu->natureza_nascimento), 36, 38);
        gfx_texto(g, 61, 42, "+", GFX_BRANCO);
        ui_icone(g, natureza_icone(origem), 76, 38);
        return;
    }
    int celula_i = 0;
    for (int n = NAT_FOGO; n <= NAT_NEUTRA; n++) {
        if (n == gu->natureza_nascimento) {
            continue;
        }
        int x = (celula_i % 2) * 64, y = 30 + (celula_i / 2) * 17;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", gu->afinidades[n] / 100);
        ui_icone(g, natureza_icone((natureza_t)n), x + 1, y);
        gfx_texto(g, x + 19, y, buf, GFX_BRANCO);
        ui_progresso(g, x + 19, y + 10, 42, 5, gu->afinidades[n], VITAL_MAX);
        celula_i++;
    }
}

static void desenhar(gfx_t *g, uint32_t agora)
{
    ui_abas(g, ABAS, 4, aba, agora);
    switch (aba) {
    case 0: aba_guara(g, agora); break;
    case 1: aba_estado(g); break;
    case 2: aba_nivel(g); break;
    default: aba_natureza(g); break;
    }
}

const tela_t TELA_GUARA = {
    .nome = "guara",
    .tecla = tecla,
    .desenhar = desenhar,
};
