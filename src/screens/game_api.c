/*
 * Extensões da API Lua do console virtual (ver README > Jogos > API).
 *
 * - oled.icon / oled.icon_size: bitmaps da VROM com modo de desenho, escala e
 *   espelhamento, e consulta de tamanho.
 * - guara.draw: desenha o Guará do jogador (cabeça + corpo + olhos).
 * - catalog: leitura de itens, equipamentos e habilidades do catálogo.
 * - ui: componentes do sistema. Os de desenho (título, abas, lista, rodapé,
 *   progresso, rolagem, letreiro, parágrafo) só desenham; o estado (seleção)
 *   fica com o jogo. Os interativos (alerta, confirmação, escolha, quantidade,
 *   detalhes, teclado) abrem a tela do sistema por cima do jogo, que fica em
 *   pausa, e devolvem a resposta numa função de retorno.
 */
#include "game_api.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "lauxlib.h"
#include "modal.h"
#include "sprite.h"
#include "storage.h"
#include "widgets.h"

/* Quantos componentes com estado próprio (listas, letreiros) um jogo pode usar ao mesmo tempo */
#define MAX_STATEFUL_WIDGETS 4
/* Limite de opções do ui.choice (o modal mostra 3 por vez, com rolagem) */
#define MAX_CHOICE_OPTIONS 8
/* Limite de itens de uma ui.list */
#define MAX_LIST_ITEMS 32

/* Estado persistente entre quadros de cada lista e letreiro, escolhido pelo `id` passado pelo jogo */
static ui_list_t list_states[MAX_STATEFUL_WIDGETS];
static marquee_t marquee_states[MAX_STATEFUL_WIDGETS];

void game_api_reset(void)
{
    memset(list_states, 0, sizeof(list_states));
    memset(marquee_states, 0, sizeof(marquee_states));
}

/* ------------------------------------------------------------------------- */
/* Utilitários de argumentos                                                 */
/* ------------------------------------------------------------------------- */

/* Argumento numérico obrigatório como inteiro (frações são truncadas). */
static int int_arg(lua_State *L, int index)
{
    return (int)luaL_checknumber(L, index);
}

/* Campo inteiro opcional de uma tabela de opções. */
static int int_option(lua_State *L, int table_index, const char *field, int fallback)
{
    if (!lua_istable(L, table_index)) {
        return fallback;
    }
    lua_getfield(L, table_index, field);
    int value = lua_isnumber(L, -1) ? (int)lua_tonumber(L, -1) : fallback;
    lua_pop(L, 1);
    return value;
}

/* Campo booleano opcional de uma tabela de opções. */
static bool bool_option(lua_State *L, int table_index, const char *field, bool fallback)
{
    if (!lua_istable(L, table_index)) {
        return fallback;
    }
    lua_getfield(L, table_index, field);
    bool value = lua_isnil(L, -1) ? fallback : lua_toboolean(L, -1);
    lua_pop(L, 1);
    return value;
}

/* Campo texto opcional de uma tabela de opções (válido enquanto a tabela existir). */
static const char *string_option(lua_State *L, int table_index, const char *field, const char *fallback)
{
    if (!lua_istable(L, table_index)) {
        return fallback;
    }
    lua_getfield(L, table_index, field);
    const char *value = lua_isstring(L, -1) ? lua_tostring(L, -1) : fallback;
    lua_pop(L, 1);
    return value;
}

/* Cor opcional: 0 preto, 1 branco (padrão), 2 inverter. */
static gfx_color_t color_arg(lua_State *L, int index)
{
    int color = (int)luaL_optnumber(L, index, 1);
    return color == 0 ? GFX_BLACK : (color == 2 ? GFX_INVERT : GFX_WHITE);
}

/* Converte o nome do modo de desenho; modos desconhecidos geram erro no jogo. */
static gfx_mode_t mode_from_name(lua_State *L, const char *name)
{
    static const char *const names[] = {"copy", "or", "mask", "xor", "invert"};
    static const gfx_mode_t modes[] = {GFX_COPY, GFX_OR, GFX_MASK, GFX_XOR, GFX_INVERSE};
    for (int i = 0; i < 5; i++) {
        if (strcmp(name, names[i]) == 0) {
            return modes[i];
        }
    }
    luaL_error(L, "modo de desenho inválido: %s (use copy, or, mask, xor ou invert)", name);
    return GFX_OR;
}

