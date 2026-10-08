/*
 * Console virtual: lista de jogos do cartão e a VM Lua 5.4 que executa
 * init/update/draw a 30 FPS, com os módulos oled, btn, audio, guara e storage.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "app.h"
#include "files.h"
#include "audio.h"
#include "buttons.h"
#include "widgets.h"
#include "config.h"
#include "game_api.h"
#include "ini.h"
#include "midi.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "modal.h"
#include "rules.h"
#include "sdcard.h"
#include "screens.h"
#include "network.h"

/*
 * Console virtual (README > Jogos): jogos em /sdcard/games/<slug>/ (game.ini +
 * main.lua) ou scripts .lua avulsos, executados numa VM Lua 5.4 a 30 FPS com
 * init(), update(dt) e draw(). Segure B por 2 s (ou chame sair()) para sair.
 */

#define FOLDER_GAMES SD_ROOT "/games"
#define LUA_MEMORIA_MAX (120 * 1024)
#define HOLD_B_EXIT_MS 2000

/* ========================================================================= */
/* Lista                                                                     */
/* ========================================================================= */

typedef struct {
    char name[64];   /* pasta ou arquivo */
    char title[40];
    char icon[160]; /* caminho do icon.bmp, vazio = ícone padrão */
    bool folder;
} game_entry_t;

static game_entry_t *games;
static ui_list_t list;
static gfx_bitmap_t icons[3];
static int icons_from[3] = {-1, -1, -1};

/* Libera os ícones personalizados em cache. */
static void release_icons(void)
{
    for (int i = 0; i < 3; i++) {
        files_release_bmp(&icons[i]);
        icons_from[i] = -1;
    }
}

/* Relê a pasta /games (pastas com main.lua e scripts .lua avulsos). */
static void reload(void)
{
    static const char *const ext[] = {".lua", NULL};
    free(games);
    games = NULL;
    list.total = 0;
    release_icons();
    file_list_t *l = files_list(FOLDER_GAMES, ext, true, 64);
    if (!l) {
        return;
    }
    games = calloc((size_t)(l->count ? l->count : 1), sizeof(game_entry_t));
    for (int i = 0; games && i < l->count; i++) {
        const file_entry_t *e = &l->items[i];
        game_entry_t *j = &games[list.total];
        snprintf(j->name, sizeof(j->name), "%s", e->name);
        j->folder = e->folder;
        if (e->folder) {
            char c[200];
            struct stat st;
            snprintf(c, sizeof(c), "%s/%s/main.lua", FOLDER_GAMES, e->name);
            if (stat(c, &st) != 0) {
                continue;
            }
            snprintf(c, sizeof(c), "%s/%s/game.ini", FOLDER_GAMES, e->name);
            size_t length;
            char *ini = (char *)files_read(c, &length, 4096);
            char icon[48] = "icon.bmp";
            if (!ini || !ini_text(ini, "GAME", "title", j->title, sizeof(j->title))) {
                files_friendly_name(e->name, j->title, sizeof(j->title));
            }
            if (ini) {
                ini_text(ini, "GAME", "icon", icon, sizeof(icon));
                free(ini);
            }
            snprintf(j->icon, sizeof(j->icon), "%s/%s/%s", FOLDER_GAMES, e->name, icon);
        } else {
            files_friendly_name(e->name, j->title, sizeof(j->title));
        }
        list.total++;
    }
    free(l);
}

/* Item da lista de jogos. */
static void item(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    it->icon = games[i].folder ? "games" : "game_item";
    snprintf(it->text, sizeof(it->text), "%s", games[i].title);
}

