#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "alarmes.h"
#include "app.h"
#include "audio.h"
#include "bateria.h"
#include "componentes.h"
#include "config.h"
#include "modal.h"
#include "persist.h"
#include "relogio.h"
#include "sprite.h"
#include "storage.h"
#include "telas.h"
#include "wifi.h"

/* Layout (mockup bitmaps/exemple/main_screen.bmp) */
#define GUARA_X 64
#define GUARA_BASE 61          /* 2 px acima do limite inferior */
#define BALAO_X 18             /* lado direito do aparelho = esquerda do usuário */
#define BALAO_Y 14
#define ITEM_X 100             /* área de itens: à esquerda do Guará (direita do usuário) */
#define ITEM_Y 34

static struct {
    uint32_t piscar_em, piscar_ate;
    uint32_t feliz_em, feliz_ate;
    uint32_t emocao_em, emocao_ate;
    uint32_t piscadela_ate;
} anim;

static uint32_t aleatorio_ms(uint32_t min, uint32_t max)
{
    return (uint32_t)rng_faixa(&g_rng, (int)min, (int)max);
}

static void agendar(uint32_t agora)
{
    if (anim.piscar_em == 0) {
        anim.piscar_em = agora + aleatorio_ms(3000, 6000);
        anim.feliz_em = agora + aleatorio_ms(20 * 60000, 30 * 60000);
        anim.emocao_em = agora + aleatorio_ms(20 * 60000, 30 * 60000);
    }
    if (agora >= anim.piscar_em) {
        anim.piscar_ate = agora + 150;
        anim.piscar_em = agora + aleatorio_ms(3000, 6000);
    }
    /* Olhar feliz: 5 a 10 s a cada 20 a 30 min, se os vitais estiverem acima de 80% */
    if (agora >= anim.feliz_em) {
        if (guara_tem(&g_guara, EST_FELIZ)) {
            anim.feliz_ate = agora + aleatorio_ms(5000, 10000);
        }
        anim.feliz_em = agora + aleatorio_ms(20 * 60000, 30 * 60000);
    }
    /* Emoções no balão: 5 a 30 s a cada 20 a 30 min */
    if (agora >= anim.emocao_em) {
        anim.emocao_ate = agora + aleatorio_ms(5000, 30000);
        anim.emocao_em = agora + aleatorio_ms(20 * 60000, 30 * 60000);
    }
}

void principal_piscadela(uint32_t ate)
{
    anim.piscadela_ate = ate;
}

static const char *olho_atual(uint32_t agora)
{
    const guara_t *g = &g_guara;
    if (guara_tem(g, EST_DESMAIADO)) return OLHO_DESMAIADO;
    if (guara_tem(g, EST_DORMINDO)) return OLHO_FECHADO;
    if (agora < anim.piscadela_ate) return OLHO_PISCADELA;
    if (guara_tem(g, EST_ENVENENADO) || guara_tem(g, EST_QUEIMADO) || guara_tem(g, EST_CONGELADO)) return OLHO_ASSUSTADO;
    if (guara_tem(g, EST_FAMINTO) || guara_tem(g, EST_CANSADO) || guara_tem(g, EST_CONFUSO) ||
        guara_tem(g, EST_PARALISADO))
        return OLHO_ATORDOADO;
    if (guara_tem(g, EST_ZANGADO)) return OLHO_RAIVA;
    if (guara_tem(g, EST_TRISTE) || guara_tem(g, EST_ENTEDIADO)) return OLHO_CHATEADO;
    if (agora < anim.feliz_ate) return OLHO_FELIZ;
    if (agora < anim.piscar_ate) return OLHO_FECHADO;
    return OLHO_NORMAL;
}

static const char *icone_balao(uint32_t agora)
{
    int fisico = guara_estado_principal(&g_guara);
    if (fisico >= 0) {
        return estado_icone((estado_t)fisico);
    }
    if (agora < anim.feliz_ate) {
        return estado_icone(EST_FELIZ);
    }
    int emocao = guara_emocao(&g_guara);
    if (emocao >= 0 && emocao != EST_FELIZ && agora < anim.emocao_ate) {
        return estado_icone((estado_t)emocao);
    }
    return NULL;
}

