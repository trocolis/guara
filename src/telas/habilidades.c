#include <stdio.h>

#include "app.h"
#include "componentes.h"
#include "modal.h"
#include "telas.h"

/* Habilidades possuídas (sub-menu padrão); as 4 ativas são marcadas com "ATV". */

static int ids[64];
static ui_lista_t lista;

static bool ativa(int id)
{
    for (int i = 0; i < QTD_HAB_ATIVAS; i++) {
        if (g_guara.hab_ativas[i] == id) {
            return true;
        }
    }
    return false;
}

static void montar(void)
{
    lista.total = 0;
    for (int id = 0; id < catalogo_quantidade(CAT_HABILIDADES) && id < 64; id++) {
        if (g_guara.hab_possui & (1ull << id)) {
            ids[lista.total++] = id;
        }
    }
}

static void item(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    registro_t r;
    catalogo_ler(CAT_HABILIDADES, ids[i], &r);
    it->icone = r.icone;
    snprintf(it->texto, sizeof(it->texto), "%s", r.nome);
    if (ativa(ids[i])) {
        snprintf(it->info, sizeof(it->info), "ATV");
    }
}

static void alternar(int r, void *ctx)
{
    (void)ctx;
    int id = ids[lista.sel];
    if (r != 1) {
        return;
    }
    if (ativa(id)) {
        for (int i = 0; i < QTD_HAB_ATIVAS; i++) {
            if (g_guara.hab_ativas[i] == id) {
                g_guara.hab_ativas[i] = SLOT_VAZIO;
            }
        }
    } else {
        for (int i = 0; i < QTD_HAB_ATIVAS; i++) {
            if (g_guara.hab_ativas[i] == SLOT_VAZIO) {
                g_guara.hab_ativas[i] = (uint8_t)id;
                break;
            }
        }
    }
    app_alterou();
}

static void tecla(const evento_tecla_t *ev)
{
    if (ui_lista_tecla(&lista, ev, UI_LINHAS_VISIVEIS) || ev->tipo != TECLA_PRESSIONOU) {
        return;
    }
    if (ev->tecla == TECLA_B) {
        ui_voltar();
    } else if (ev->tecla == TECLA_A && lista.total) {
        static const char *const tipos[] = {"Ataque", "Cura", "Efeito"};
        static const char *const alvos[] = {"Usuário", "Oponente"};
        registro_t r;
        catalogo_ler(CAT_HABILIDADES, ids[lista.sel], &r);
        char texto[200];
        int n = snprintf(texto, sizeof(texto), "%s\nTipo: %s\nAlvo: %s\nNatureza: %s\nPoder: %d  MP: %d", r.descricao,
                         tipos[r.tipo % 3], alvos[r.alvo % 2], natureza_nome((natureza_t)r.natureza), r.poder,
                         r.custo_mp);
        for (int i = 0; i < r.n_sobre && n < (int)sizeof(texto); i++) {
            n += snprintf(texto + n, sizeof(texto) - (size_t)n, "\n%s: %d%%", estado_nome((estado_t)r.sobre[i].estado),
                          r.sobre[i].valor);
        }
        ui_detalhes(r.nome, texto);
    } else if ((ev->tecla == TECLA_ESQ || ev->tecla == TECLA_DIR) && lista.total) {
        bool liga = !ativa(ids[lista.sel]);
        int livres = 0;
        for (int i = 0; i < QTD_HAB_ATIVAS; i++) {
            livres += g_guara.hab_ativas[i] == SLOT_VAZIO;
        }
        if (liga && livres == 0) {
            ui_alerta("HABILIDADES", "mp", "Já há 4 habilidades ativas.", "OK", NULL, NULL);
        } else {
            alternar(1, NULL);
        }
    }
}

static void desenhar(gfx_t *g, uint32_t agora)
{
    ui_titulo(g, "HABILIDADES", agora);
    if (lista.total == 0) {
        gfx_texto_centro(g, 0, GFX_LARGURA, 32, "Nenhuma habilidade", GFX_BRANCO);
        return;
    }
    ui_lista_desenhar(g, &lista, UI_TITULO_ALTURA, UI_LINHAS_VISIVEIS, item, NULL, agora);
}

static void abrir(void *param)
{
    (void)param;
    lista = (ui_lista_t){0};
    montar();
}

const tela_t TELA_HABILIDADES = {
    .nome = "habilidades",
    .abrir = abrir,
    .tecla = tecla,
    .desenhar = desenhar,
};