/* Desenha a lista e os ícones personalizados (icon.bmp) das linhas visíveis. */
static void draw_list(gfx_t *g, uint32_t now)
{
    ui_title(g, "JOGOS", now);
    if (!sd_mounted()) {
        gfx_text_center(g, 0, GFX_WIDTH, 32, "Insira o cartão SD", GFX_WHITE);
        return;
    }
    if (list.total == 0) {
        gfx_text_center(g, 0, GFX_WIDTH, 28, "Nenhum jogo em", GFX_WHITE);
        gfx_text_center(g, 0, GFX_WIDTH, 38, "/games", GFX_WHITE);
        return;
    }
    ui_list_draw(g, &list, UI_LIST_Y, UI_LINES_VISIBLE, item, NULL, now);
    /* Ícones personalizados (icon.bmp 16x16) por cima do ícone padrão */
    for (int k = 0; k < 3 && list.top + k < list.total; k++) {
        int i = list.top + k;
        if (!games[i].icon[0]) {
            continue;
        }
        if (icons_from[k] != i) {
            files_release_bmp(&icons[k]);
            icons_from[k] = i;
            if (!files_load_bmp(games[i].icon, &icons[k]) || icons[k].width > 16 || icons[k].height > 16) {
                files_release_bmp(&icons[k]);
            }
        }
        if (icons[k].data) {
            int y = UI_LIST_Y + k * UI_LINE_HEIGHT;
            /* apaga o ícone padrão e põe o do jogo no lugar */
            gfx_rect_filled(g, 4, y + 1, 16, 14, GFX_BLACK);
            gfx_bitmap(g, &icons[k], 4, y, GFX_OR, 0);
        }
    }
}

/* Teclas da lista de jogos. */
static void list_on_key(const key_event_t *ev)
{
    if (ui_list_key(&list, ev, UI_LINES_VISIBLE) || ev->type != KEY_PRESSED) {
        return;
    }
    if (ev->key == KEY_B) {
        ui_back();
    } else if (ev->key == KEY_A && list.total) {
        char c[160];
        snprintf(c, sizeof(c), "%s/%s", FOLDER_GAMES, games[list.sel].name);
        screens_run_game(c);
    }
}

/* Monta o cartão (se preciso) e lista os jogos. */
static void open_list(void *param)
{
    (void)param;
    if (!sd_mounted()) {
        sd_mount();
    }
    list = (ui_list_t){0};
    reload();
}

/* Libera a lista e os ícones. */
static void close_list(void)
{
    free(games);
    games = NULL;
    release_icons();
}

const screen_t SCREEN_GAMES = {
    .name = "jogos",
    .on_open = open_list,
    .on_close = close_list,
    .on_key = list_on_key,
    .on_draw = draw_list,
};

/* ========================================================================= */
/* Execução                                                                  */
/* ========================================================================= */

typedef struct {
    int energy_cost, satiety_cost, mood_gain, xp_gain;
    int focus_attribute, attribute_points;
    int affinity, affinity_points;
    int drop_item, drop_interval;
} game_effects_t;

static struct {
    lua_State *L;
    size_t memoria;
    char folder[160];
    char error[160];
    bool exit_requested;
    bool has_error;
    gfx_t *g;
    bool holding[COUNT_KEYS], previous[COUNT_KEYS];
    bool resync_keys; /* voltou de um modal: ignora bordas de tecla neste quadro */
    uint32_t b_pressed_at;
    uint32_t start, last_frame, last_minute;
    int minutes;
    game_effects_t fx;
    gfx_bitmap_t splash;
    uint32_t splash_until;
    gfx_bitmap_t sheets[4];
} vm;

/* Declarada aqui porque game_draw precisa saber se o jogo está no topo da pilha */
static const screen_t SCREEN_GAME;

/* Alocador da VM Lua com limite de memória. */
static void *allocator(void *ud, void *ptr, size_t old_size, size_t incoming)
{
    (void)ud;
    if (ptr == NULL) {
        old_size = 0;
    }
    if (incoming == 0) {
        free(ptr);
        vm.memoria -= old_size;
        return NULL;
    }
    if (vm.memoria - old_size + incoming > LUA_MEMORIA_MAX) {
        return NULL; /* a VM gera "not enough memory" */
    }
    void *p = realloc(ptr, incoming);
    if (p) {
        vm.memoria = vm.memoria - old_size + incoming;
    }
    return p;
}

/* Coordenadas aceitam números fracionários (truncados), comuns em física com dt. */
static int lua_int_arg(lua_State *L, int i)
{
    return (int)luaL_checknumber(L, i);
}

/* Argumento numérico opcional como inteiro. */
static int lua_optional_int_arg(lua_State *L, int i, int fallback)
{
    return (int)luaL_optnumber(L, i, fallback);
}

/* Cor opcional: 0 preto, 1 branco (padrão), 2 inverter. */
static int lua_color_arg(lua_State *L, int i)
{
    int c = lua_optional_int_arg(L, i, 1);
    return c == 0 ? GFX_BLACK : (c == 2 ? GFX_INVERT : GFX_WHITE);
}

