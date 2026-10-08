#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "app.h"
#include "arquivos.h"
#include "audio.h"
#include "botoes.h"
#include "componentes.h"
#include "config.h"
#include "ini.h"
#include "midi.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "modal.h"
#include "regras.h"
#include "sdcard.h"
#include "telas.h"
#include "wifi.h"

/*
 * Console virtual (README > Jogos): jogos em /sdcard/games/<slug>/ (game.ini +
 * main.lua) ou scripts .lua avulsos, executados numa VM Lua 5.4 a 30 FPS com
 * init(), update(dt) e draw(). Segure B por 2 s (ou chame sair()) para sair.
 */

#define PASTA_JOGOS SD_RAIZ "/games"
#define LUA_MEMORIA_MAX (120 * 1024)
#define SEGURAR_B_SAIR_MS 2000

/* ========================================================================= */
/* Lista                                                                     */
/* ========================================================================= */

typedef struct {
    char nome[64];   /* pasta ou arquivo */
    char titulo[40];
    char icone[160]; /* caminho do icon.bmp, vazio = ícone padrão */
    bool pasta;
} jogo_t;

static jogo_t *jogos;
static ui_lista_t lista;
static gfx_bitmap_t icones[3];
static int icones_de[3] = {-1, -1, -1};

static void liberar_icones(void)
{
    for (int i = 0; i < 3; i++) {
        arquivos_liberar_bmp(&icones[i]);
        icones_de[i] = -1;
    }
}

static void recarregar(void)
{
    static const char *const ext[] = {".lua", NULL};
    free(jogos);
    jogos = NULL;
    lista.total = 0;
    liberar_icones();
    lista_arquivos_t *l = arquivos_listar(PASTA_JOGOS, ext, true, 64);
    if (!l) {
        return;
    }
    jogos = calloc((size_t)(l->quantidade ? l->quantidade : 1), sizeof(jogo_t));
    for (int i = 0; jogos && i < l->quantidade; i++) {
        const entrada_t *e = &l->itens[i];
        jogo_t *j = &jogos[lista.total];
        snprintf(j->nome, sizeof(j->nome), "%s", e->nome);
        j->pasta = e->pasta;
        if (e->pasta) {
            char c[200];
            struct stat st;
            snprintf(c, sizeof(c), "%s/%s/main.lua", PASTA_JOGOS, e->nome);
            if (stat(c, &st) != 0) {
                continue;
            }
            snprintf(c, sizeof(c), "%s/%s/game.ini", PASTA_JOGOS, e->nome);
            size_t tam;
            char *ini = (char *)arquivos_ler(c, &tam, 4096);
            char icone[48] = "icon.bmp";
            if (!ini || !ini_texto(ini, "GAME", "title", j->titulo, sizeof(j->titulo))) {
                arquivos_nome_amigavel(e->nome, j->titulo, sizeof(j->titulo));
            }
            if (ini) {
                ini_texto(ini, "GAME", "icon", icone, sizeof(icone));
                free(ini);
            }
            snprintf(j->icone, sizeof(j->icone), "%s/%s/%s", PASTA_JOGOS, e->nome, icone);
        } else {
            arquivos_nome_amigavel(e->nome, j->titulo, sizeof(j->titulo));
        }
        lista.total++;
    }
    free(l);
}

static void item(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    it->icone = jogos[i].pasta ? "games" : "game_item";
    snprintf(it->texto, sizeof(it->texto), "%s", jogos[i].titulo);
}

