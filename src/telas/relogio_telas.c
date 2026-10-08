#include <stdio.h>
#include <string.h>

#include "alarmes.h"
#include "app.h"
#include "audio.h"
#include "componentes.h"
#include "config.h"
#include "modal.h"
#include "relogio.h"
#include "submenu.h"
#include "telas.h"
#include "rede.h"

/* ========================================================================= */
/* Editor de hora "--:--" (README > Relógio > Despertador)                   */
/* ========================================================================= */

typedef void (*hora_fn)(int valor, bool cancelou);

static struct {
    char titulo[24];
    int digitos[4];
    int cursor;
    bool duracao; /* MM:SS em vez de HH:MM */
    bool edicao;
    hora_fn fn;
} ed;

static int max_digito(int i)
{
    if (ed.duracao) {
        return i == 2 ? 5 : 9;
    }
    switch (i) {
    case 0: return 2;
    case 1: return ed.digitos[0] == 2 ? 3 : 9;
    case 2: return 5;
    default: return 9;
    }
}

static int valor_editor(void)
{
    int a = ed.digitos[0] * 10 + ed.digitos[1], b = ed.digitos[2] * 10 + ed.digitos[3];
    return ed.duracao ? a * 60 + b : a * 60 + b;
}

static void ed_tecla(const evento_tecla_t *ev)
{
    if (ev->tipo == TECLA_SOLTOU) {
        return;
    }
    switch (ev->tecla) {
    case TECLA_ESQ:
        ed.cursor = (ed.cursor + 3) % 4;
        break;
    case TECLA_DIR:
        ed.cursor = (ed.cursor + 1) % 4;
        break;
    case TECLA_CIMA:
        ed.digitos[ed.cursor] = ed.digitos[ed.cursor] >= max_digito(ed.cursor) ? 0 : ed.digitos[ed.cursor] + 1;
        break;
    case TECLA_BAIXO:
        ed.digitos[ed.cursor] = ed.digitos[ed.cursor] <= 0 ? max_digito(ed.cursor) : ed.digitos[ed.cursor] - 1;
        break;
    case TECLA_A:
    case TECLA_B:
        if (ev->tipo == TECLA_PRESSIONOU) {
            hora_fn fn = ed.fn;
            int v = valor_editor();
            bool cancelou = ev->tecla == TECLA_B;
            ui_voltar();
            fn(v, cancelou);
        }
        return;
    default:
        break;
    }
    if (ed.digitos[1] > max_digito(1)) {
        ed.digitos[1] = max_digito(1);
    }
}

/* Dígitos grandes centralizados; o cursor é um retângulo cheio que inverte a fonte e pisca. */
static void desenhar_digitos(gfx_t *g, const int d[4], int cursor, uint32_t agora, int y)
{
    const int escala = 2, passo = 6 * escala, x0 = (GFX_LARGURA - 5 * passo + escala) / 2;
    for (int i = 0; i < 5; i++) {
        int x = x0 + i * passo;
        char s[2] = {i == 2 ? ':' : (char)('0' + d[i < 2 ? i : i - 1]), 0};
        bool sel = i != 2 && (i < 2 ? i : i - 1) == cursor && (agora / 400) % 2 == 0;
        if (sel) {
            gfx_retangulo_arred_cheio(g, x - 2, y - 2, passo + 2, 8 * escala + 2, 2, GFX_BRANCO);
        }
        gfx_texto_escala(g, x, y, s, sel ? GFX_PRETO : GFX_BRANCO, escala);
    }
}

static void ed_desenhar(gfx_t *g, uint32_t agora)
{
    ui_titulo(g, ed.titulo, agora);
    desenhar_digitos(g, ed.digitos, ed.cursor, agora, 22);
    ui_rodape(g, "CANCELAR", "SALVAR", agora);
}

static const tela_t TELA_EDITAR_HORA = {
    .nome = "editar_hora",
    .tecla = ed_tecla,
    .desenhar = ed_desenhar,
};