/*
 * Lê as opções de desenho de bitmaps. Aceita o formato antigo (booleano =
 * espelhar) ou uma tabela {mode, scale, mirror}.
 */
static void draw_options(lua_State *L, int index, gfx_mode_t *mode, unsigned *flags)
{
    *mode = GFX_OR;
    *flags = 0;
    if (lua_isboolean(L, index)) {
        *flags = lua_toboolean(L, index) ? GFX_MIRROR : 0;
        return;
    }
    if (!lua_istable(L, index)) {
        return;
    }
    *mode = mode_from_name(L, string_option(L, index, "mode", "or"));
    if (int_option(L, index, "scale", 1) == 2) {
        *flags |= GFX_SCALE2X;
    }
    if (bool_option(L, index, "mirror", false)) {
        *flags |= GFX_MIRROR;
    }
}

/* ------------------------------------------------------------------------- */
/* Gráficos: VROM e o Guará                                                  */
/* ------------------------------------------------------------------------- */

/* oled.icon(nome, x, y [, opções]): bitmap da VROM; opções = espelhar ou {mode, scale, mirror}. */
static int l_icon(lua_State *L)
{
    const gfx_bitmap_t *bitmap = vrom(luaL_checkstring(L, 1));
    if (bitmap) {
        gfx_mode_t mode;
        unsigned flags;
        draw_options(L, 4, &mode, &flags);
        gfx_bitmap(game_runtime_framebuffer(), bitmap, int_arg(L, 2), int_arg(L, 3), mode, flags);
    }
    lua_pushboolean(L, bitmap != NULL);
    return 1;
}

/* oled.icon_size(nome) -> largura, altura (ou nil se o bitmap não existir). */
static int l_icon_size(lua_State *L)
{
    gfx_bitmap_t bitmap;
    if (!vrom_lookup(luaL_checkstring(L, 1), &bitmap)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, bitmap.width);
    lua_pushinteger(L, bitmap.height);
    return 2;
}

/*
 * guara.draw(x, y [, opções]): desenha o Guará do jogador com a base em `y` e
 * centralizado em `x`. Opções: {eye, scale, mirror, frame}. Sem `frame`, o
 * corpo anima sozinho (1, 2, 3 a 3 FPS). Retorna a altura desenhada.
 */