static void desenhar_lista(gfx_t *g, uint32_t agora)
{
    ui_titulo(g, "JOGOS", agora);
    if (!sd_montado()) {
        gfx_texto_centro(g, 0, GFX_LARGURA, 32, "Insira o cartão SD", GFX_BRANCO);
        return;
    }
    if (lista.total == 0) {
        gfx_texto_centro(g, 0, GFX_LARGURA, 28, "Nenhum jogo em", GFX_BRANCO);
        gfx_texto_centro(g, 0, GFX_LARGURA, 38, "/games", GFX_BRANCO);
        return;
    }
    ui_lista_desenhar(g, &lista, UI_TITULO_ALTURA, UI_LINHAS_VISIVEIS, item, NULL, agora);
    /* Ícones personalizados (icon.bmp 16x16) por cima do ícone padrão */
    for (int k = 0; k < 3 && lista.topo + k < lista.total; k++) {
        int i = lista.topo + k;
        if (!jogos[i].icone[0]) {
            continue;
        }
        if (icones_de[k] != i) {
            arquivos_liberar_bmp(&icones[k]);
            icones_de[k] = i;
            if (!arquivos_carregar_bmp(jogos[i].icone, &icones[k]) || icones[k].largura > 16 || icones[k].altura > 16) {
                arquivos_liberar_bmp(&icones[k]);
            }
        }
        if (icones[k].dados) {
            int y = UI_TITULO_ALTURA + k * UI_LINHA_ALTURA;
            gfx_retangulo_cheio(g, 4, y + 1, 16, 14, GFX_PRETO);
            gfx_bitmap(g, &icones[k], 4, y, GFX_OU, 0);
        }
    }
}

static void tecla_lista(const evento_tecla_t *ev)
{
    if (ui_lista_tecla(&lista, ev, UI_LINHAS_VISIVEIS) || ev->tipo != TECLA_PRESSIONOU) {
        return;
    }
    if (ev->tecla == TECLA_B) {
        ui_voltar();
    } else if (ev->tecla == TECLA_A && lista.total) {
        char c[160];
        snprintf(c, sizeof(c), "%s/%s", PASTA_JOGOS, jogos[lista.sel].nome);
        telas_executar_jogo(c);
    }
}

static void abrir_lista(void *param)
{
    (void)param;
    if (!sd_montado()) {
        sd_montar();
    }
    lista = (ui_lista_t){0};
    recarregar();
}

static void fechar_lista(void)
{
    free(jogos);
    jogos = NULL;
    liberar_icones();
}

const tela_t TELA_JOGOS = {
    .nome = "jogos",
    .abrir = abrir_lista,
    .fechar = fechar_lista,
    .tecla = tecla_lista,
    .desenhar = desenhar_lista,
};

/* ========================================================================= */
/* Execução                                                                  */
/* ========================================================================= */

typedef struct {
    int custo_energia, custo_alimentacao, ganho_humor, ganho_xp;
    int foco_atributo, pontos_atributo;
    int afinidade, pontos_afinidade;
    int drop_item, drop_intervalo;
} efeitos_t;

static struct {
    lua_State *L;
    size_t memoria;
    char pasta[160];
    char erro[160];
    bool sair;
    bool tem_erro;
    gfx_t *g;
    bool segurando[QTD_TECLAS], anterior[QTD_TECLAS];
    uint32_t b_desde;
    uint32_t inicio, ultimo_quadro, ultimo_minuto;
    int minutos;
    efeitos_t ef;
    gfx_bitmap_t splash;
    uint32_t splash_ate;
    gfx_bitmap_t folhas[4];
} vm;

static void *alocador(void *ud, void *ptr, size_t antigo, size_t novo)
{
    (void)ud;
    if (ptr == NULL) {
        antigo = 0;
    }
    if (novo == 0) {
        free(ptr);
        vm.memoria -= antigo;
        return NULL;
    }
    if (vm.memoria - antigo + novo > LUA_MEMORIA_MAX) {
        return NULL; /* a VM gera "not enough memory" */
    }
    void *p = realloc(ptr, novo);
    if (p) {
        vm.memoria = vm.memoria - antigo + novo;
    }
    return p;
}

/* Coordenadas aceitam números fracionários (truncados), comuns em física com dt. */
static int arg_int(lua_State *L, int i)
{
    return (int)luaL_checknumber(L, i);
}

static int arg_opt(lua_State *L, int i, int padrao)
{
    return (int)luaL_optnumber(L, i, padrao);
}

static int cor_arg(lua_State *L, int i)
{
    int c = arg_opt(L, i, 1);
    return c == 0 ? GFX_PRETO : (c == 2 ? GFX_INVERTER : GFX_BRANCO);
}

/* ---------------- oled ---------------- */

