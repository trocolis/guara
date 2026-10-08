#include <stdio.h>
#include <string.h>

#include "app.h"
#include "audio.h"
#include "componentes.h"
#include "modal.h"
#include "sprite.h"
#include "telas.h"

/*
 * Primeiro boot (ou após Reiniciar): pede um nome de até 10 letras e sorteia
 * natureza, cor e cabeça (README > Ciclo de vida > Nascimento).
 */

static void apresentado(int r, void *ctx)
{
    (void)r;
    (void)ctx;
    ui_limpar_pilha(&TELA_PRINCIPAL, NULL);
}

static void nomeado(const char *nome, void *ctx)
{
    (void)ctx;
    if (!nome || !*nome) {
        return; /* continua na tela de nascimento */
    }
    app_nascer(nome);
    audio_vitoria();
    char msg[64];
    snprintf(msg, sizeof(msg), "%s nasceu! Natureza: %s", g_guara.nome, natureza_nome(g_guara.natureza));
    ui_alerta("NASCIMENTO", natureza_icone(g_guara.natureza), msg, "OK", apresentado, NULL);
}

static void tecla(const evento_tecla_t *ev)
{
    if (ev->tipo == TECLA_PRESSIONOU && ev->tecla == TECLA_A) {
        ui_teclado("Nome do Guará", "", 10, nomeado, NULL);
    }
}

static void desenhar(gfx_t *g, uint32_t agora)
{
    ui_titulo(g, "UM NOVO GUARA", agora);
    /* silhueta piscando: o filhote está chegando */
    if ((agora / 600) % 2) {
        sprite_guara(g, COR_BRANCO, 1, ESTAGIO_BEBE, OLHO_FECHADO, 64, 36, sprite_quadro(agora), 0);
    }
    gfx_texto_centro(g, 0, GFX_LARGURA, 40, "Escolha um nome", GFX_BRANCO);
    ui_rodape(g, NULL, "NOMEAR", agora);
}

const tela_t TELA_NASCIMENTO = {
    .nome = "nascimento",
    .tecla = tecla,
    .desenhar = desenhar,
};
