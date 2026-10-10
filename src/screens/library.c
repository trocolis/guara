/*
 * Biblioteca (/sdcard/library): textos .txt e livros (book.ini + book.txt com
 * imagens {{arquivo.bmp}} da pasta ou {{nome}} da VROM), com leitor de 20
 * caracteres por linha.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app.h"
#include "files.h"
#include "audio.h"
#include "widgets.h"
#include "ini.h"
#include "rich_text.h"
#include "modal.h"
#include "sdcard.h"
#include "storage.h"
#include "screens.h"

/*
 * Biblioteca (/sdcard/library): .txt simples ou livros (pasta com book.ini,
 * book.txt e imagens 1-bit de até 16x16 referenciadas como {{imagem.bmp}},
 * da pasta do livro, ou {{nome}}, da VROM).
 */

#define FOLDER_LIBRARY SD_ROOT "/library"
#define TEXT_MAX (64 * 1024)
#define MAX_LINES 4000
#define LINE_PX 120 /* 20 caracteres normais de 6 px; em negrito cabem menos */

/* ---------------- Leitor ---------------- */

enum { LINE_TEXT, LINE_IMAGE, LINE_HEADER };

typedef struct {
    uint32_t start;
    uint16_t bytes;
    uint8_t kind;  /* LINE_TEXT, LINE_IMAGE ({{...}}) ou LINE_HEADER (#...#) */
    uint8_t style; /* estilos RICH_* ativos no começo da linha */
} text_line_t;

static struct {
    char *text;
    text_line_t *lines;
    int n_lines;
    int top;
    char folder[160];
    marquee_t marquees[4]; /* letreiro de cada cabeçalho visível (por posição na tela) */
    gfx_bitmap_t images[4];
    char image_names[4][32];
    uint32_t image_used[4]; /* último uso de cada imagem do cache (contador) */
    uint32_t use_clock;
} reader;

/* Acrescenta uma linha ao índice do leitor. */
static void add_line(uint32_t ini, int bytes, uint8_t kind, uint8_t style)
{
    if (reader.n_lines < MAX_LINES) {
        reader.lines[reader.n_lines++] = (text_line_t){ini, (uint16_t)bytes, kind, style};
    }
}

/*
 * Quebra o texto em linhas de até LINE_PX pixels (20 caracteres normais),
 * preferindo espaços. As marcações de estilo não ocupam espaço, o negrito
 * ocupa 1 px a mais por caractere, e os estilos valem até o fim do parágrafo.
 */
static void paginate(void)
{
    const char *t = reader.text;
    const char *p = t;
    while (*p) {
        const char *paragraph_end = strchr(p, '\n');
        if (!paragraph_end) {
            paragraph_end = p + strlen(p);
        }
        const char *opening = strstr(p, "{{");
        while (opening && opening < paragraph_end && opening > p && opening[-1] == '\\') {
            opening = strstr(opening + 2, "{{"); /* \{{ é texto: mostra as chaves */
        }
        if (opening && opening < paragraph_end) {
            const char *closing = strstr(opening, "}}");
            if (closing && closing < paragraph_end) {
                add_line((uint32_t)(opening + 2 - t), (int)(closing - opening - 2), LINE_IMAGE, 0);
                p = closing + 2;
                while (*p == ' ' || *p == '\r') {
                    p++;
                }
                if (*p == '\n') {
                    p++;
                }
                continue;
            }
        }
        if (p == paragraph_end || (p + 1 == paragraph_end && *p == '\r')) {
            add_line((uint32_t)(p - t), 0, LINE_TEXT, 0);
        }
        const char *text_end = paragraph_end;
        if (text_end > p && text_end[-1] == '\r') {
            text_end--;
        }
        /* #TEXTO#: o parágrafo inteiro vira um cabeçalho (barra branca, texto rolando se não couber) */
        if (text_end - p >= 2 && *p == '#' && text_end[-1] == '#') {
            add_line((uint32_t)(p + 1 - t), (int)(text_end - p - 2), LINE_HEADER, 0);
            p = *paragraph_end ? paragraph_end + 1 : paragraph_end;
            continue;
        }
        uint8_t style = 0; /* cada parágrafo começa sem estilo */
        while (p < text_end) {
            /* Avança caractere a caractere enquanto couber; guarda o último espaço e o estilo nele */
            const char *q = p, *line_break = NULL;
            uint8_t line_style = style, s = style, style_at_break = style;
            int px = 0, visible = 0;
            while (q < text_end) {
                const char *before = q;
                uint8_t s_before = s;
                uint32_t c = rich_next(&q, text_end, &s);
                if (!c) {
                    break;
                }
                if (px + rich_advance(s) > LINE_PX + 1) { /* +1: o espaço depois da última letra */
                    q = before;
                    s = s_before;
                    break;
                }
                px += rich_advance(s);
                visible++;
                if (c == ' ') {
                    line_break = q - 1; /* o próprio espaço: as marcações antes dele ficam nesta linha */
                    style_at_break = s;
                }
            }
            if (q < text_end && *q != ' ' && line_break) {
                q = line_break;
                s = style_at_break;
            }
            if (!visible) {
                break; /* só marcações sem texto: nada mais a mostrar */
            }
            add_line((uint32_t)(p - t), (int)(q - p), LINE_TEXT, line_style);
            style = s;
            p = q;
            while (p < paragraph_end && *p == ' ') {
                p++;
            }
        }
        p = *paragraph_end ? paragraph_end + 1 : paragraph_end;
    }
}