/* ---------------- oled ---------------- */

static int l_clear(lua_State *L) { (void)L; gfx_clear(vm.g); return 0; }
/* oled.pixel(x, y [, cor]). */
static int l_pixel(lua_State *L)
{
    gfx_pixel(vm.g, lua_int_arg(L, 1), lua_int_arg(L, 2), lua_color_arg(L, 3));
    return 0;
}
/* oled.line(x0, y0, x1, y1 [, cor]). */
static int l_line(lua_State *L)
{
    gfx_line(vm.g, lua_int_arg(L, 1), lua_int_arg(L, 2), lua_int_arg(L, 3),
              lua_int_arg(L, 4), lua_color_arg(L, 5));
    return 0;
}
/* oled.rect(x, y, l, a [, cor]). */
static int l_rect(lua_State *L)
{
    gfx_rect(vm.g, lua_int_arg(L, 1), lua_int_arg(L, 2), lua_int_arg(L, 3),
                  lua_int_arg(L, 4), lua_color_arg(L, 5));
    return 0;
}
/* oled.fill(x, y, l, a [, cor]). */
static int l_fill(lua_State *L)
{
    gfx_rect_filled(vm.g, lua_int_arg(L, 1), lua_int_arg(L, 2), lua_int_arg(L, 3),
                        lua_int_arg(L, 4), lua_color_arg(L, 5));
    return 0;
}
/* oled.circle(x, y, r [, cor]). */
static int l_circle(lua_State *L)
{
    gfx_circle(vm.g, lua_int_arg(L, 1), lua_int_arg(L, 2), lua_int_arg(L, 3), lua_color_arg(L, 4));
    return 0;
}
/* oled.fillcircle(x, y, r [, cor]). */
static int l_fillcircle(lua_State *L)
{
    gfx_circle_filled(vm.g, lua_int_arg(L, 1), lua_int_arg(L, 2), lua_int_arg(L, 3),
                      lua_color_arg(L, 4));
    return 0;
}
/* oled.text(x, y, texto [, cor]) -> largura. */
static int l_text(lua_State *L)
{
    int w = gfx_text(vm.g, lua_int_arg(L, 1), lua_int_arg(L, 2), luaL_checkstring(L, 3), lua_color_arg(L, 4));
    lua_pushinteger(L, w);
    return 1;
}
/* oled.textwidth(texto) -> largura em pixels. */
static int l_textwidth(lua_State *L)
{
    lua_pushinteger(L, gfx_text_width(luaL_checkstring(L, 1)));
    return 1;
}

/* oled.load("sprites.bmp") -> id ; oled.sprite(id, sx, sy, w, h, x, y [, espelhar]) */
static int l_load(lua_State *L)
{
    char c[200];
    snprintf(c, sizeof(c), "%s/%s", vm.folder, luaL_checkstring(L, 1));
    for (int i = 0; i < 4; i++) {
        if (!vm.sheets[i].data) {
            if (!files_load_bmp(c, &vm.sheets[i])) {
                return luaL_error(L, "não foi possível carregar %s (BMP 1-bit)", c);
            }
            lua_pushinteger(L, i + 1);
            return 1;
        }
    }
    return luaL_error(L, "limite de 4 imagens carregadas");
}
/* oled.sprite(id, sx, sy, l, a, x, y [, espelhar]): recorte de uma imagem carregada. */
static int l_sprite(lua_State *L)
{
    int id = lua_int_arg(L, 1) - 1;
    if (id < 0 || id >= 4 || !vm.sheets[id].data) {
        return luaL_error(L, "imagem inválida");
    }
    const gfx_bitmap_t *b = &vm.sheets[id];
    int sx = lua_optional_int_arg(L, 2, 0), sy = lua_optional_int_arg(L, 3, 0);
    int w = lua_optional_int_arg(L, 4, b->width), h = lua_optional_int_arg(L, 5, b->height);
    int x = lua_optional_int_arg(L, 6, 0), y = lua_optional_int_arg(L, 7, 0);
    bool mirror = lua_toboolean(L, 8);
    int step = (b->width + 7) / 8;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int bx = sx + i, by = sy + j;
            if (bx < 0 || by < 0 || bx >= b->width || by >= b->height) {
                continue;
            }
            if ((b->data[by * step + (bx >> 3)] >> (7 - (bx & 7))) & 1) {
                gfx_pixel(vm.g, x + (mirror ? w - 1 - i : i), y + j, GFX_WHITE);
            }
        }
    }
    return 0;
}