static int l_clear(lua_State *L) { (void)L; gfx_limpar(vm.g); return 0; }
static int l_pixel(lua_State *L)
{
    gfx_pixel(vm.g, arg_int(L, 1), arg_int(L, 2), cor_arg(L, 3));
    return 0;
}
static int l_line(lua_State *L)
{
    gfx_linha(vm.g, arg_int(L, 1), arg_int(L, 2), arg_int(L, 3),
              arg_int(L, 4), cor_arg(L, 5));
    return 0;
}
static int l_rect(lua_State *L)
{
    gfx_retangulo(vm.g, arg_int(L, 1), arg_int(L, 2), arg_int(L, 3),
                  arg_int(L, 4), cor_arg(L, 5));
    return 0;
}
static int l_fill(lua_State *L)
{
    gfx_retangulo_cheio(vm.g, arg_int(L, 1), arg_int(L, 2), arg_int(L, 3),
                        arg_int(L, 4), cor_arg(L, 5));
    return 0;
}
static int l_circle(lua_State *L)
{
    gfx_circulo(vm.g, arg_int(L, 1), arg_int(L, 2), arg_int(L, 3), cor_arg(L, 4));
    return 0;
}
static int l_fillcircle(lua_State *L)
{
    gfx_circulo_cheio(vm.g, arg_int(L, 1), arg_int(L, 2), arg_int(L, 3),
                      cor_arg(L, 4));
    return 0;
}
static int l_text(lua_State *L)
{
    int w = gfx_texto(vm.g, arg_int(L, 1), arg_int(L, 2), luaL_checkstring(L, 3), cor_arg(L, 4));
    lua_pushinteger(L, w);
    return 1;
}
static int l_textwidth(lua_State *L)
{
    lua_pushinteger(L, gfx_largura_texto(luaL_checkstring(L, 1)));
    return 1;
}
/* oled.icon(nome, x, y [, espelhar]) desenha um bitmap da VROM */
static int l_icon(lua_State *L)
{
    const gfx_bitmap_t *b = vrom(luaL_checkstring(L, 1));
    if (b) {
        gfx_bitmap(vm.g, b, arg_int(L, 2), arg_int(L, 3), GFX_OU,
                   lua_toboolean(L, 4) ? GFX_ESPELHAR : 0);
    }
    return 0;
}
/* oled.load("sprites.bmp") -> id ; oled.sprite(id, sx, sy, w, h, x, y [, espelhar]) */
static int l_load(lua_State *L)
{
    char c[200];
    snprintf(c, sizeof(c), "%s/%s", vm.pasta, luaL_checkstring(L, 1));
    for (int i = 0; i < 4; i++) {
        if (!vm.folhas[i].dados) {
            if (!arquivos_carregar_bmp(c, &vm.folhas[i])) {
                return luaL_error(L, "não foi possível carregar %s (BMP 1-bit)", c);
            }
            lua_pushinteger(L, i + 1);
            return 1;
        }
    }
    return luaL_error(L, "limite de 4 imagens carregadas");
}
static int l_sprite(lua_State *L)
{
    int id = arg_int(L, 1) - 1;
    if (id < 0 || id >= 4 || !vm.folhas[id].dados) {
        return luaL_error(L, "imagem inválida");
    }
    const gfx_bitmap_t *b = &vm.folhas[id];
    int sx = arg_opt(L, 2, 0), sy = arg_opt(L, 3, 0);
    int w = arg_opt(L, 4, b->largura), h = arg_opt(L, 5, b->altura);
    int x = arg_opt(L, 6, 0), y = arg_opt(L, 7, 0);
    bool espelhar = lua_toboolean(L, 8);
    int passo = (b->largura + 7) / 8;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int bx = sx + i, by = sy + j;
            if (bx < 0 || by < 0 || bx >= b->largura || by >= b->altura) {
                continue;
            }
            if ((b->dados[by * passo + (bx >> 3)] >> (7 - (bx & 7))) & 1) {
                gfx_pixel(vm.g, x + (espelhar ? w - 1 - i : i), y + j, GFX_BRANCO);
            }
        }
    }
    return 0;
}

/* ---------------- btn ---------------- */