/* Bitmap da VROM de até 16x16 pelo nome (a VROM fica mapeada: sem cópia nem cache). */
static const gfx_bitmap_t *vrom_image(const char *name)
{
    static gfx_bitmap_t bmp; /* desenhado logo em seguida, antes da próxima busca */
    return vrom_lookup(name, &bmp) && bmp.width <= 16 && bmp.height <= 16 ? &bmp : NULL;
}

/*
 * Imagem do livro de até 16x16. {{nome}} (sem extensão) vem da VROM;
 * {{arquivo.bmp}} vem da pasta do livro e, se não existir lá, da VROM pelo
 * nome sem ".bmp". As da pasta ficam num cache de 4: cheio, a imagem
 * usada há mais tempo dá lugar à nova (cabem até 3 linhas de imagem na tela,
 * então as visíveis nunca saem).
 */
static const gfx_bitmap_t *book_image(const char *name, int n)
{
    char key[32];
    snprintf(key, sizeof(key), "%.*s", n < 31 ? n : 31, name);
    char *dot = strrchr(key, '.');
    if (!dot) {
        return vrom_image(key);
    }
    for (int i = 0; i < 4; i++) {
        if (reader.images[i].data && strcmp(reader.image_names[i], key) == 0) {
            reader.image_used[i] = ++reader.use_clock;
            return &reader.images[i];
        }
    }
    char c[200];
    gfx_bitmap_t loaded;
    snprintf(c, sizeof(c), "%s/%s", reader.folder, key);
    if (!files_load_bmp(c, &loaded)) {
        if (strcmp(dot, ".bmp") == 0) {
            *dot = '\0';
            return vrom_image(key); /* não está na pasta: tenta a VROM */
        }
        return NULL;
    }
    if (loaded.width > 16 || loaded.height > 16) {
        files_release_bmp(&loaded); /* README: máximo 16x16 */
        return NULL;
    }
    /* Espaço livre ou, com o cache cheio, o da imagem usada há mais tempo */
    int slot = -1;
    for (int i = 0; i < 4 && slot < 0; i++) {
        if (!reader.images[i].data) {
            slot = i;
        }
    }
    if (slot < 0) {
        slot = 0;
        for (int i = 1; i < 4; i++) {
            if (reader.image_used[i] < reader.image_used[slot]) {
                slot = i;
            }
        }
        files_release_bmp(&reader.images[slot]);
    }
    reader.images[slot] = loaded;
    snprintf(reader.image_names[slot], sizeof(reader.image_names[slot]), "%s", key);
    reader.image_used[slot] = ++reader.use_clock;
    return &reader.images[slot];
}

#define HEIGHT_LINE 9 /* 8 px da fonte + 1 px entre linhas */
#define HEIGHT_HEADER (UI_TITLE_HEIGHT + 1) /* barra do cabeçalho + 1 px */

/* Altura em pixels da linha (texto ou imagem). */
static int height(int i)
{
    /* Imagem: a altura dela + 1 px, o mesmo espaço que separa duas linhas de texto */
    if (reader.lines[i].kind == LINE_HEADER) {
        return HEIGHT_HEADER;
    }
    if (reader.lines[i].kind == LINE_IMAGE) {
        const gfx_bitmap_t *b = book_image(reader.text + reader.lines[i].start, reader.lines[i].bytes);
        return b ? b->height + 1 : HEIGHT_LINE;
    }
    return HEIGHT_LINE;
}

