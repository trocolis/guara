/*
 * Músicas MIDI de /sdcard/music e o tocador, que continua em segundo plano.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "files.h"
#include "audio.h"
#include "widgets.h"
#include "midi.h"
#include "modal.h"
#include "sdcard.h"
#include "screens.h"

/* Músicas MIDI de /sdcard/music, tocadas no buzzer em segundo plano. */

#define FOLDER_MUSIC SD_ROOT "/music"
#define MIDI_MAX (96 * 1024)

/* ---------------- Fonte de música para a task de áudio ---------------- */

typedef struct {
    uint8_t *data;
    size_t size;
    midi_t midi;
    volatile uint32_t played_ms;
    uint32_t total_ms;
    char name[64];
    char path[160];
} music_track_t;

static music_track_t *track; /* faixa atual (pertence à task de áudio enquanto toca) */

/* Fonte de música: próxima nota do MIDI (chamada pela task de áudio). */
static bool midi_next_note(void *ctx, note_t *n)
{
    music_track_t *f = ctx;
    midi_note_t m;
    if (!midi_next(&f->midi, &m)) {
        return false;
    }
    n->freq = m.freq;
    n->ms = m.ms;
    f->played_ms += m.ms;
    return true;
}

/* Libera a faixa quando a task de áudio a descarta. */
static void free_music_track(void *ctx)
{
    music_track_t *f = ctx;
    if (track == f) {
        track = NULL;
    }
    free(f->data);
    free(f);
}

/* Carrega um MIDI e começa a tocar em segundo plano. */
static bool play(const char *path)
{
    music_track_t *f = calloc(1, sizeof(music_track_t));
    if (!f) {
        return false;
    }
    f->data = files_read(path, &f->size, MIDI_MAX);
    if (!f->data || !midi_open(&f->midi, f->data, f->size)) {
        free(f->data);
        free(f);
        return false;
    }
    f->total_ms = midi_duration_ms(f->data, f->size);
    snprintf(f->path, sizeof(f->path), "%s", path);
    const char *base = strrchr(path, '/');
    files_friendly_name(base ? base + 1 : path, f->name, sizeof(f->name));
    music_source_t source = {midi_next_note, free_music_track, f};
    audio_play_music(&source);
    track = f;
    /* README: Amaldiçoado se cura escutando música */
    if (g_has_guara && guara_has_state(&g_guara, ST_CURSED)) {
        guara_set_state(&g_guara, ST_CURSED, false);
        app_mark_changed();
    }
    return true;
}

/* ---------------- Lista ---------------- */

static file_list_t *items;
static ui_list_t list;

/* Relê a pasta de músicas. */
static void reload(void)
{
    static const char *const ext[] = {".mid", ".midi", NULL};
    free(items);
    items = files_list(FOLDER_MUSIC, ext, false, 64);
    list.total = items ? items->count : 0;
    ui_list_adjust(&list, UI_LINES_VISIBLE);
}

/* Caminho da música na posição da lista. */
static void path_for_index(int i, char *buf, int length)
{
    snprintf(buf, (size_t)length, "%s/%s", FOLDER_MUSIC, items->items[i].name);
}

/* Item da lista com o indicador de tocando/pausado. */
static void item(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    it->icon = "tape";
    files_friendly_name(items->items[i].name, it->text, sizeof(it->text));
    char c[160];
    path_for_index(i, c, sizeof(c));
    if (track && audio_music_active() && strcmp(track->path, c) == 0) {
        snprintf(it->info, sizeof(it->info), audio_music_paused() ? "||" : ">");
    }
}

/* ---------------- Tocador ---------------- */

/* Toca a música anterior ou seguinte da lista. */
static void switch_track(int delta)
{
    if (!items || items->count == 0) {
        return;
    }
    list.sel = (list.sel + delta + items->count) % items->count;
    char c[160];
    path_for_index(list.sel, c, sizeof(c));
    play(c);
}

/* A toca/pausa, ↑/↓ trocam de faixa, ←/→ ajustam o volume, B volta sem parar. */
static void player_key(const key_event_t *ev)
{
    if (ev->type != KEY_PRESSED) {
        return;
    }
    switch (ev->key) {
    case KEY_A:
        if (audio_music_active()) {
            audio_pause_music(!audio_music_paused());
        } else if (track == NULL && items && items->count) {
            switch_track(0);
        }
        break;
    case KEY_B:
        ui_back(); /* volta à lista sem interromper o áudio */
        break;
    case KEY_UP:
        switch_track(-1);
        break;
    case KEY_DOWN:
        switch_track(1);
        break;
    case KEY_RIGHT:
        audio_volume(audio_volume_level() + 1);
        break;
    case KEY_LEFT:
        audio_volume(audio_volume_level() - 1);
        break;
    default:
        break;
    }
}