static int btn_estado(lua_State *L, tecla_t t)
{
    lua_pushboolean(L, vm.segurando[t]);
    return 1;
}
static int l_up(lua_State *L) { return btn_estado(L, TECLA_CIMA); }
static int l_down(lua_State *L) { return btn_estado(L, TECLA_BAIXO); }
static int l_left(lua_State *L) { return btn_estado(L, TECLA_ESQ); }
static int l_right(lua_State *L) { return btn_estado(L, TECLA_DIR); }
static int l_a(lua_State *L) { return btn_estado(L, TECLA_A); }
static int l_b(lua_State *L) { return btn_estado(L, TECLA_B); }
/* btn.pressed("a") verdadeiro só no quadro em que a tecla desceu */
static int l_pressed(lua_State *L)
{
    static const char *const nomes[] = {"up", "down", "left", "right", "a", "b", NULL};
    int t = luaL_checkoption(L, 1, NULL, nomes);
    lua_pushboolean(L, vm.segurando[t] && !vm.anterior[t]);
    return 1;
}

/* ---------------- audio ---------------- */

static int l_beep(lua_State *L)
{
    audio_tom((uint16_t)luaL_optinteger(L, 1, 1000), (uint16_t)luaL_optinteger(L, 2, 50));
    return 0;
}
static int l_note(lua_State *L)
{
    audio_tom(midi_frequencia(arg_int(L, 1)), (uint16_t)luaL_optinteger(L, 2, 150));
    return 0;
}
static int l_play_midi(lua_State *L)
{
    char c[200];
    snprintf(c, sizeof(c), "%s/%s", vm.pasta, luaL_checkstring(L, 1));
    telas_tocar_musica_fundo(c);
    return 0;
}
static int l_stop(lua_State *L)
{
    (void)L;
    audio_parar_musica();
    return 0;
}

/* ---------------- guara ---------------- */

static void campo(lua_State *L, const char *nome, lua_Integer v)
{
    lua_pushinteger(L, v);
    lua_setfield(L, -2, nome);
}

static int l_get_status(lua_State *L)
{
    const guara_t *g = &g_guara;
    lua_newtable(L);
    lua_pushstring(L, g->nome);
    lua_setfield(L, -2, "nome");
    lua_pushstring(L, natureza_nome(g->natureza));
    lua_setfield(L, -2, "natureza");
    campo(L, "nivel", g->nivel);
    campo(L, "xp", g->xp);
    campo(L, "hp", g->hp);
    campo(L, "hp_max", g->hp_max);
    campo(L, "mp", g->mp);
    campo(L, "mp_max", g->mp_max);
    campo(L, "energia", g->energia / 100);
    campo(L, "humor", g->humor / 100);
    campo(L, "alimentacao", g->alimentacao / 100);
    campo(L, "saude", g->saude / 100);
    static const char *const atr[] = {"atk", "def", "for", "agi", "int", "sor"};
    for (int i = 0; i < QTD_ATRIBUTOS; i++) {
        campo(L, atr[i], guara_atributo(g, (alvo_efeito_t)(ATR_ATK + i)));
    }
    return 1;
}

static int l_give_item(lua_State *L)
{
    int id = lua_type(L, 1) == LUA_TSTRING ? catalogo_buscar_slug(CAT_ITENS, lua_tostring(L, 1)) : arg_int(L, 1);
    int qtd = arg_opt(L, 2, 1);
    if (id < 0 || id >= catalogo_quantidade(CAT_ITENS) || id >= QTD_ITENS) {
        return luaL_error(L, "item inexistente");
    }
    int novo = g_guara.inventario[id] + qtd;
    g_guara.inventario[id] = (uint8_t)(novo < 0 ? 0 : (novo > 255 ? 255 : novo));
    app_alterou();
    return 0;
}

static const char *const NOMES_ALVO[] = {"hp", "mp", "energia", "humor", "alimentacao", "saude",
                                         "atk", "def", "for", "agi", "int", "sor", NULL};

