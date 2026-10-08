/*
 * Gerenciador do cartão SD: navegação, abrir, copiar, mover, renomear, excluir
 * e detalhes (README > Cartão SD).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "app.h"
#include "files.h"
#include "audio.h"
#include "widgets.h"
#include "modal.h"
#include "sdcard.h"
#include "screens.h"

/*
 * Gerenciador do cartão SD (README > Cartão SD). A abre a pasta ou o modal de
 * comandos do arquivo; ←/→ abre os comandos também para pastas. B sobe um nível.
 */

#define MAX_ENTRIES 96

static char folder[160] = SD_ROOT;
static file_list_t *items;
static ui_list_t list;
static struct {
    char source[192];
    bool move;
    bool enabled;
} area;

/* Verdadeiro se a pasta atual é a raiz do cartão. */
static bool is_root(void)
{
    return strcmp(folder, SD_ROOT) == 0;
}

/* Linhas extras no topo: "Colar aqui" (se houver algo copiado) e ".." */
static int extras(void)
{
    return (area.enabled ? 1 : 0) + (is_root() ? 0 : 1);
}

/* Relê a pasta atual. */
static void reload(void)
{
    free(items);
    items = files_list(folder, NULL, true, MAX_ENTRIES);
    list.total = (items ? items->count : 0) + extras();
    ui_list_adjust(&list, UI_LINES_VISIBLE);
}

/* Entrada selecionada (NULL nas linhas fixas). */
static const file_entry_t *selected_entry(void)
{
    int i = list.sel - extras();
    return items && i >= 0 && i < items->count ? &items->items[i] : NULL;
}

/* Caminho completo de uma entrada da pasta atual. */
static void build_path(const file_entry_t *e, char *buf, int length)
{
    snprintf(buf, (size_t)length, "%s/%s", folder, e->name);
}

/* Item da lista com ícone por tipo e tamanho. */
static void item(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    if (area.enabled && i == 0) {
        it->icon = "add";
        snprintf(it->text, sizeof(it->text), "%s aqui", area.move ? "Mover" : "Colar");
        return;
    }
    if (!is_root() && i == (area.enabled ? 1 : 0)) {
        it->icon = "up";
        snprintf(it->text, sizeof(it->text), "..");
        return;
    }
    const file_entry_t *e = &items->items[i - extras()];
    it->icon = files_icon_for(e);
    snprintf(it->text, sizeof(it->text), "%s", e->name);
    if (e->folder) {
        snprintf(it->info, sizeof(it->info), "DIR");
    } else {
        files_format_size(e->size, it->info, sizeof(it->info));
    }
}

/* Sobe um nível. */
static void raise(void)
{
    char *bar = strrchr(folder, '/');
    if (bar && !is_root()) {
        *bar = '\0';
        list = (ui_list_t){0};
        reload();
    }
}

/* Copia um arquivo em blocos de 1 KB. */
static bool copy_file(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb"), *b = a ? fopen(to, "wb") : NULL;
    bool ok = a && b;
    char buf[1024];
    size_t n;
    while (ok && (n = fread(buf, 1, sizeof(buf), a)) > 0) {
        ok = fwrite(buf, 1, n, b) == n;
    }
    if (a) fclose(a);
    if (b) fclose(b);
    return ok;
}

/* Apaga um arquivo ou uma pasta com todo o conteúdo. */
static bool erase_recursive(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        return remove(path) == 0;
    }
    DIR *d = opendir(path);
    struct dirent *from;
    char child[192];
    while (d && (from = readdir(d)) != NULL) {
        if (strcmp(from->d_name, ".") && strcmp(from->d_name, "..")) {
            snprintf(child, sizeof(child), "%s/%s", path, from->d_name);
            erase_recursive(child);
        }
    }
    if (d) {
        closedir(d);
    }
    return rmdir(path) == 0;
}

/* Conclui o copiar/mover para a pasta atual. */
static void paste(void)
{
    const char *name = strrchr(area.source, '/');
    char destination[192];
    snprintf(destination, sizeof(destination), "%s%s", folder, name ? name : "/arquivo");
    bool ok = area.move ? rename(area.source, destination) == 0 : copy_file(area.source, destination);
    area.enabled = false;
    reload();
    if (!ok) {
        audio_error();
        ui_alert("CARTAO SD", "sd_card", "Não foi possível concluir.", "OK", NULL, NULL);
    }
}

/* Aplica o novo nome digitado. */
static void renamed(const char *incoming, void *ctx)
{
    (void)ctx;
    const file_entry_t *e = selected_entry();
    if (!incoming || !*incoming || !e) {
        return;
    }
    char from[192], to[192];
    build_path(e, from, sizeof(from));
    snprintf(to, sizeof(to), "%s/%s", folder, incoming);
    if (rename(from, to) != 0) {
        audio_error();
        ui_alert("RENOMEAR", "file", "Falhou.", "OK", NULL, NULL);
    }
    reload();
}

/* Segunda confirmação: exclui. */
static void delete_2(int r, void *ctx)
{
    (void)ctx;
    const file_entry_t *e = selected_entry();
    if (r == 1 && e) {
        char c[192];
        build_path(e, c, sizeof(c));
        erase_recursive(c);
        reload();
    }
}