static void editar_hora(const char *titulo, int valor, bool duracao, bool edicao, hora_fn fn)
{
    memset(&ed, 0, sizeof(ed));
    snprintf(ed.titulo, sizeof(ed.titulo), "%s", titulo);
    int a = valor / 60, b = valor % 60;
    ed.digitos[0] = a / 10 % 10;
    ed.digitos[1] = a % 10;
    ed.digitos[2] = b / 10;
    ed.digitos[3] = b % 10;
    ed.duracao = duracao;
    ed.edicao = edicao;
    ed.fn = fn;
    ui_abrir(&TELA_EDITAR_HORA, NULL);
}

/* ========================================================================= */
/* Despertadores                                                             */
/* ========================================================================= */

static ui_lista_t lista_alarmes;

static void item_alarme(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    if (i == 0) {
        it->icone = "add";
        snprintf(it->texto, sizeof(it->texto), "Criar");
        return;
    }
    int m = alarmes_minuto(i - 1);
    it->icone = "alarm";
    snprintf(it->texto, sizeof(it->texto), "%02d:%02d", m / 60, m % 60);
}

static void sem_cartao(void)
{
    audio_erro();
    ui_alerta("DESPERTADOR", "sd_card", "Insira o cartão SD para salvar.", "OK", NULL, NULL);
}

static void novo_salvo(int valor, bool cancelou)
{
    if (!cancelou && !alarmes_adicionar(valor)) {
        sem_cartao();
    }
}

static int editando;
static void apagar_resposta(int r, void *ctx)
{
    (void)ctx;
    if (r == 1) {
        alarmes_remover(editando);
    }
}

static void editado(int valor, bool cancelou)
{
    if (cancelou) {
        ui_confirmar("DESPERTADOR", "alarm", "Apagar este despertador?", "NAO", "SIM", apagar_resposta, NULL);
    } else if (!alarmes_editar(editando, valor)) {
        sem_cartao();
    }
}

static void alarmes_tecla(const evento_tecla_t *ev)
{
    lista_alarmes.total = alarmes_quantidade() + 1;
    if (ui_lista_tecla(&lista_alarmes, ev, UI_LINHAS_VISIVEIS) || ev->tipo != TECLA_PRESSIONOU) {
        return;
    }
    if (ev->tecla == TECLA_B) {
        ui_voltar();
    } else if (ev->tecla == TECLA_A) {
        if (lista_alarmes.sel == 0) {
            if (alarmes_quantidade() >= ALARMES_MAX) {
                ui_alerta("DESPERTADOR", "alarm", "Limite de despertadores atingido.", "OK", NULL, NULL);
            } else {
                editar_hora("NOVO DESPERTADOR", relogio_minuto_do_dia() >= 0 ? relogio_minuto_do_dia() : 0, false,
                            false, novo_salvo);
            }
        } else {
            editando = lista_alarmes.sel - 1;
            editar_hora("DESPERTADOR", alarmes_minuto(editando), false, true, editado);
        }
    }
}

static void alarmes_desenhar(gfx_t *g, uint32_t agora)
{
    lista_alarmes.total = alarmes_quantidade() + 1;
    ui_titulo(g, "DESPERTADOR", agora);
    ui_lista_desenhar(g, &lista_alarmes, UI_TITULO_ALTURA, UI_LINHAS_VISIVEIS, item_alarme, NULL, agora);
}

static const tela_t TELA_DESPERTADORES = {
    .nome = "despertadores",
    .tecla = alarmes_tecla,
    .desenhar = alarmes_desenhar,
};

/* ========================================================================= */
/* Cronômetro                                                                */
/* ========================================================================= */

static struct {
    bool correndo;
    uint32_t inicio, decorrido;
} crono;

static void crono_tecla(const evento_tecla_t *ev)
{
    if (ev->tipo != TECLA_PRESSIONOU) {
        return;
    }
    if (ev->tecla == TECLA_B) {
        app_bloquear_sono(false);
        ui_voltar();
    } else if (ev->tecla == TECLA_A) {
        if (crono.correndo) {
            crono.decorrido = app_ms() - crono.inicio;
            crono.correndo = false;
        } else {
            crono.inicio = app_ms();
            crono.decorrido = 0;
            crono.correndo = true;
        }
        app_bloquear_sono(crono.correndo);
    }
}