static void somar_alvo(int alvo, int valor)
{
    guara_t *g = &g_guara;
    switch (alvo) {
    case VITAL_HP: g->hp = (uint16_t)(g->hp + valor < 0 ? 0 : (g->hp + valor > g->hp_max ? g->hp_max : g->hp + valor)); break;
    case VITAL_MP: g->mp = (uint16_t)(g->mp + valor < 0 ? 0 : (g->mp + valor > g->mp_max ? g->mp_max : g->mp + valor)); break;
    case VITAL_ENERGIA:
    case VITAL_HUMOR:
    case VITAL_ALIMENTACAO:
    case VITAL_SAUDE: {
        uint16_t *v = alvo == VITAL_ENERGIA ? &g->energia : alvo == VITAL_HUMOR ? &g->humor
                    : alvo == VITAL_ALIMENTACAO ? &g->alimentacao : &g->saude;
        int n = *v + CENTI(valor);
        *v = (uint16_t)(n < 0 ? 0 : (n > VITAL_MAX ? VITAL_MAX : n));
        break;
    }
    default: {
        int i = ATR_INDICE(alvo);
        int n = g->atributos[i] + valor;
        g->atributos[i] = (uint16_t)(n < 1 ? 1 : (n > 999 ? 999 : n));
        break;
    }
    }
    app_alterou();
}

static int l_add_stat(lua_State *L)
{
    somar_alvo(luaL_checkoption(L, 1, NULL, NOMES_ALVO), arg_int(L, 2));
    return 0;
}

static int l_add_exp(lua_State *L)
{
    lua_pushinteger(L, app_ganhar_xp((uint32_t)luaL_checkinteger(L, 1)));
    return 1;
}

static int nome_para_elemento(const char *nome)
{
    static const char *const nomes[] = {"fogo", "terra", "agua", "ar", "neutro", "neutra"};
    for (int i = 0; i < 6; i++) {
        if (strcmp(nome, nomes[i]) == 0) {
            return i > 4 ? NAT_NEUTRA : i;
        }
    }
    return -1;
}

static int l_add_affinity(lua_State *L)
{
    const char *e = luaL_checkstring(L, 1);
    int pontos = arg_int(L, 2);
    if (strcmp(e, "luz") == 0 || strcmp(e, "sombra") == 0) {
        /* README: luz/sombra movem o medidor Dia x Noite */
        guara_medidor(&g_guara, MED_DIA, (strcmp(e, "luz") == 0 ? 1 : -1) * pontos * 100);
    } else if (strcmp(e, "mente") == 0) {
        somar_alvo(ATR_INT, pontos); /* "pontos mentais" -> INT (Psíquica) */
    } else {
        int el = nome_para_elemento(e);
        if (el < 0) {
            return luaL_error(L, "elemento inválido: %s", e);
        }
        guara_afinidade(&g_guara, (natureza_t)el, CENTI(pontos));
    }
    app_alterou();
    return 0;
}

static int l_report_result(lua_State *L)
{
    bool venceu = lua_toboolean(L, 1);
    guara_medidor(&g_guara, MED_VITORIAS, venceu ? MED_PASSO_VITORIA : -MED_PASSO_VITORIA);
    if (!venceu) {
        guara_medidor(&g_guara, MED_RAIVA, -MED_PASSO_TRISTEZA);
    }
    app_alterou();
    return 0;
}

/* ---------------- storage (isolado na pasta do jogo) ---------------- */

static bool chave_valida(const char *k)
{
    if (!*k || strlen(k) > 24) {
        return false;
    }
    for (; *k; k++) {
        if (!((*k >= 'a' && *k <= 'z') || (*k >= 'A' && *k <= 'Z') || (*k >= '0' && *k <= '9') || *k == '_')) {
            return false;
        }
    }
    return true;
}

static int l_save(lua_State *L)
{
    const char *k = luaL_checkstring(L, 1);
    size_t n;
    const char *v = luaL_checklstring(L, 2, &n);
    if (!chave_valida(k)) {
        return luaL_error(L, "chave inválida");
    }
    char c[220];
    snprintf(c, sizeof(c), "%s/%s.sav", vm.pasta, k);
    FILE *f = fopen(c, "wb");
    bool ok = f && fwrite(v, 1, n, f) == n;
    if (f) {
        fclose(f);
    }
    lua_pushboolean(L, ok);
    return 1;
}