/* Rolagem por linha (↑/↓) ou por página (←/→); B fecha. */
static void reader_key(const key_event_t *ev)
{
    if (ev->type == KEY_RELEASED) {
        return;
    }
    switch (ev->key) {
    case KEY_UP: reader.top--; break;
    case KEY_DOWN: reader.top++; break;
    case KEY_LEFT: reader.top -= 6; break;
    case KEY_RIGHT: reader.top += 6; break;
    case KEY_B:
        if (ev->type == KEY_PRESSED) {
            ui_back();
        }
        return;
    default: break;
    }
    if (reader.top > reader.n_lines - 1) {
        reader.top = reader.n_lines - 1;
    }
    if (reader.top < 0) {
        reader.top = 0;
    }
}

/*
 * Cabeçalho #TEXTO#: barra branca da largura da tela com o texto em preto,
 * centralizado, ou rolando como nas barras de título se não couber.
 */
static void draw_header(gfx_t *g, int y, const text_line_t *l, marquee_t *m, uint32_t now)
{
    char buf[96];
    int n = l->bytes < (int)sizeof(buf) - 1 ? l->bytes : (int)sizeof(buf) - 1;
    memcpy(buf, reader.text + l->start, (size_t)n);
    buf[n] = '\0';
    gfx_rect_filled(g, 0, y, GFX_WIDTH, UI_TITLE_HEIGHT, GFX_WHITE);
    int w = gfx_text_width(buf);
    if (w <= GFX_WIDTH - 4) {
        gfx_text(g, (GFX_WIDTH - w) / 2, y + UI_TITLE_TEXT_Y, buf, GFX_BLACK);
    } else {
        ui_marquee_text(g, 2, y + UI_TITLE_TEXT_Y, GFX_WIDTH - 4, buf, GFX_BLACK, true, m, now);
    }
}

/* Desenha as linhas visíveis (o título fica a cargo do texto, com #TEXTO#). */
static void reader_draw(gfx_t *g, uint32_t now)
{
    int y = 0, headers = 0;
    for (int i = reader.top; i < reader.n_lines && y < GFX_HEIGHT; i++) {
        text_line_t *l = &reader.lines[i];
        if (l->kind == LINE_HEADER) {
            draw_header(g, y, l, &reader.marquees[headers++ % 4], now);
        } else if (l->kind == LINE_IMAGE) {
            const gfx_bitmap_t *b = book_image(reader.text + l->start, l->bytes);
            if (b) {
                gfx_bitmap(g, b, (GFX_WIDTH - b->width) / 2, y, GFX_COPY, 0);
            }
        } else if (l->bytes) {
            const char *start = reader.text + l->start;
            rich_draw(g, 2, y, start, start + l->bytes, l->style);
        }
        y += height(i);
    }
    ui_scrollbar(g, 0, GFX_HEIGHT, reader.n_lines, 7, reader.top > reader.n_lines - 7 ? reader.n_lines - 7 : reader.top);
}

/* Libera texto, índice e imagens. */
static void reader_close(void)
{
    free(reader.text);
    free(reader.lines);
    for (int i = 0; i < 4; i++) {
        files_release_bmp(&reader.images[i]);
    }
    memset(&reader, 0, sizeof(reader));
}

static const screen_t SCREEN_READER = {
    .name = "leitor",
    .keep_open = true, /* ler uma página pode levar mais de 1 minuto */
    .on_key = reader_key,
    .on_draw = reader_draw,
    .on_close = reader_close,
};

/* Carrega o texto, pagina e abre o leitor. */
static void open_reader(const char *file, const char *folder)
{
    size_t length;
    reader_close();
    reader.text = (char *)files_read(file, &length, TEXT_MAX);
    reader.lines = malloc(sizeof(text_line_t) * MAX_LINES);
    if (!reader.text || !reader.lines) {
        reader_close();
        audio_error();
        ui_alert("BIBLIOTECA", "book", "Não foi possível abrir (máx. 64 KB).", "OK", NULL, NULL);
        return;
    }
    snprintf(reader.folder, sizeof(reader.folder), "%s", folder);
    paginate();
    ui_open(&SCREEN_READER, NULL);
}