static void crono_desenhar(gfx_t *g, uint32_t agora)
{
    uint32_t ms = crono.correndo ? agora - crono.inicio : crono.decorrido;
    char buf[16];
    uint32_t s = ms / 1000;
    if (s >= 3600) {
        snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
                 (unsigned long)(s % 60));
    } else {
        snprintf(buf, sizeof(buf), "%02lu:%02lu.%lu", (unsigned long)(s / 60), (unsigned long)(s % 60),
                 (unsigned long)(ms / 100 % 10));
    }
    ui_titulo(g, "CRONOMETRO", agora);
    int w = gfx_largura_texto(buf) * 2 + 1;
    gfx_texto_escala(g, (GFX_LARGURA - w) / 2, 24, buf, GFX_BRANCO, 2);
    ui_rodape(g, "SAIR", crono.correndo ? "PARAR" : "INICIAR", agora);
}

static const tela_t TELA_CRONOMETRO = {
    .nome = "cronometro",
    .fps = 30,
    .tecla = crono_tecla,
    .desenhar = crono_desenhar,
};

/* ========================================================================= */
/* Temporizador (roda em segundo plano; editável quando parado)              */
/* ========================================================================= */

static int temp_digitos[4] = {0, 5, 0, 0};
static int temp_cursor;

static void temp_tecla(const evento_tecla_t *ev)
{
    if (ev->tipo == TECLA_SOLTOU) {
        return;
    }
    bool parado = !temporizador_ativo();
    if (parado) {
        static const int maximos[4] = {9, 9, 5, 9};
        switch (ev->tecla) {
        case TECLA_ESQ: temp_cursor = (temp_cursor + 3) % 4; return;
        case TECLA_DIR: temp_cursor = (temp_cursor + 1) % 4; return;
        case TECLA_CIMA:
            temp_digitos[temp_cursor] = temp_digitos[temp_cursor] >= maximos[temp_cursor] ? 0 : temp_digitos[temp_cursor] + 1;
            return;
        case TECLA_BAIXO:
            temp_digitos[temp_cursor] = temp_digitos[temp_cursor] <= 0 ? maximos[temp_cursor] : temp_digitos[temp_cursor] - 1;
            return;
        default: break;
        }
    }
    if (ev->tipo != TECLA_PRESSIONOU) {
        return;
    }
    if (ev->tecla == TECLA_B) {
        ui_voltar();
    } else if (ev->tecla == TECLA_A) {
        if (parado) {
            uint32_t s = (uint32_t)((temp_digitos[0] * 10 + temp_digitos[1]) * 60 + temp_digitos[2] * 10 + temp_digitos[3]);
            if (s > 0) {
                temporizador_iniciar(s);
            }
        } else {
            temporizador_parar();
        }
    }
}

static void temp_desenhar(gfx_t *g, uint32_t agora)
{
    ui_titulo(g, "TEMPORIZADOR", agora);
    if (temporizador_ativo()) {
        uint32_t r = temporizador_restante();
        int d[4] = {(int)(r / 600 % 10), (int)(r / 60 % 10), (int)(r % 60 / 10), (int)(r % 10)};
        desenhar_digitos(g, d, -1, agora, 22);
    } else {
        desenhar_digitos(g, temp_digitos, temp_cursor, agora, 22);
    }
    ui_rodape(g, "SAIR", temporizador_ativo() ? "PARAR" : "INICIAR", agora);
}

static const tela_t TELA_TEMPORIZADOR = {
    .nome = "temporizador",
    .tecla = temp_tecla,
    .desenhar = temp_desenhar,
};

/* ========================================================================= */
/* Hora de dormir, ajuste manual e NTP                                       */
/* ========================================================================= */

static void dormir_salvo(int v, bool cancelou)
{
    if (!cancelou) {
        g_cfg.dormir_min = (uint16_t)v;
        config_salvar();
    }
}

static void acordar_salvo(int v, bool cancelou)
{
    if (!cancelou) {
        g_cfg.acordar_min = (uint16_t)v;
        config_salvar();
    }
}