/* ---------------- btn ---------------- */

/* Empilha se a tecla está pressionada neste quadro. */
static int btn_state(lua_State *L, input_key_t t)
{
    lua_pushboolean(L, vm.holding[t]);
    return 1;
}
static int l_up(lua_State *L) { return btn_state(L, KEY_UP); }
static int l_down(lua_State *L) { return btn_state(L, KEY_DOWN); }
static int l_left(lua_State *L) { return btn_state(L, KEY_LEFT); }
static int l_right(lua_State *L) { return btn_state(L, KEY_RIGHT); }
static int l_a(lua_State *L) { return btn_state(L, KEY_A); }
static int l_b(lua_State *L) { return btn_state(L, KEY_B); }
/* btn.pressed("a") verdadeiro só no quadro em que a tecla desceu */
static int l_pressed(lua_State *L)
{
    static const char *const names[] = {"up", "down", "left", "right", "a", "b", NULL};
    int t = luaL_checkoption(L, 1, NULL, names);
    lua_pushboolean(L, vm.holding[t] && !vm.previous[t]);
    return 1;
}

/* ---------------- audio ---------------- */

/* audio.beep(freq, ms). */
static int l_beep(lua_State *L)
{
    audio_tone((uint16_t)luaL_optinteger(L, 1, 1000), (uint16_t)luaL_optinteger(L, 2, 50));
    return 0;
}
/* audio.note(nota_midi, ms). */
static int l_note(lua_State *L)
{
    audio_tone(midi_frequency(lua_int_arg(L, 1)), (uint16_t)luaL_optinteger(L, 2, 150));
    return 0;
}
/* audio.play_midi(arquivo): música de fundo da pasta do jogo. */
static int l_play_midi(lua_State *L)
{
    char c[200];
    snprintf(c, sizeof(c), "%s/%s", vm.folder, luaL_checkstring(L, 1));
    screens_play_background_music(c);
    return 0;
}
/* audio.stop(): para a música. */
static int l_stop(lua_State *L)
{
    (void)L;
    audio_stop_music();
    return 0;
}

/* ---------------- guara ---------------- */

/* Define um campo inteiro na tabela do topo da pilha. */
static void field(lua_State *L, const char *name, lua_Integer v)
{
    lua_pushinteger(L, v);
    lua_setfield(L, -2, name);
}

/* guara.get_status(): tabela com nome, natureza, nível, vitais e atributos. */
static int l_get_status(lua_State *L)
{
    const guara_t *g = &g_guara;
    lua_newtable(L);
    lua_pushstring(L, g->name);
    lua_setfield(L, -2, "name");
    lua_pushstring(L, nature_name(g->nature));
    lua_setfield(L, -2, "nature");
    lua_pushstring(L, g->color == COLOR_WHITE ? "white" : "black");
    lua_setfield(L, -2, "color");
    field(L, "head", g->head);
    field(L, "stage", g->stage);
    field(L, "level", g->level);
    field(L, "xp", g->xp);
    field(L, "hp", g->hp);
    field(L, "hp_max", g->hp_max);
    field(L, "mp", g->mp);
    field(L, "mp_max", g->mp_max);
    field(L, "energy", g->energy / 100);
    field(L, "mood", g->mood / 100);
    field(L, "satiety", g->satiety / 100);
    field(L, "health", g->health / 100);
    static const char *const attr[] = {"atk", "def", "str", "agi", "int", "luk"};
    for (int i = 0; i < ATTRIBUTE_COUNT; i++) {
        field(L, attr[i], guara_effective_attribute(g, (effect_target_t)(ATTR_ATK + i)));
    }
    return 1;
}

/* guara.give_item(id ou slug, quantidade). */
static int l_give_item(lua_State *L)
{
    int id = lua_type(L, 1) == LUA_TSTRING ? catalog_find_slug(CAT_ITEMS, lua_tostring(L, 1)) : lua_int_arg(L, 1);
    int amount = lua_optional_int_arg(L, 2, 1);
    if (id < 0 || id >= catalog_record_count(CAT_ITEMS) || id >= COUNT_ITEMS) {
        return luaL_error(L, "item inexistente");
    }
    int incoming = g_guara.inventory[id] + amount;
    g_guara.inventory[id] = (uint8_t)(incoming < 0 ? 0 : (incoming > 255 ? 255 : incoming));
    app_mark_changed();
    return 0;
}