static int l_load_save(lua_State *L)
{
    const char *k = luaL_checkstring(L, 1);
    if (!chave_valida(k)) {
        return luaL_error(L, "chave inválida");
    }
    char c[220];
    snprintf(c, sizeof(c), "%s/%s.sav", vm.pasta, k);
    size_t n;
    char *d = (char *)arquivos_ler(c, &n, 16 * 1024);
    if (!d) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, d, n);
    free(d);
    return 1;
}

static int l_sair(lua_State *L)
{
    (void)L;
    vm.sair = true;
    return 0;
}

static int l_millis(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)(app_ms() - vm.inicio));
    return 1;
}

static void registrar(lua_State *L, const char *nome, const luaL_Reg *f)
{
    lua_newtable(L);
    luaL_setfuncs(L, f, 0);
    lua_setglobal(L, nome);
}

static void abrir_bibliotecas(lua_State *L)
{
    static const luaL_Reg libs[] = {
        {LUA_GNAME, luaopen_base}, {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string},
        {LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8}, {LUA_COLIBNAME, luaopen_coroutine},
        {NULL, NULL},
    };
    for (const luaL_Reg *lib = libs; lib->func; lib++) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
    /* Sandbox: sem acesso a arquivos arbitrários */
    lua_pushnil(L);
    lua_setglobal(L, "dofile");
    lua_pushnil(L);
    lua_setglobal(L, "loadfile");

    static const luaL_Reg oled[] = {
        {"clear", l_clear}, {"pixel", l_pixel}, {"line", l_line}, {"rect", l_rect}, {"fill", l_fill},
        {"circle", l_circle}, {"fillcircle", l_fillcircle}, {"text", l_text}, {"textwidth", l_textwidth},
        {"icon", l_icon}, {"load", l_load}, {"sprite", l_sprite}, {NULL, NULL},
    };
    static const luaL_Reg btn[] = {
        {"up", l_up}, {"down", l_down}, {"left", l_left}, {"right", l_right},
        {"a", l_a}, {"b", l_b}, {"pressed", l_pressed}, {NULL, NULL},
    };
    static const luaL_Reg audio[] = {
        {"beep", l_beep}, {"note", l_note}, {"play_midi", l_play_midi}, {"stop", l_stop}, {NULL, NULL},
    };
    static const luaL_Reg guara[] = {
        {"get_status", l_get_status}, {"give_item", l_give_item}, {"add_stat", l_add_stat}, {"add_exp", l_add_exp},
        {"add_affinity", l_add_affinity}, {"report_result", l_report_result}, {NULL, NULL},
    };
    static const luaL_Reg storage[] = {{"save", l_save}, {"load", l_load_save}, {NULL, NULL}};
    registrar(L, "oled", oled);
    registrar(L, "btn", btn);
    registrar(L, "audio", audio);
    registrar(L, "guara", guara);
    lua_getglobal(L, "guara");
    lua_setglobal(L, "pet"); /* README: módulo `guara` (ou `pet`) */
    registrar(L, "storage", storage);
    lua_pushcfunction(L, l_sair);
    lua_setglobal(L, "sair");
    lua_pushcfunction(L, l_millis);
    lua_setglobal(L, "millis");
    lua_pushinteger(L, GFX_LARGURA);
    lua_setglobal(L, "WIDTH");
    lua_pushinteger(L, GFX_ALTURA);
    lua_setglobal(L, "HEIGHT");
}

static bool chamar(const char *funcao, int nargs)
{
    lua_State *L = vm.L;
    lua_getglobal(L, funcao);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1 + nargs);
        return true;
    }
    if (nargs) {
        lua_insert(L, -1 - nargs);
    }
    if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
        snprintf(vm.erro, sizeof(vm.erro), "%s", lua_tostring(L, -1));
        lua_pop(L, 1);
        vm.tem_erro = true;
        return false;
    }
    return true;
}