/* Primeira confirmação: pede a segunda. */
static void delete_1(int r, void *ctx)
{
    (void)ctx;
    if (r == 1) {
        ui_confirm("EXCLUIR", "kill", "Não dá para desfazer. Confirma?", "NAO", "EXCLUIR", delete_2, NULL);
    }
}

/* Abre o arquivo conforme o tipo (imagem, texto, Lua, MIDI). */
static void open_file(const file_entry_t *e)
{
    char c[192];
    build_path(e, c, sizeof(c));
    if (files_has_extension(e->name, ".bmp")) {
        screens_view_image(c);
    } else if (files_has_extension(e->name, ".txt")) {
        screens_read_text(c);
    } else if (files_has_extension(e->name, ".lua")) {
        screens_run_game(c);
    } else if (files_has_extension(e->name, ".mid") || files_has_extension(e->name, ".midi")) {
        screens_play_music(c);
    } else {
        ui_alert("ABRIR", files_icon_for(e), "Tipo de arquivo não suportado.", "OK", NULL, NULL);
    }
}

/* Executa o comando escolhido no modal. */
static void command(int r, void *ctx)
{
    (void)ctx;
    const file_entry_t *e = selected_entry();
    if (!e) {
        return;
    }
    char c[192];
    build_path(e, c, sizeof(c));
    switch (r) {
    case 0:
        if (e->folder) {
            snprintf(folder + strlen(folder), sizeof(folder) - strlen(folder), "/%s", e->name);
            list = (ui_list_t){0};
            reload();
        } else {
            open_file(e);
        }
        break;
    case 1:
    case 2:
        snprintf(area.source, sizeof(area.source), "%s", c);
        area.move = r == 2;
        area.enabled = true;
        reload();
        break;
    case 3:
        ui_keyboard("Novo nome", e->name, 60, renamed, NULL);
        break;
    case 4:
        ui_confirm("EXCLUIR", files_icon_for(e), e->name, "NAO", "SIM", delete_1, NULL);
        break;
    case 5: {
        char text[200], length[12];
        files_format_size(e->size, length, sizeof(length));
        snprintf(text, sizeof(text), "%s\nTipo: %s\nTamanho: %lu bytes", e->name, e->folder ? "pasta" : files_icon_for(e),
                 (unsigned long)e->size);
        ui_details("DETALHES", text);
        break;
    }
    default:
        break;
    }
}

/* Abre o modal de comandos da entrada. */
static void commands(const file_entry_t *e)
{
    static const char *const options[] = {"Abrir", "Copiar", "Mover", "Renomear", "Excluir", "Detalhes"};
    ui_choice(e->name, options, 6, command, NULL);
}

/* Navegação, comandos e montagem do cartão. */
static void on_key(const key_event_t *ev)
{
    if (ui_list_key(&list, ev, UI_LINES_VISIBLE) || ev->type != KEY_PRESSED) {
        return;
    }
    if (!sd_mounted()) {
        if (ev->on_key == KEY_A) {
            sd_mount();
            reload();
        } else if (ev->on_key == KEY_B) {
            ui_back();
        }
        return;
    }
    const file_entry_t *e = selected_entry();
    switch (ev->on_key) {
    case KEY_B:
        if (is_root()) {
            ui_back();
        } else {
            raise();
        }
        break;
    case KEY_A:
        if (area.enabled && list.sel == 0) {
            paste();
        } else if (!e) {
            raise();
        } else if (e->folder) {
            command(0, NULL);
        } else {
            commands(e);
        }
        break;
    case KEY_LEFT:
    case KEY_RIGHT:
        if (e) {
            commands(e);
        }
        break;
    default:
        break;
    }
}

/* Desenha a pasta atual ou o aviso de cartão ausente. */
static void on_draw(gfx_t *g, uint32_t now)
{
    ui_title(g, folder, now);
    if (!sd_mounted()) {
        gfx_text_center(g, 0, GFX_WIDTH, 26, "Cartão não montado", GFX_WHITE);
        ui_footer(g, "VOLTAR", "MONTAR", now);
        return;
    }
    if (list.total == 0) {
        gfx_text_center(g, 0, GFX_WIDTH, 32, "Pasta vazia", GFX_WHITE);
        return;
    }
    ui_list_draw(g, &list, UI_LIST_Y, UI_LINES_VISIBLE, item, NULL, now);
}

/* Monta o cartão e abre na raiz. */
static void on_open(void *param)
{
    (void)param;
    if (!sd_mounted()) {
        sd_mount();
    }
    snprintf(folder, sizeof(folder), "%s", SD_ROOT);
    list = (ui_list_t){0};
    reload();
}

/* Atualiza a lista ao voltar. */
static void on_resume(void)
{
    reload();
}

/* Libera a lista e a área de transferência. */
static void on_close(void)
{
    free(items);
    items = NULL;
    area.enabled = false;
}

const screen_t SCREEN_CARD_SD = {
    .name = "cartao_sd",
    .on_open = on_open,
    .on_resume = on_resume,
    .on_close = on_close,
    .on_key = on_key,
    .on_draw = on_draw,
};