/* Nomes aceitos por guara.add_stat e por gain_stat_focus (mesma ordem de effect_target_t) */
static const char *const NAMES_TARGET[] = {"hp", "mp", "energy", "mood", "satiety", "health",
                                           "atk", "def", "str", "agi", "int", "luk", NULL};

/* Soma pontos a um vital (HP, MP, %) ou atributo do Guará. */
static void add_to_target(int target, int value)
{
    guara_t *g = &g_guara;
    switch (target) {
    case VITAL_HP: g->hp = (uint16_t)(g->hp + value < 0 ? 0 : (g->hp + value > g->hp_max ? g->hp_max : g->hp + value)); break;
    case VITAL_MP: g->mp = (uint16_t)(g->mp + value < 0 ? 0 : (g->mp + value > g->mp_max ? g->mp_max : g->mp + value)); break;
    case VITAL_ENERGY:
    case VITAL_MOOD:
    case VITAL_SATIETY:
    case VITAL_HEALTH: {
        uint16_t *v = target == VITAL_ENERGY ? &g->energy : target == VITAL_MOOD ? &g->mood
                    : target == VITAL_SATIETY ? &g->satiety : &g->health;
        int n = *v + CENTI(value);
        *v = (uint16_t)(n < 0 ? 0 : (n > VITAL_MAX ? VITAL_MAX : n));
        break;
    }
    default: {
        int i = ATTR_INDEX(target);
        int n = g->attributes[i] + value;
        g->attributes[i] = (uint16_t)(n < 1 ? 1 : (n > 999 ? 999 : n));
        break;
    }
    }
    app_mark_changed();
}

/* guara.add_stat(nome, valor). */
static int l_add_stat(lua_State *L)
{
    add_to_target(luaL_checkoption(L, 1, NULL, NAMES_TARGET), lua_int_arg(L, 2));
    return 0;
}

/* guara.add_exp(pontos) -> níveis ganhos. */
static int l_add_exp(lua_State *L)
{
    lua_pushinteger(L, app_gain_xp((uint32_t)luaL_checkinteger(L, 1)));
    return 1;
}