static void aplicar_efeitos_minuto(void)
{
    const efeitos_t *e = &vm.ef;
    vm.minutos++;
    somar_alvo(VITAL_ENERGIA, -e->custo_energia);
    somar_alvo(VITAL_ALIMENTACAO, -e->custo_alimentacao);
    somar_alvo(VITAL_HUMOR, e->ganho_humor);
    if (e->ganho_xp) {
        app_ganhar_xp((uint32_t)e->ganho_xp);
    }
    if (e->foco_atributo >= 0 && e->pontos_atributo) {
        somar_alvo(e->foco_atributo, e->pontos_atributo);
    }
    if (e->afinidade >= 0 && e->pontos_afinidade) {
        guara_afinidade(&g_guara, (natureza_t)e->afinidade, CENTI(e->pontos_afinidade));
    }
    if (e->drop_item >= 0 && e->drop_intervalo > 0 && vm.minutos % e->drop_intervalo == 0 && e->drop_item < QTD_ITENS &&
        g_guara.inventario[e->drop_item] < 255) {
        g_guara.inventario[e->drop_item]++;
    }
    app_interagir();
}

static void ler_game_ini(const char *ini)
{
    efeitos_t *e = &vm.ef;
    memset(e, 0, sizeof(*e));
    e->foco_atributo = e->afinidade = e->drop_item = -1;
    if (!ini) {
        return;
    }
    const char *s = "GUARA_EFFECTS";
    e->custo_energia = ini_inteiro(ini, s, "cost_energy_per_min", 0);
    e->custo_alimentacao = ini_inteiro(ini, s, "cost_food_per_min", 0);
    e->ganho_humor = ini_inteiro(ini, s, "gain_humor_per_min", 0);
    e->ganho_xp = ini_inteiro(ini, s, "gain_exp_per_min", 0);
    e->pontos_atributo = ini_inteiro(ini, s, "gain_stat_pts_per_min", 0);
    e->pontos_afinidade = ini_inteiro(ini, s, "affinity_pts_per_min", 0);
    e->drop_item = ini_inteiro(ini, s, "drop_item_id", -1);
    e->drop_intervalo = ini_inteiro(ini, s, "drop_item_interval_min", 0);
    char buf[16];
    if (ini_texto(ini, s, "gain_stat_focus", buf, sizeof(buf))) {
        for (int i = ATR_ATK; NOMES_ALVO[i]; i++) {
            if (strcasecmp(buf, NOMES_ALVO[i]) == 0) {
                e->foco_atributo = i;
            }
        }
    }
    if (ini_texto(ini, s, "affinity_elem", buf, sizeof(buf))) {
        for (char *p = buf; *p; p++) {
            *p = (char)(*p >= 'A' && *p <= 'Z' ? *p + 32 : *p);
        }
        e->afinidade = nome_para_elemento(buf);
    }
}

static void fechar_vm(void)
{
    if (vm.L) {
        lua_close(vm.L);
        vm.L = NULL;
    }
    for (int i = 0; i < 4; i++) {
        arquivos_liberar_bmp(&vm.folhas[i]);
    }
    arquivos_liberar_bmp(&vm.splash);
    app_bloquear_sono(false);
    if (g_cfg.wifi_ligado) {
        app_conectar_wifi_salvo(4000);
    }
}

static void jogo_tecla(const evento_tecla_t *ev)
{
    if (ev->tecla == TECLA_B) {
        vm.b_desde = ev->tipo == TECLA_PRESSIONOU ? app_ms() : (ev->tipo == TECLA_SOLTOU ? 0 : vm.b_desde);
    }
    if (vm.tem_erro && ev->tipo == TECLA_PRESSIONOU && (ev->tecla == TECLA_A || ev->tecla == TECLA_B)) {
        vm.sair = true;
    }
}

