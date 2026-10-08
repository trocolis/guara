#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "app.h"
#include "audio.h"
#include "componentes.h"
#include "modal.h"
#include "telas.h"

/* Inventário: abas TODOS, COMIDAS, BEBIDAS e POCOES (README > Inventário). */

static const char *const ABAS[] = {"TODOS", "COMIDAS", "BEBIDAS", "POCOES"};
static int aba;
static int ids[QTD_ITENS];
static ui_lista_t lista;

static int por_nome(const void *a, const void *b)
{
    registro_t ra, rb;
    catalogo_ler(CAT_ITENS, *(const int *)a, &ra);
    catalogo_ler(CAT_ITENS, *(const int *)b, &rb);
    return strcasecmp(ra.nome, rb.nome);
}

static void montar(void)
{
    registro_t r;
    lista.total = 0;
    for (int id = 0; id < catalogo_quantidade(CAT_ITENS) && id < QTD_ITENS; id++) {
        if (g_guara.inventario[id] == 0 || !catalogo_ler(CAT_ITENS, id, &r)) {
            continue;
        }
        if (aba == 0 || r.tipo == aba - 1) {
            ids[lista.total++] = id;
        }
    }
    qsort(ids, (size_t)lista.total, sizeof(int), por_nome);
    ui_lista_ajustar(&lista, UI_LINHAS_VISIVEIS);
}

static void item(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    registro_t r;
    catalogo_ler(CAT_ITENS, ids[i], &r);
    it->icone = r.icone;
    snprintf(it->texto, sizeof(it->texto), "%s", r.nome);
    snprintf(it->info, sizeof(it->info), "x%d", g_guara.inventario[ids[i]]);
}

static int item_sel(void)
{
    return lista.total ? ids[lista.sel] : -1;
}

static void descartar_confirmado(int r, void *ctx)
{
    int qtd = (int)(intptr_t)ctx;
    int id = item_sel();
    if (r == 1 && id >= 0) {
        g_guara.inventario[id] = (uint8_t)(g_guara.inventario[id] > qtd ? g_guara.inventario[id] - qtd : 0);
        app_alterou();
        montar();
    }
}

static void descartar_quantidade(int qtd, void *ctx)
{
    (void)ctx;
    if (qtd <= 0) {
        return;
    }
    registro_t r;
    catalogo_ler(CAT_ITENS, item_sel(), &r);
    char msg[48];
    snprintf(msg, sizeof(msg), "Descartar %d %s?", qtd, r.nome);
    ui_confirmar("DESCARTAR", r.icone, msg, "NAO", "SIM", descartar_confirmado, (void *)(intptr_t)qtd);
}

static void detalhes(const registro_t *r)
{
    static const char *const alvos[] = {"HP", "MP", "Energia", "Humor", "Alimentação", "Saúde"};
    char texto[256];
    int n = snprintf(texto, sizeof(texto), "%s\n", r->descricao);
    for (int i = 0; i < r->n_efeitos && n < (int)sizeof(texto); i++) {
        int a = r->efeitos[i].alvo;
        n += snprintf(texto + n, sizeof(texto) - (size_t)n, "%s %+d%s\n", a < 6 ? alvos[a] : "?", r->efeitos[i].valor,
                      a >= VITAL_ENERGIA && a < 6 ? "%" : "");
    }
    for (int i = 0; i < r->n_sobre && n < (int)sizeof(texto); i++) {
        const char *nome = r->sobre[i].estado == EST_TODOS ? "Todos" : estado_nome((estado_t)r->sobre[i].estado);
        if (r->sobre[i].valor <= -100) {
            n += snprintf(texto + n, sizeof(texto) - (size_t)n, "Cura: %s\n", nome);
        } else {
            n += snprintf(texto + n, sizeof(texto) - (size_t)n, "%s %+d%%\n", nome, r->sobre[i].valor);
        }
    }
    if (n < (int)sizeof(texto)) {
        snprintf(texto + n, sizeof(texto) - (size_t)n, "Afinidade: %s", natureza_nome((natureza_t)r->natureza));
    }
    ui_detalhes(r->nome, texto);
}

static void acao_escolhida(int opcao, void *ctx)
{
    (void)ctx;
    int id = item_sel();
    registro_t r;
    if (id < 0 || !catalogo_ler(CAT_ITENS, id, &r)) {
        return;
    }
    switch (opcao) {
    case 0:
        telas_usar_item(id);
        break;
    case 1:
        detalhes(&r);
        break;
    case 2:
        ui_quantidade("QUANTIDADE", 1, g_guara.inventario[id], 1, descartar_quantidade, NULL);
        break;
    case 3:
        /* README: TODO "Elaborar a opção de Compartilhar" (depende do Bluetooth) */
        ui_alerta("COMPARTILHAR", "bluetooth", "Em breve: troca de itens entre Guarás.", "OK", NULL, NULL);
        break;
    default:
        break;
    }
}

static void tecla(const evento_tecla_t *ev)
{
    if (ui_lista_tecla(&lista, ev, UI_LINHAS_VISIVEIS) || ev->tipo == TECLA_SOLTOU) {
        return;
    }
    if (ev->tecla == TECLA_ESQ || ev->tecla == TECLA_DIR) {
        aba = (aba + (ev->tecla == TECLA_DIR ? 1 : 3)) % 4;
        lista.sel = lista.topo = 0;
        montar();
    } else if (ev->tipo != TECLA_PRESSIONOU) {
        return;
    } else if (ev->tecla == TECLA_B) {
        ui_voltar();
    } else if (ev->tecla == TECLA_A && lista.total) {
        registro_t r;
        catalogo_ler(CAT_ITENS, item_sel(), &r);
        const char *usar = r.tipo == ITEM_COMIDA ? "Comer" : (r.tipo == ITEM_BEBIDA ? "Beber" : "Usar");
        const char *opcoes[] = {usar, "Detalhes", "Descartar", "Compartilhar"};
        ui_escolha(r.nome, opcoes, 4, acao_escolhida, NULL);
    }
}

static void desenhar(gfx_t *g, uint32_t agora)
{
    ui_abas(g, ABAS, 4, aba, agora);
    if (lista.total == 0) {
        gfx_texto_centro(g, 0, GFX_LARGURA, 32, "Nenhum item", GFX_BRANCO);
        return;
    }
    ui_lista_desenhar(g, &lista, UI_TITULO_ALTURA, UI_LINHAS_VISIVEIS, item, NULL, agora);
}

static void abrir(void *param)
{
    (void)param;
    aba = 0;
    lista = (ui_lista_t){0};
    montar();
}

static void retomar(void)
{
    montar();
}

const tela_t TELA_INVENTARIO = {
    .nome = "inventario",
    .abrir = abrir,
    .retomar = retomar,
    .tecla = tecla,
    .desenhar = desenhar,
};