/* Converte o nome do elemento (fire, earth, water, air, neutral) na afinidade. */
static int name_to_element(const char *name)
{
    /* Mesma ordem das afinidades em nature_t (fogo, terra, água, ar, neutra) */
    static const char *const names[] = {"fire", "earth", "water", "air", "neutral"};
    for (int i = 0; i < 5; i++) {
        if (strcmp(name, names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

/* guara.add_affinity(elemento, pontos); light/shadow movem Dia x Noite e mind soma INT. */
static int l_add_affinity(lua_State *L)
{
    const char *e = luaL_checkstring(L, 1);
    int points = lua_int_arg(L, 2);
    if (strcmp(e, "light") == 0 || strcmp(e, "shadow") == 0) {
        /* README: luz/sombra movem o medidor Dia x Noite */
        guara_add_gauge(&g_guara, GAUGE_DAY, (strcmp(e, "light") == 0 ? 1 : -1) * points * 100);
    } else if (strcmp(e, "mind") == 0) {
        add_to_target(ATTR_INT, points); /* "pontos mentais" -> INT (Psíquica) */
    } else {
        int elem = name_to_element(e);
        if (elem < 0) {
            return luaL_error(L, "elemento inválido: %s", e);
        }
        guara_add_affinity(&g_guara, (nature_t)elem, CENTI(points));
    }
    app_mark_changed();
    return 0;
}

/* guara.report_result(vitoria): move o medidor Vitórias x Derrotas. */
static int l_report_result(lua_State *L)
{
    bool won = lua_toboolean(L, 1);
    guara_add_gauge(&g_guara, GAUGE_VICTORIES, won ? GAUGE_STEP_VICTORY : -GAUGE_STEP_VICTORY);
    if (!won) {
        guara_add_gauge(&g_guara, GAUGE_ANGER, -GAUGE_STEP_SADNESS);
    }
    app_mark_changed();
    return 0;
}

/* ---------------- storage (isolado na pasta do jogo) ---------------- */

/* Chave de save válida: letras, números e '_' (até 24). */
static bool is_valid_key(const char *k)
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

/* storage.save(chave, texto): grava <pasta do jogo>/<chave>.sav. */
static int l_save(lua_State *L)
{
    const char *k = luaL_checkstring(L, 1);
    size_t n;
    const char *v = luaL_checklstring(L, 2, &n);
    if (!is_valid_key(k)) {
        return luaL_error(L, "chave inválida");
    }
    char c[220];
    snprintf(c, sizeof(c), "%s/%s.sav", vm.folder, k);
    FILE *f = fopen(c, "wb");
    bool ok = f && fwrite(v, 1, n, f) == n;
    if (f) {
        fclose(f);
    }
    lua_pushboolean(L, ok);
    return 1;
}

/* storage.load(chave) -> texto ou nil. */
static int l_load_save(lua_State *L)
{
    const char *k = luaL_checkstring(L, 1);
    if (!is_valid_key(k)) {
        return luaL_error(L, "chave inválida");
    }
    char c[220];
    snprintf(c, sizeof(c), "%s/%s.sav", vm.folder, k);
    size_t n;
    char *d = (char *)files_read(c, &n, 16 * 1024);
    if (!d) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, d, n);
    free(d);
    return 1;
}

/* exit(): encerra o jogo. */
static int l_exit(lua_State *L)
{
    (void)L;
    vm.exit_requested = true;
    return 0;
}

/* millis(): milissegundos desde o início do jogo. */
static int l_millis(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)(app_ms() - vm.start));
    return 1;
}

/* Registra uma tabela global com as funções do módulo. */
static void register_lua_module(lua_State *L, const char *name, const luaL_Reg *f)
{
    lua_newtable(L);
    luaL_setfuncs(L, f, 0);
    lua_setglobal(L, name);
}

/* Abre as bibliotecas padrão seguras e registra a API do Guará. */
static void open_libraries(lua_State *L)
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
        {"load", l_load}, {"sprite", l_sprite}, {NULL, NULL},
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
    register_lua_module(L, "oled", oled);
    register_lua_module(L, "btn", btn);
    register_lua_module(L, "audio", audio);
    register_lua_module(L, "guara", guara);
    lua_getglobal(L, "guara");
    lua_setglobal(L, "pet"); /* README: módulo `guara` (ou `pet`) */
    register_lua_module(L, "storage", storage);
    lua_pushcfunction(L, l_exit);
    lua_setglobal(L, "exit");
    lua_pushcfunction(L, l_millis);
    lua_setglobal(L, "millis");
    lua_pushinteger(L, GFX_WIDTH);
    lua_setglobal(L, "WIDTH");
    lua_pushinteger(L, GFX_HEIGHT);
    lua_setglobal(L, "HEIGHT");
    /* Gráficos da VROM, o Guará, o catálogo e os componentes de interface (game_api.c) */
    game_api_register(L);
}

/* Chama uma função global do jogo (se existir); erros param o jogo e são exibidos. */
static bool call(const char *function, int nargs)
{
    lua_State *L = vm.L;
    lua_getglobal(L, function);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1 + nargs);
        return true;
    }
    if (nargs) {
        lua_insert(L, -1 - nargs);
    }
    if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
        snprintf(vm.error, sizeof(vm.error), "%s", lua_tostring(L, -1));
        lua_pop(L, 1);
        vm.has_error = true;
        return false;
    }
    return true;
}

/* Aplica os efeitos por minuto do [GUARA_EFFECTS] ao Guará. */
static void apply_effects_minute(void)
{
    const game_effects_t *e = &vm.fx;
    vm.minutes++;
    add_to_target(VITAL_ENERGY, -e->energy_cost);
    add_to_target(VITAL_SATIETY, -e->satiety_cost);
    add_to_target(VITAL_MOOD, e->mood_gain);
    if (e->xp_gain) {
        app_gain_xp((uint32_t)e->xp_gain);
    }
    if (e->focus_attribute >= 0 && e->attribute_points) {
        add_to_target(e->focus_attribute, e->attribute_points);
    }
    if (e->affinity >= 0 && e->affinity_points) {
        guara_add_affinity(&g_guara, (nature_t)e->affinity, CENTI(e->affinity_points));
    }
    if (e->drop_item >= 0 && e->drop_interval > 0 && vm.minutes % e->drop_interval == 0 && e->drop_item < COUNT_ITEMS &&
        g_guara.inventory[e->drop_item] < 255) {
        g_guara.inventory[e->drop_item]++;
    }
    app_interact();
}