static int l_draw_guara(lua_State *L)
{
    if (!g_has_guara) {
        lua_pushinteger(L, 0);
        return 1;
    }
    const char *eye = string_option(L, 3, "eye", EYE_NORMAL);
    unsigned flags = 0;
    if (int_option(L, 3, "scale", 1) == 2) {
        flags |= GFX_SCALE2X;
    }
    if (bool_option(L, 3, "mirror", false)) {
        flags |= GFX_MIRROR;
    }
    int frame = int_option(L, 3, "frame", 0);
    if (frame < 1 || frame > 3) {
        frame = sprite_frame(app_ms());
    }
    int height = sprite_guara(game_runtime_framebuffer(), g_guara.color, g_guara.head, g_guara.stage, eye,
                              int_arg(L, 1), int_arg(L, 2), frame, flags);
    lua_pushinteger(L, height);
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Catálogo                                                                  */
/* ------------------------------------------------------------------------- */

/* Nomes dos tipos de registro por catálogo, na ordem das enumerações de types.h */
static const char *const ITEM_TYPE_NAMES[] = {"food", "drink", "potion"};
static const char *const EQUIPMENT_TYPE_NAMES[] = {"head", "body", "hand_atk", "hand_def", "feet", "accessory"};
static const char *const SKILL_TYPE_NAMES[] = {"attack", "heal", "effect"};

/* Nome do tipo do registro para o catálogo informado. */
static const char *type_name(catalog_t catalog, uint8_t type)
{
    switch (catalog) {
    case CAT_ITEMS: return type < 3 ? ITEM_TYPE_NAMES[type] : "?";
    case CAT_EQUIPMENT: return type < 6 ? EQUIPMENT_TYPE_NAMES[type] : "?";
    default: return type < 3 ? SKILL_TYPE_NAMES[type] : "?";
    }
}

/*
 * Empilha uma tabela com os dados do registro pedido por id ou slug, ou nil
 * se ele não existir: {id, slug, name, icon, type, description}.
 */
static int push_record(lua_State *L, catalog_t catalog)
{
    int id = lua_type(L, 1) == LUA_TSTRING ? catalog_find_slug(catalog, lua_tostring(L, 1)) : int_arg(L, 1);
    catalog_record_t record;
    if (id < 0 || !catalog_read(catalog, id, &record)) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    lua_pushinteger(L, record.id);
    lua_setfield(L, -2, "id");
    lua_pushstring(L, record.slug);
    lua_setfield(L, -2, "slug");
    lua_pushstring(L, record.name);
    lua_setfield(L, -2, "name");
    lua_pushstring(L, record.icon);
    lua_setfield(L, -2, "icon");
    lua_pushstring(L, type_name(catalog, record.type));
    lua_setfield(L, -2, "type");
    lua_pushstring(L, record.description);
    lua_setfield(L, -2, "description");
    if (catalog == CAT_ITEMS) {
        /* quantidade possuída pelo Guará, útil para lojas e inventários de jogo */
        lua_pushinteger(L, id < COUNT_ITEMS ? g_guara.inventory[id] : 0);
        lua_setfield(L, -2, "owned");
    }
    return 1;
}

/* catalog.item(id ou slug) -> tabela ou nil. */
static int l_catalog_item(lua_State *L)
{
    return push_record(L, CAT_ITEMS);
}

/* catalog.equipment(id ou slug) -> tabela ou nil. */
static int l_catalog_equipment(lua_State *L)
{
    return push_record(L, CAT_EQUIPMENT);
}

/* catalog.skill(id ou slug) -> tabela ou nil. */
static int l_catalog_skill(lua_State *L)
{
    return push_record(L, CAT_SKILLS);
}

/* catalog.count("items" | "equipment" | "skills") -> quantidade de registros. */
static int l_catalog_count(lua_State *L)
{
    static const char *const kinds[] = {"items", "equipment", "skills", NULL};
    int kind = luaL_checkoption(L, 1, NULL, kinds);
    lua_pushinteger(L, catalog_record_count((catalog_t)kind));
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Interface: componentes de desenho                                         */
/* ------------------------------------------------------------------------- */

/* Índice (0..3) do componente com estado, a partir do campo `id` (1..4) das opções. */
static int widget_slot(lua_State *L, int options_index)
{
    int id = int_option(L, options_index, "id", 1);
    if (id < 1 || id > MAX_STATEFUL_WIDGETS) {
        luaL_error(L, "id de componente deve ser de 1 a %d", MAX_STATEFUL_WIDGETS);
    }
    return id - 1;
}

/* ui.title(texto): barra de título padrão (Y=0..11). */
static int l_ui_title(lua_State *L)
{
    ui_title(game_runtime_framebuffer(), luaL_checkstring(L, 1), app_ms());
    return 0;
}

/* ui.tabs({nomes}, selecionada): barra de abas padrão; `selecionada` começa em 1. */
static int l_ui_tabs(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    const char *names[12];
    int count = (int)lua_rawlen(L, 1);
    if (count > 12) {
        count = 12;
    }
    for (int i = 0; i < count; i++) {
        lua_rawgeti(L, 1, i + 1);
        names[i] = luaL_checkstring(L, -1);
        lua_pop(L, 1); /* a string continua viva dentro da tabela */
    }
    ui_tabs(game_runtime_framebuffer(), names, count, int_arg(L, 2) - 1, app_ms());
    return 0;
}

/* Contexto da ui.list: a tabela de itens está na pilha Lua no índice 1. */
typedef struct {
    lua_State *L;
} list_context_t;

/* Lê o item `position` da tabela: texto simples ou {text, icon, info}. */
static void list_item(int position, ui_item_t *item, void *ctx)
{
    lua_State *L = ((list_context_t *)ctx)->L;
    lua_rawgeti(L, 1, position + 1);
    if (lua_istable(L, -1)) {
        snprintf(item->text, sizeof(item->text), "%s", string_option(L, -1, "text", ""));
        snprintf(item->info, sizeof(item->info), "%s", string_option(L, -1, "info", ""));
        /* o ícone precisa sobreviver ao pop: usa o nome guardado pela VROM */
        const char *icon = string_option(L, -1, "icon", NULL);
        gfx_bitmap_t bitmap;
        static char icon_names[3][32];
        if (icon && vrom_lookup(icon, &bitmap)) {
            snprintf(icon_names[position % 3], sizeof(icon_names[0]), "%s", icon);
            item->icon = icon_names[position % 3];
        }
    } else {
        snprintf(item->text, sizeof(item->text), "%s", luaL_optstring(L, -1, ""));
    }
    lua_pop(L, 1);
}

/*
 * ui.list({itens}, selecionado [, opções]): lista padrão de sub-menu.
 * Itens são textos ou {text, icon, info}; `selecionado` começa em 1.
 * Opções: {id (1..4), y (padrão 12), rows (padrão 3)}.
 */
static int l_ui_list(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    ui_list_t *list = &list_states[widget_slot(L, 3)];
    list->total = (int)lua_rawlen(L, 1);
    if (list->total > MAX_LIST_ITEMS) {
        list->total = MAX_LIST_ITEMS;
    }
    list->sel = int_arg(L, 2) - 1;
    int y = int_option(L, 3, "y", UI_LIST_Y);
    int rows = int_option(L, 3, "rows", UI_LINES_VISIBLE);
    list_context_t ctx = {L};
    ui_list_draw(game_runtime_framebuffer(), list, y, rows, list_item, &ctx, app_ms());
    return 0;
}

/* ui.footer(texto_b, texto_a): rodapé padrão; nil esconde o botão correspondente. */
static int l_ui_footer(lua_State *L)
{
    ui_footer(game_runtime_framebuffer(), luaL_optstring(L, 1, NULL), luaL_optstring(L, 2, NULL), app_ms());
    return 0;
}

/* ui.progress(x, y, largura, altura, valor, máximo): barra de progresso padrão. */
static int l_ui_progress(lua_State *L)
{
    ui_progress(game_runtime_framebuffer(), int_arg(L, 1), int_arg(L, 2), int_arg(L, 3), int_arg(L, 4),
                int_arg(L, 5), int_arg(L, 6));
    return 0;
}

/* ui.scrollbar(y, altura, total, visíveis, topo): barra de rolagem em X=127; `topo` começa em 1. */
static int l_ui_scrollbar(lua_State *L)
{
    ui_scrollbar(game_runtime_framebuffer(), int_arg(L, 1), int_arg(L, 2), int_arg(L, 3), int_arg(L, 4),
                 int_arg(L, 5) - 1);
    return 0;
}

/* ui.marquee(x, y, largura, texto [, opções]): texto que rola quando não cabe. Opções: {id, color}. */
static int l_ui_marquee(lua_State *L)
{
    marquee_t *marquee = &marquee_states[widget_slot(L, 5)];
    int color = int_option(L, 5, "color", 1);
    ui_marquee_text(game_runtime_framebuffer(), int_arg(L, 1), int_arg(L, 2), int_arg(L, 3), luaL_checkstring(L, 4),
                    color == 0 ? GFX_BLACK : (color == 2 ? GFX_INVERT : GFX_WHITE), true, marquee, app_ms());
    return 0;
}

/* ui.paragraph(x, y, largura, max_linhas, texto [, centralizar]) -> linhas desenhadas. */
static int l_ui_paragraph(lua_State *L)
{
    int lines = ui_paragraph(game_runtime_framebuffer(), int_arg(L, 1), int_arg(L, 2), int_arg(L, 3), int_arg(L, 4),
                             luaL_checkstring(L, 5), lua_toboolean(L, 6));
    lua_pushinteger(L, lines);
    return 1;
}

/* ui.text_center(x, largura, y, texto [, cor]): texto centralizado num intervalo. */
static int l_ui_text_center(lua_State *L)
{
    gfx_text_center(game_runtime_framebuffer(), int_arg(L, 1), int_arg(L, 2), int_arg(L, 3), luaL_checkstring(L, 4),
                    color_arg(L, 5));
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Interface: componentes interativos (modais e teclado)                     */
/* ------------------------------------------------------------------------- */

/* Tipos de resposta que a função de retorno do jogo recebe */
typedef enum {
    ANSWER_NONE,     /* só avisa que fechou (alerta, detalhes) */
    ANSWER_BOOLEAN,  /* confirmação: true (A) ou false (B) */
    ANSWER_INDEX,    /* escolha: índice 1..n ou nil */
    ANSWER_NUMBER,   /* quantidade: valor ou nil */
} answer_kind_t;

/* Função de retorno pendente: referência no registro do Lua + tipo da resposta */
typedef struct {
    int callback_ref;
    answer_kind_t kind;
} pending_answer_t;

/* Respostas pendentes (modais podem ser abertos em sequência) */
static pending_answer_t pending[8];

/* Guarda a função de retorno (argumento `index`, opcional) e devolve o contexto do modal. */
static void *store_callback(lua_State *L, int index, answer_kind_t kind)
{
    for (int i = 0; i < 8; i++) {
        if (pending[i].callback_ref == 0) {
            if (lua_isfunction(L, index)) {
                lua_pushvalue(L, index);
                pending[i].callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
            } else {
                pending[i].callback_ref = LUA_NOREF;
            }
            pending[i].kind = kind;
            return &pending[i];
        }
    }
    luaL_error(L, "modais demais abertos ao mesmo tempo");
    return NULL;
}

/*
 * Chamada pelo sistema quando o modal fecha: empilha a resposta e chama a
 * função de retorno do jogo. Erros no retorno interrompem o jogo.
 */
static void deliver_answer(pending_answer_t *answer, int result, const char *text, bool has_text)
{
    lua_State *L = game_runtime_lua();
    int ref = answer->callback_ref;
    answer->callback_ref = 0;
    if (!L || ref == LUA_NOREF || ref == LUA_REFNIL || ref == 0) {
        return;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
    int nargs = 1;
    if (has_text) {
        if (text) {
            lua_pushstring(L, text);
        } else {
            lua_pushnil(L);
        }
    } else {
        switch (answer->kind) {
        case ANSWER_BOOLEAN: lua_pushboolean(L, result == 1); break;
        case ANSWER_INDEX:
            if (result >= 0) {
                lua_pushinteger(L, result + 1);
            } else {
                lua_pushnil(L);
            }
            break;
        case ANSWER_NUMBER:
            if (result >= 0) {
                lua_pushinteger(L, result);
            } else {
                lua_pushnil(L);
            }
            break;
        default: nargs = 0; break;
        }
    }
    if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
        game_runtime_fail(lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

/* Retorno numérico dos modais do sistema. */
static void on_modal_answer(int result, void *ctx)
{
    deliver_answer(ctx, result, NULL, false);
}

/* Retorno de texto do teclado virtual. */
static void on_keyboard_answer(const char *text, void *ctx)
{
    deliver_answer(ctx, 0, text, true);
}

/* ui.alert(título, mensagem [, ícone [, ao_fechar]]): alerta com o botão A. */
static int l_ui_alert(lua_State *L)
{
    void *ctx = store_callback(L, 4, ANSWER_NONE);
    ui_alert(luaL_checkstring(L, 1), luaL_optstring(L, 3, NULL), luaL_checkstring(L, 2), "OK", on_modal_answer, ctx);
    return 0;
}

/* ui.confirm(título, mensagem, ao_responder(sim) [, texto_nao, texto_sim]). */
static int l_ui_confirm(lua_State *L)
{
    void *ctx = store_callback(L, 3, ANSWER_BOOLEAN);
    ui_confirm(luaL_checkstring(L, 1), NULL, luaL_checkstring(L, 2), luaL_optstring(L, 4, "NAO"),
               luaL_optstring(L, 5, "SIM"), on_modal_answer, ctx);
    return 0;
}

/* ui.choice(título, {opções}, ao_escolher(indice_ou_nil)): lista de até 8 opções. */
static int l_ui_choice(lua_State *L)
{
    luaL_checktype(L, 2, LUA_TTABLE);
    const char *options[MAX_CHOICE_OPTIONS];
    int count = (int)lua_rawlen(L, 2);
    if (count > MAX_CHOICE_OPTIONS) {
        count = MAX_CHOICE_OPTIONS;
    }
    for (int i = 0; i < count; i++) {
        lua_rawgeti(L, 2, i + 1);
        options[i] = luaL_checkstring(L, -1);
        lua_pop(L, 1);
    }
    void *ctx = store_callback(L, 3, ANSWER_INDEX);
    /* o modal copia os textos, então eles podem ser descartados pelo Lua depois */
    ui_choice(luaL_checkstring(L, 1), options, count, on_modal_answer, ctx);
    return 0;
}

/* ui.quantity(título, mínimo, máximo, inicial, ao_escolher(valor_ou_nil)). */
static int l_ui_quantity(lua_State *L)
{
    void *ctx = store_callback(L, 5, ANSWER_NUMBER);
    ui_quantity_picker(luaL_checkstring(L, 1), int_arg(L, 2), int_arg(L, 3), int_arg(L, 4), on_modal_answer, ctx);
    return 0;
}

/* ui.details(título, texto): texto rolável sem rodapé (B ou A fecham). */
static int l_ui_details(lua_State *L)
{
    ui_details(luaL_checkstring(L, 1), luaL_checkstring(L, 2));
    return 0;
}

/* ui.keyboard(título, inicial, tamanho_max, ao_concluir(texto_ou_nil)): teclado virtual. */
static int l_ui_keyboard(lua_State *L)
{
    void *ctx = store_callback(L, 4, ANSWER_NONE);
    ui_keyboard(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""), int_arg(L, 3), on_keyboard_answer, ctx);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Registro                                                                  */
/* ------------------------------------------------------------------------- */

/* Acrescenta funções a uma tabela global já existente (cria a tabela se preciso). */
static void extend_module(lua_State *L, const char *name, const luaL_Reg *functions)
{
    lua_getglobal(L, name);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setglobal(L, name);
    }
    luaL_setfuncs(L, functions, 0);
    lua_pop(L, 1);
}

void game_api_register(lua_State *L)
{
    static const luaL_Reg oled[] = {{"icon", l_icon}, {"icon_size", l_icon_size}, {NULL, NULL}};
    static const luaL_Reg guara[] = {{"draw", l_draw_guara}, {NULL, NULL}};
    static const luaL_Reg catalog[] = {
        {"item", l_catalog_item}, {"equipment", l_catalog_equipment}, {"skill", l_catalog_skill},
        {"count", l_catalog_count}, {NULL, NULL},
    };
    static const luaL_Reg ui[] = {
        {"title", l_ui_title}, {"tabs", l_ui_tabs}, {"list", l_ui_list}, {"footer", l_ui_footer},
        {"progress", l_ui_progress}, {"scrollbar", l_ui_scrollbar}, {"marquee", l_ui_marquee},
        {"paragraph", l_ui_paragraph}, {"text_center", l_ui_text_center}, {"alert", l_ui_alert},
        {"confirm", l_ui_confirm}, {"choice", l_ui_choice}, {"quantity", l_ui_quantity},
        {"details", l_ui_details}, {"keyboard", l_ui_keyboard}, {NULL, NULL},
    };
    memset(pending, 0, sizeof(pending));
    extend_module(L, "oled", oled);
    extend_module(L, "guara", guara);
    extend_module(L, "catalog", catalog);
    extend_module(L, "ui", ui);
}