static void jogo_desenhar(gfx_t *g, uint32_t agora)
{
    vm.g = g;
    if (vm.sair || (vm.b_desde && agora - vm.b_desde > SEGURAR_B_SAIR_MS)) {
        vm.b_desde = 0;
        ui_voltar();
        return;
    }
    if (agora < vm.splash_ate) {
        if (vm.splash.dados) {
            bool pequeno = vm.splash.largura <= 16 && vm.splash.altura <= 16;
            int esc = pequeno ? 2 : 1, w = vm.splash.largura * esc, h = vm.splash.altura * esc;
            gfx_bitmap(g, &vm.splash, (GFX_LARGURA - w) / 2, (GFX_ALTURA - h) / 2, GFX_COPIA, pequeno ? GFX_ESCALA2 : 0);
        } else {
            ui_icone_modo(g, "games", 48, 16, GFX_OU, GFX_ESCALA2);
        }
        vm.ultimo_quadro = agora;
        return;
    }
    if (vm.tem_erro) {
        ui_titulo(g, "ERRO NO JOGO", agora);
        ui_paragrafo(g, 2, 14, 124, 5, vm.erro, false);
        return;
    }
    for (int i = 0; i < QTD_TECLAS; i++) {
        vm.anterior[i] = vm.segurando[i];
        vm.segurando[i] = botoes_segurando((tecla_t)i);
    }
    float dt = (float)(agora - vm.ultimo_quadro) / 1000.0f;
    vm.ultimo_quadro = agora;
    lua_pushnumber(vm.L, (lua_Number)dt);
    if (chamar("update", 1)) {
        gfx_limpar(g);
        chamar("draw", 0);
    }
    if (agora - vm.ultimo_minuto >= 60000) {
        vm.ultimo_minuto += 60000;
        aplicar_efeitos_minuto();
    }
}

static const tela_t TELA_JOGO = {
    .nome = "jogo",
    .fps = 30,
    .tecla = jogo_tecla,
    .desenhar = jogo_desenhar,
    .fechar = fechar_vm,
};

void telas_executar_jogo(const char *caminho)
{
    memset(&vm, 0, sizeof(vm));
    struct stat st;
    char principal[200];
    char *ini = NULL;
    size_t tam;
    if (stat(caminho, &st) == 0 && S_ISDIR(st.st_mode)) {
        snprintf(vm.pasta, sizeof(vm.pasta), "%s", caminho);
        snprintf(principal, sizeof(principal), "%s/main.lua", caminho);
        char c[200], splash[48] = "load.bmp";
        snprintf(c, sizeof(c), "%s/game.ini", caminho);
        ini = (char *)arquivos_ler(c, &tam, 4096);
        if (ini) {
            ini_texto(ini, "GAME", "splash", splash, sizeof(splash));
        }
        snprintf(c, sizeof(c), "%s/%s", caminho, splash);
        if (!arquivos_carregar_bmp(c, &vm.splash)) {
            char icone[48] = "icon.bmp";
            if (ini) {
                ini_texto(ini, "GAME", "icon", icone, sizeof(icone));
            }
            snprintf(c, sizeof(c), "%s/%s", caminho, icone);
            arquivos_carregar_bmp(c, &vm.splash); /* upscale 2x do ícone 16x16 */
        }
    } else {
        snprintf(principal, sizeof(principal), "%s", caminho);
        snprintf(vm.pasta, sizeof(vm.pasta), "%s", caminho);
        char *barra = strrchr(vm.pasta, '/');
        if (barra) {
            *barra = '\0';
        }
    }
    ler_game_ini(ini);
    free(ini);

    /* README: o Wi-Fi é descarregado antes do jogo para liberar ~60 KB */
    wifi_desligar();

    vm.L = lua_newstate(alocador, NULL);
    if (!vm.L) {
        ui_alerta("JOGOS", "games", "Sem memória para a VM Lua.", "OK", NULL, NULL);
        return;
    }
    lua_gc(vm.L, LUA_GCINC, 100, 250, 0); /* GC incremental agressivo */
    abrir_bibliotecas(vm.L);
    vm.inicio = vm.ultimo_quadro = vm.ultimo_minuto = app_ms();
    vm.splash_ate = vm.inicio + 1500;
    app_bloquear_sono(true);
    ui_abrir(&TELA_JOGO, NULL);

    gfx_t rascunho;
    gfx_iniciar(&rascunho);
    vm.g = &rascunho;
    if (luaL_loadfilex(vm.L, principal, "t") != LUA_OK) {
        snprintf(vm.erro, sizeof(vm.erro), "%s", lua_tostring(vm.L, -1));
        lua_pop(vm.L, 1);
        vm.tem_erro = true;
        return;
    }
    if (lua_pcall(vm.L, 0, 0, 0) != LUA_OK) {
        snprintf(vm.erro, sizeof(vm.erro), "%s", lua_tostring(vm.L, -1));
        lua_pop(vm.L, 1);
        vm.tem_erro = true;
        return;
    }
    chamar("init", 0);
}