/* Lê a seção [GUARA_EFFECTS] do game.ini. */
static void read_game_ini(const char *ini)
{
    game_effects_t *e = &vm.fx;
    memset(e, 0, sizeof(*e));
    e->focus_attribute = e->affinity = e->drop_item = -1;
    if (!ini) {
        return;
    }
    const char *s = "GUARA_EFFECTS";
    e->energy_cost = ini_integer(ini, s, "cost_energy_per_min", 0);
    e->satiety_cost = ini_integer(ini, s, "cost_food_per_min", 0);
    e->mood_gain = ini_integer(ini, s, "gain_humor_per_min", 0);
    e->xp_gain = ini_integer(ini, s, "gain_exp_per_min", 0);
    e->attribute_points = ini_integer(ini, s, "gain_stat_pts_per_min", 0);
    e->affinity_points = ini_integer(ini, s, "affinity_pts_per_min", 0);
    e->drop_item = ini_integer(ini, s, "drop_item_id", -1);
    e->drop_interval = ini_integer(ini, s, "drop_item_interval_min", 0);
    char buf[16];
    if (ini_text(ini, s, "gain_stat_focus", buf, sizeof(buf))) {
        for (int i = ATTR_ATK; NAMES_TARGET[i]; i++) {
            if (strcasecmp(buf, NAMES_TARGET[i]) == 0) {
                e->focus_attribute = i;
            }
        }
    }
    if (ini_text(ini, s, "affinity_elem", buf, sizeof(buf))) {
        for (char *p = buf; *p; p++) {
            *p = (char)(*p >= 'A' && *p <= 'Z' ? *p + 32 : *p);
        }
        e->affinity = name_to_element(buf);
    }
}

/* Fecha a VM, libera imagens e devolve a rede. */
static void close_vm(void)
{
    if (vm.L) {
        lua_close(vm.L);
        vm.L = NULL;
    }
    for (int i = 0; i < 4; i++) {
        files_release_bmp(&vm.sheets[i]);
    }
    files_release_bmp(&vm.splash);
    app_block_sleep(false);
    network_suspend(false);
}

/* Rastreia o B segurado (2 s saem) e fecha a tela de erro. */
static void game_key(const key_event_t *ev)
{
    if (ev->key == KEY_B) {
        vm.b_pressed_at = ev->type == KEY_PRESSED ? app_ms() : (ev->type == KEY_RELEASED ? 0 : vm.b_pressed_at);
    }
    if (vm.has_error && ev->type == KEY_PRESSED && (ev->key == KEY_A || ev->key == KEY_B)) {
        vm.exit_requested = true;
    }
}

/* Splash, quadro do jogo (update + draw) e efeitos por minuto. */
static void game_draw(gfx_t *g, uint32_t now)
{
    vm.g = g;
    if (vm.exit_requested || (vm.b_pressed_at && now - vm.b_pressed_at > HOLD_B_EXIT_MS)) {
        vm.b_pressed_at = 0;
        ui_back();
        return;
    }
    if (now < vm.splash_until) {
        if (vm.splash.data) {
            bool small = vm.splash.width <= 16 && vm.splash.height <= 16;
            int scale_factor = small ? 2 : 1, w = vm.splash.width * scale_factor, h = vm.splash.height * scale_factor;
            gfx_bitmap(g, &vm.splash, (GFX_WIDTH - w) / 2, (GFX_HEIGHT - h) / 2, GFX_COPY, small ? GFX_SCALE2X : 0);
        } else {
            ui_icon_mode(g, "games", 48, 16, GFX_OR, GFX_SCALE2X);
        }
        vm.last_frame = now;
        return;
    }
    if (vm.has_error) {
        ui_title(g, "ERRO NO JOGO", now);
        ui_paragraph(g, 2, 14, 124, 5, vm.error, false);
        return;
    }
    /*
     * Com um modal ou o teclado do sistema por cima, o jogo fica em pausa: só
     * redesenha o último estado (o modal é desenhado em seguida, por cima).
     */
    if (ui_top() != &SCREEN_GAME) {
        vm.resync_keys = true;
        gfx_clear(g);
        call("draw", 0);
        vm.last_frame = now;
        return;
    }
    for (int i = 0; i < COUNT_KEYS; i++) {
        vm.previous[i] = vm.holding[i];
        vm.holding[i] = buttons_is_held((input_key_t)i);
        if (vm.resync_keys) {
            /* a tecla que fechou o modal ainda pode estar apertada: não conta como nova */
            vm.previous[i] = vm.holding[i];
        }
    }
    vm.resync_keys = false;
    float dt = (float)(now - vm.last_frame) / 1000.0f;
    vm.last_frame = now;
    lua_pushnumber(vm.L, (lua_Number)dt);
    if (call("update", 1)) {
        gfx_clear(g);
        call("draw", 0);
    }
    if (now - vm.last_minute >= 60000) {
        vm.last_minute += 60000;
        apply_effects_minute();
    }
}