static void info_dormir(char *b, int t) { snprintf(b, (size_t)t, "%02d%02d", g_cfg.dormir_min / 60, g_cfg.dormir_min % 60); }
static void info_acordar(char *b, int t) { snprintf(b, (size_t)t, "%02d%02d", g_cfg.acordar_min / 60, g_cfg.acordar_min % 60); }
static void abrir_dormir(void) { editar_hora("HORA DE DORMIR", g_cfg.dormir_min, false, false, dormir_salvo); }
static void abrir_acordar(void) { editar_hora("HORA DE ACORDAR", g_cfg.acordar_min, false, false, acordar_salvo); }

static const submenu_item_t ITENS_DORMIR[] = {
    {"moon_face", "Dormir", info_dormir, abrir_dormir},
    {"clock", "Acordar", info_acordar, abrir_acordar},
};
static const submenu_t MENU_DORMIR = {"HORA DE DORMIR", ITENS_DORMIR, 2};

static void ajuste_salvo(int v, bool cancelou)
{
    if (!cancelou) {
        relogio_ajustar_local(v / 60, v % 60);
    }
}

static void abrir_ajuste(void)
{
    int m = relogio_minuto_do_dia();
    editar_hora("AJUSTE MANUAL", m >= 0 ? m : 12 * 60, false, false, ajuste_salvo);
}

static void sincronizar(void)
{
    if (!rede_permitida()) {
        audio_erro();
        ui_alerta("NTP", "satellite", "Ligue o Wi-Fi em Ajustes.", "OK", NULL, NULL);
        return;
    }
    rede_usar();
    bool ok = rede_aguardar(12000) && relogio_sincronizar_ntp(6000);
    rede_liberar();
    char hora[8];
    relogio_formatar_hhmm(hora, sizeof(hora));
    char msg[48];
    snprintf(msg, sizeof(msg), ok ? "Hora atualizada: %s" : "Falhou. Configure o Wi-Fi.", hora);
    if (!ok) {
        audio_erro();
    }
    ui_alerta("NTP", "satellite", msg, "OK", NULL, NULL);
}

static const submenu_item_t ITENS_CONFIG[] = {
    {"wristwatch", "Ajuste manual", NULL, abrir_ajuste},
    {"satellite", "Sincronizar NTP", NULL, sincronizar},
};
static const submenu_t MENU_CONFIG = {"CONFIGURAR", ITENS_CONFIG, 2};

static void abrir_despertadores(void)
{
    lista_alarmes = (ui_lista_t){.total = alarmes_quantidade() + 1};
    ui_abrir(&TELA_DESPERTADORES, NULL);
}
static void abrir_crono(void) { ui_abrir(&TELA_CRONOMETRO, NULL); }
static void abrir_temp(void) { ui_abrir(&TELA_TEMPORIZADOR, NULL); }
static void abrir_menu_dormir(void) { submenu_abrir(&MENU_DORMIR); }
static void abrir_menu_config(void) { submenu_abrir(&MENU_CONFIG); }

static void info_alarmes(char *b, int t)
{
    if (alarmes_quantidade()) {
        snprintf(b, (size_t)t, "%d", alarmes_quantidade());
    }
}

static void info_temp(char *b, int t)
{
    if (temporizador_ativo()) {
        uint32_t r = temporizador_restante();
        snprintf(b, (size_t)t, "%02lu%02lu", (unsigned long)(r / 60 % 100), (unsigned long)(r % 60));
    }
}

static const submenu_item_t ITENS_RELOGIO[] = {
    {"alarm", "Despertador", info_alarmes, abrir_despertadores},
    {"stopwatch", "Cronômetro", NULL, abrir_crono},
    {"timer", "Temporizador", info_temp, abrir_temp},
    {"moon_face", "Hora de dormir", NULL, abrir_menu_dormir},
    {"gear", "Configurar", NULL, abrir_menu_config},
};
static const submenu_t MENU_RELOGIO = {"RELOGIO", ITENS_RELOGIO, 5};

void relogio_abrir_menu(void)
{
    submenu_abrir(&MENU_RELOGIO);
}