/* Formata milissegundos como M:SS. */
static void format_time(char *buf, int length, uint32_t ms)
{
    snprintf(buf, (size_t)length, "%lu:%02lu", (unsigned long)(ms / 60000), (unsigned long)(ms / 1000 % 60));
}

/* Desenha o nome, play/pausa, progresso, tempos e volume. */
static void player_draw(gfx_t *g, uint32_t now)
{
    music_track_t *f = audio_music_active() ? track : NULL;
    ui_title(g, f ? f->name : "Parado", now);
    if (!f) {
        gfx_text_center(g, 0, GFX_WIDTH, 30, "A: tocar", GFX_WHITE);
        return;
    }
    /* Ícone de reprodução (▶) ou pausa (⏸) */
    if (audio_music_paused()) {
        gfx_rect_filled(g, 56, 18, 5, 14, GFX_WHITE);
        gfx_rect_filled(g, 66, 18, 5, 14, GFX_WHITE);
    } else {
        gfx_arrow(g, 58, 18, 15, 1, GFX_WHITE);
    }
    ui_progress(g, 8, 38, 112, 6, (int)(f->played_ms / 100), (int)(f->total_ms / 100 ? f->total_ms / 100 : 1));
    char a[12], b[12], line[28];
    format_time(a, sizeof(a), f->played_ms);
    format_time(b, sizeof(b), f->total_ms);
    snprintf(line, sizeof(line), "%s / %s", a, b);
    gfx_text_center(g, 0, GFX_WIDTH, 47, line, GFX_WHITE);
    for (int v = 0; v < audio_volume_level(); v++) {
        gfx_rect_filled(g, 4 + v * 4, 55 - v * 2, 3, 3 + v * 2, GFX_WHITE);
    }
}

static const screen_t SCREEN_PLAYER = {
    .name = "player",
    .on_key = player_key,
    .on_draw = player_draw,
};

/* Toca sem abrir o tocador (trilha de jogos). */
bool screens_play_background_music(const char *path)
{
    return play(path);
}

/* Toca e abre o tocador. */
void screens_play_music(const char *path)
{
    if (!play(path)) {
        audio_error();
        ui_alert("MUSICA", "tape", "Arquivo MIDI inválido.", "OK", NULL, NULL);
        return;
    }
    ui_open(&SCREEN_PLAYER, NULL);
}

/* Abre o tocador da música selecionada. */
static void on_key(const key_event_t *ev)
{
    if (ui_list_key(&list, ev, UI_LINES_VISIBLE) || ev->type != KEY_PRESSED) {
        return;
    }
    if (ev->key == KEY_B) {
        ui_back();
    } else if (ev->key == KEY_A && list.total) {
        char c[160];
        path_for_index(list.sel, c, sizeof(c));
        if (track && audio_music_active() && strcmp(track->path, c) == 0) {
            ui_open(&SCREEN_PLAYER, NULL);
        } else {
            screens_play_music(c);
        }
    }
}

/* Desenha a lista ou o aviso de pasta vazia. */
static void on_draw(gfx_t *g, uint32_t now)
{
    ui_title(g, "MUSICAS", now);
    if (!sd_mounted()) {
        gfx_text_center(g, 0, GFX_WIDTH, 32, "Insira o cartão SD", GFX_WHITE);
    } else if (list.total == 0) {
        gfx_text_center(g, 0, GFX_WIDTH, 28, "Nenhuma música em", GFX_WHITE);
        gfx_text_center(g, 0, GFX_WIDTH, 38, "/music", GFX_WHITE);
    } else {
        ui_list_draw(g, &list, UI_LIST_Y, UI_LINES_VISIBLE, item, NULL, now);
    }
}

/* Monta o cartão (se preciso) e lista as músicas. */
static void on_open(void *param)
{
    (void)param;
    if (!sd_mounted()) {
        sd_mount();
    }
    list = (ui_list_t){0};
    reload();
}

const screen_t SCREEN_MUSIC = {
    .name = "musicas",
    .on_open = on_open,
    .on_key = on_key,
    .on_draw = on_draw,
};