static const screen_t SCREEN_GAME = {
    .name = "jogo",
    .fps = 30,
    .on_key = game_key,
    .on_draw = game_draw,
    .on_close = close_vm,
};

/* Abre um jogo (pasta ou .lua avulso): lê o game.ini, cria a VM e chama init(). */
void screens_run_game(const char *path)
{
    memset(&vm, 0, sizeof(vm));
    game_api_reset();
    struct stat st;
    char home[200];
    char *ini = NULL;
    size_t length;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        snprintf(vm.folder, sizeof(vm.folder), "%s", path);
        snprintf(home, sizeof(home), "%s/main.lua", path);
        char c[200], splash[48] = "load.bmp";
        snprintf(c, sizeof(c), "%s/game.ini", path);
        ini = (char *)files_read(c, &length, 4096);
        if (ini) {
            ini_text(ini, "GAME", "splash", splash, sizeof(splash));
        }
        snprintf(c, sizeof(c), "%s/%s", path, splash);
        if (!files_load_bmp(c, &vm.splash)) {
            char icon[48] = "icon.bmp";
            if (ini) {
                ini_text(ini, "GAME", "icon", icon, sizeof(icon));
            }
            snprintf(c, sizeof(c), "%s/%s", path, icon);
            files_load_bmp(c, &vm.splash); /* upscale 2x do ícone 16x16 */
        }
    } else {
        snprintf(home, sizeof(home), "%s", path);
        snprintf(vm.folder, sizeof(vm.folder), "%s", path);
        char *bar = strrchr(vm.folder, '/');
        if (bar) {
            *bar = '\0';
        }
    }
    read_game_ini(ini);
    free(ini);

    /* README: o Wi-Fi é descarregado antes do jogo para liberar ~60 KB */
    network_suspend(true);

    vm.L = lua_newstate(allocator, NULL);
    if (!vm.L) {
        ui_alert("JOGOS", "games", "Sem memória para a VM Lua.", "OK", NULL, NULL);
        return;
    }
    lua_gc(vm.L, LUA_GCINC, 100, 250, 0); /* GC incremental agressivo */
    open_libraries(vm.L);
    vm.start = vm.last_frame = vm.last_minute = app_ms();
    vm.splash_until = vm.start + 1500;
    app_block_sleep(true);
    ui_open(&SCREEN_GAME, NULL);

    gfx_t scratch;
    gfx_init(&scratch);
    vm.g = &scratch;
    if (luaL_loadfilex(vm.L, home, "t") != LUA_OK) {
        snprintf(vm.error, sizeof(vm.error), "%s", lua_tostring(vm.L, -1));
        lua_pop(vm.L, 1);
        vm.has_error = true;
        return;
    }
    if (lua_pcall(vm.L, 0, 0, 0) != LUA_OK) {
        snprintf(vm.error, sizeof(vm.error), "%s", lua_tostring(vm.L, -1));
        lua_pop(vm.L, 1);
        vm.has_error = true;
        return;
    }
    call("init", 0);
}

/* Framebuffer do quadro atual, onde as funções de desenho da API escrevem. */
gfx_t *game_runtime_framebuffer(void)
{
    return vm.g;
}

/* VM do jogo aberto (NULL se nenhum), usada pelas funções de retorno dos modais. */
lua_State *game_runtime_lua(void)
{
    return vm.L;
}

/* Para o jogo e mostra o erro (ex.: falha numa função de retorno de modal). */
void game_runtime_fail(const char *message)
{
    snprintf(vm.error, sizeof(vm.error), "%s", message ? message : "erro desconhecido");
    vm.has_error = true;
}