static void barra_superior(gfx_t *g, uint32_t agora)
{
    char hora[8];
    relogio_formatar_hhmm(hora, sizeof(hora));
    gfx_texto(g, 1, 1, hora, GFX_BRANCO);

    int x = GFX_LARGURA - 9;
    char nome[24];
    if (!bateria_presente()) {
        snprintf(nome, sizeof(nome), "status_bateria_ausente_8x8");
    } else {
        int nivel = bateria_porcento() / 25;
        if (nivel > 3) {
            nivel = 3;
        }
        if (bateria_carregando()) {
            nivel = (int)((agora / 500) % 4); /* carregando: animação */
        }
        snprintf(nome, sizeof(nome), "battery_%d", nivel);
    }
    ui_icone(g, nome, x, 1);
    x -= 10;
    if (wifi_conectado()) {
        snprintf(nome, sizeof(nome), "wifi_%d", wifi_barras() ? wifi_barras() : 1);
        ui_icone(g, nome, x, 1);
        x -= 10;
    }
    if (g_cfg.bt_ligado) {
        ui_icone(g, "status_bluetooth", x, 1);
        x -= 10;
    }
    if (alarmes_quantidade() > 0) {
        ui_icone(g, "status_alarm", x, 1);
        x -= 10;
    }
    if (temporizador_ativo()) {
        ui_icone(g, "status_timer", x, 1);
        x -= 10;
    }
    if (g_cfg.mudo) {
        ui_icone(g, "status_mute", x, 1);
    }
}

/* Cena da tela principal, reutilizada pela animação de uso de item. */
void principal_cena(gfx_t *g, uint32_t agora, bool com_balao)
{
    agendar(agora);
    barra_superior(g, agora);
    if (!g_tem_guara) {
        return;
    }
    int quadro = guara_tem(&g_guara, EST_DESMAIADO) ? 1 : sprite_quadro(agora);
    sprite_guara(g, g_guara.cor, g_guara.cabeca, g_guara.estagio, olho_atual(agora), GUARA_X, GUARA_BASE, quadro,
                 GFX_ESCALA2);

    const char *icone = com_balao ? icone_balao(agora) : NULL;
    if (icone) {
        /* "respiração": desce 8 px e volta, suavemente (ciclo de 3 s) */
        static const uint8_t onda[] = {0, 0, 1, 2, 3, 4, 5, 6, 7, 7, 8, 8, 8, 7, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0};
        int dy = onda[(agora / 125) % sizeof(onda)];
        ui_icone_modo(g, "speech_balloon", BALAO_X, BALAO_Y + dy, GFX_COPIA, 0);
        ui_icone(g, icone, BALAO_X + 2, BALAO_Y + dy + 2);
    }
}

static void tecla(const evento_tecla_t *ev)
{
    if (ev->tipo != TECLA_PRESSIONOU) {
        return;
    }
    if (ev->tecla == TECLA_A) {
        app_interagir();
        ui_abrir(&TELA_MENU, NULL);
    }
}

static void desenhar(gfx_t *g, uint32_t agora)
{
    principal_cena(g, agora, true);
}

static void retomar(void)
{
    /* Voltou do menu: grava se algo mudou (Pilar 2) */
    persist_salvar_se_sujo(&g_guara);
}

const tela_t TELA_PRINCIPAL = {
    .nome = "principal",
    .tecla = tecla,
    .desenhar = desenhar,
    .retomar = retomar,
};

/* ------------------------------------------------------------------------- */
/* Uso de item: a tela principal aparece com a animação e depois volta       */
/* ------------------------------------------------------------------------- */

static struct {
    registro_t item;
    uint32_t inicio;
    bool comida;
    int ultimo_quadro;
} uso;

#define QUADRO_MORDIDA_MS 300
#define PISCAR_MS 1600

