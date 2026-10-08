/*
 * Nascimento: pede o nome (até 10 letras) e sorteia natureza, cor e cabeça.
 */
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "audio.h"
#include "widgets.h"
#include "modal.h"
#include "sprite.h"
#include "screens.h"

/*
 * Primeiro boot (ou após Reiniciar): pede um nome de até 10 letras e sorteia
 * natureza, cor e cabeça (README > Ciclo de vida > Nascimento).
 */

/* Depois de apresentar o Guará, vai para a tela principal. */
static void shown(int r, void *ctx)
{
    (void)r;
    (void)ctx;
    ui_clear_stack(&SCREEN_HOME, NULL);
}

/* Recebe o nome digitado, cria o Guará e apresenta-o. */
static void named(const char *name, void *ctx)
{
    (void)ctx;
    if (!name || !*name) {
        return; /* continua na tela de nascimento */
    }
    app_birth(name);
    audio_victory();
    char msg[64];
    snprintf(msg, sizeof(msg), "%s nasceu! Natureza: %s", g_guara.name, nature_name(g_guara.nature));
    ui_alert("NASCIMENTO", nature_icon(g_guara.nature), msg, "OK", shown, NULL);
}

/* A abre o teclado para digitar o nome. */
static void on_key(const key_event_t *ev)
{
    if (ev->type == KEY_PRESSED && ev->key == KEY_A) {
        ui_keyboard("Nome do Guará", "", 10, named, NULL);
    }
}

/* Silhueta piscando e o convite para nomear. */
static void on_draw(gfx_t *g, uint32_t now)
{
    ui_title(g, "UM NOVO GUARA", now);
    /* silhueta piscando: o filhote está chegando */
    if ((now / 600) % 2) {
        sprite_guara(g, COLOR_WHITE, 1, STAGE_BABY, EYE_CLOSED, 64, 36, sprite_frame(now), 0);
    }
    gfx_text_center(g, 0, GFX_WIDTH, 40, "Escolha um nome", GFX_WHITE);
    ui_footer(g, NULL, "NOMEAR", now);
}

const screen_t SCREEN_BIRTH = {
    .name = "nascimento",
    .on_key = on_key,
    .on_draw = on_draw,
};