/* Abre um .txt no leitor (usado também pelo Cartão SD). */
void screens_read_text(const char *path)
{
    char folder[160];
    snprintf(folder, sizeof(folder), "%s", path);
    char *bar = strrchr(folder, '/');
    if (bar) {
        *bar = '\0';
    }
    open_reader(path, folder);
}

/* ---------------- Lista ---------------- */

typedef struct {
    char name[64];
    char title[48];
    char icon[32];
    bool book;
} library_entry_t;

static library_entry_t *works;
static ui_list_t list;

/* Relê a biblioteca: .txt e pastas com book.ini. */
static void reload(void)
{
    static const char *const ext[] = {".txt", NULL};
    free(works);
    works = NULL;
    list.total = 0;
    file_list_t *l = files_list(FOLDER_LIBRARY, ext, true, 64);
    if (!l) {
        return;
    }
    works = calloc((size_t)(l->count ? l->count : 1), sizeof(library_entry_t));
    for (int i = 0; works && i < l->count; i++) {
        library_entry_t *o = &works[list.total];
        const file_entry_t *e = &l->items[i];
        snprintf(o->name, sizeof(o->name), "%s", e->name);
        if (e->folder) {
            char ini_c[200];
            size_t length;
            snprintf(ini_c, sizeof(ini_c), "%s/%s/book.ini", FOLDER_LIBRARY, e->name);
            char *ini = (char *)files_read(ini_c, &length, 4096);
            if (!ini) {
                continue; /* pasta sem book.ini não é livro */
            }
            o->book = true;
            if (!ini_text(ini, "BOOK", "title", o->title, sizeof(o->title)) &&
                !ini_text(ini, NULL, "titulo", o->title, sizeof(o->title))) {
                files_friendly_name(e->name, o->title, sizeof(o->title));
            }
            snprintf(o->icon, sizeof(o->icon), "book");
            free(ini);
        } else {
            files_friendly_name(e->name, o->title, sizeof(o->title));
            snprintf(o->icon, sizeof(o->icon), "txt");
        }
        list.total++;
    }
    free(l);
}

/* Item da lista da biblioteca. */
static void item(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    it->icon = works[i].icon;
    snprintf(it->text, sizeof(it->text), "%s", works[i].title);
}

/* Abre a obra selecionada. */
static void on_key(const key_event_t *ev)
{
    if (ui_list_key(&list, ev, UI_LINES_VISIBLE) || ev->type != KEY_PRESSED) {
        return;
    }
    if (ev->key == KEY_B) {
        ui_back();
    } else if (ev->key == KEY_A && list.total) {
        library_entry_t *o = &works[list.sel];
        char file[200], folder[160];
        if (o->book) {
            snprintf(folder, sizeof(folder), "%s/%s", FOLDER_LIBRARY, o->name);
            snprintf(file, sizeof(file), "%s/book.txt", folder);
        } else {
            snprintf(folder, sizeof(folder), "%s", FOLDER_LIBRARY);
            snprintf(file, sizeof(file), "%s/%s", FOLDER_LIBRARY, o->name);
        }
        open_reader(file, folder);
    }
}

/* Desenha a lista ou o aviso de pasta vazia. */
static void on_draw(gfx_t *g, uint32_t now)
{
    ui_title(g, "BIBLIOTECA", now);
    if (!sd_mounted()) {
        gfx_text_center(g, 0, GFX_WIDTH, 32, "Insira o cartão SD", GFX_WHITE);
    } else if (list.total == 0) {
        gfx_text_center(g, 0, GFX_WIDTH, 28, "Nada em /library", GFX_WHITE);
    } else {
        ui_list_draw(g, &list, UI_LIST_Y, UI_LINES_VISIBLE, item, NULL, now);
    }
}

/* Monta o cartão (se preciso) e lista as obras. */
static void on_open(void *param)
{
    (void)param;
    if (!sd_mounted()) {
        sd_mount();
    }
    list = (ui_list_t){0};
    reload();
}

/* Libera a lista. */
static void on_close(void)
{
    free(works);
    works = NULL;
}

const screen_t SCREEN_LIBRARY = {
    .name = "biblioteca",
    .on_open = on_open,
    .on_close = on_close,
    .on_key = on_key,
    .on_draw = on_draw,
};