static void uso_abrir(void *param)
{
    int id = (int)(intptr_t)param;
    catalogo_ler(CAT_ITENS, id, &uso.item);
    uso.inicio = app_ms();
    uso.comida = uso.item.tipo == ITEM_COMIDA;
    uso.ultimo_quadro = -1;
}

static void uso_tecla(const evento_tecla_t *ev)
{
    (void)ev; /* a animação não pode ser interrompida */
}

static void uso_desenhar(gfx_t *g, uint32_t agora)
{
    uint32_t t = agora - uso.inicio;
    principal_cena(g, agora, false);
    if (uso.comida) {
        int quadro = (int)(t / QUADRO_MORDIDA_MS);
        if (quadro >= 6) {
            principal_piscadela(agora + 1500);
            ui_voltar();
            return;
        }
        ui_icone_modo(g, uso.item.icone, ITEM_X, ITEM_Y, GFX_COPIA, 0);
        if (quadro >= 1) {
            char nome[12];
            snprintf(nome, sizeof(nome), "bite_%d", quadro);
            ui_icone_modo(g, nome, ITEM_X, ITEM_Y, GFX_E, 0);
            if (quadro != uso.ultimo_quadro) {
                audio_mordida();
            }
        }
        uso.ultimo_quadro = quadro;
    } else {
        if (t >= PISCAR_MS) {
            principal_piscadela(agora + 1500);
            ui_voltar();
            return;
        }
        /* Pisca acelerando até sumir */
        uint32_t periodo = 300 - t * 260 / PISCAR_MS;
        if ((t / periodo) % 2 == 0) {
            ui_icone_modo(g, uso.item.icone, ITEM_X, ITEM_Y, GFX_COPIA, 0);
        }
    }
}

static const tela_t TELA_USO_ITEM = {
    .nome = "uso_item",
    .abrir = uso_abrir,
    .tecla = uso_tecla,
    .desenhar = uso_desenhar,
};

void telas_usar_item(int item_id)
{
    registro_t r;
    if (!catalogo_ler(CAT_ITENS, item_id, &r)) {
        return;
    }
    uso_t res = guara_usar_item(&g_guara, &r, &g_rng);
    if (res != USO_OK) {
        static const char *const motivos[] = {"", "Desmaiado! Espere ou use uma cura.", "Amaldiçoado! Não pode usar itens.",
                                              "Sem estoque.", "Item inválido."};
        audio_erro();
        ui_alerta(r.nome, r.icone, motivos[res], "OK", NULL, NULL);
        return;
    }
    app_interagir();
    app_alterou();
    ui_abrir(&TELA_USO_ITEM, (void *)(intptr_t)item_id);
}

/* ------------------------------------------------------------------------- */
/* Alarme: bipes até confirmar ou por 1 minuto                               */
/* ------------------------------------------------------------------------- */

static struct {
    bool tocando;
    uint32_t inicio, ultimo_bip;
    char titulo[24];
} alarme;

static void alarme_resposta(int r, void *ctx)
{
    (void)r;
    (void)ctx;
    alarme.tocando = false;
    audio_parar_efeitos();
}

bool telas_alarme_tocando(void)
{
    if (alarme.tocando) {
        uint32_t agora = app_ms();
        if (agora - alarme.inicio > 60000) {
            alarme.tocando = false; /* para sozinho após 1 minuto */
        } else if (agora - alarme.ultimo_bip > 1200) {
            alarme.ultimo_bip = agora;
            audio_alarme();
        }
    }
    return alarme.tocando;
}

void telas_disparar_alarme(const char *titulo)
{
    char hora[8], msg[40];
    relogio_formatar_hhmm(hora, sizeof(hora));
    snprintf(msg, sizeof(msg), "%s - %s", titulo, hora);
    snprintf(alarme.titulo, sizeof(alarme.titulo), "%s", titulo);
    alarme.tocando = true;
    alarme.inicio = alarme.ultimo_bip = app_ms();
    audio_alarme();
    ui_alerta(titulo, strcmp(titulo, "Temporizador") == 0 ? "timer" : "alarm",
              strcmp(titulo, "Temporizador") == 0 ? "A contagem terminou!" : msg, "OK", alarme_resposta, NULL);
}
