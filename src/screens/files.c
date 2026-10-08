/*
 * Utilitários de arquivos do cartão SD: listagem, ícones por tipo, leitura e
 * carregamento de BMP.
 */
#include "files.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "gfx_bmp.h"

/* Verdadeiro se o nome termina com a extensão (sem diferenciar maiúsculas). */
bool files_has_extension(const char *name, const char *ext)
{
    size_t n = strlen(name), e = strlen(ext);
    return n >= e && strcasecmp(name + n - e, ext) == 0;
}

/* Ordena pastas primeiro e depois por nome. */
static int compare(const void *a, const void *b)
{
    const file_entry_t *x = a, *y = b;
    if (x->folder != y->folder) {
        return x->folder ? -1 : 1;
    }
    return strcasecmp(x->name, y->name);
}

/* Lista uma pasta filtrando por extensões (resultado alocado; liberar com free). */
file_list_t *files_list(const char *folder, const char *const *extensions, bool include_folders, int maximum)
{
    file_list_t *l = calloc(1, sizeof(file_list_t) + sizeof(file_entry_t) * (size_t)maximum);
    if (!l) {
        return NULL;
    }
    DIR *d = opendir(folder);
    if (!d) {
        return l;
    }
    struct dirent *from;
    char path[160];
    while ((from = readdir(d)) != NULL && l->count < maximum) {
        if (from->d_name[0] == '.') {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", folder, from->d_name);
        struct stat st;
        if (stat(path, &st) != 0) {
            continue;
        }
        bool is_folder = S_ISDIR(st.st_mode);
        if (is_folder && !include_folders) {
            continue;
        }
        if (!is_folder && extensions) {
            bool ok = false;
            for (int i = 0; extensions[i]; i++) {
                ok |= files_has_extension(from->d_name, extensions[i]);
            }
            if (!ok) {
                continue;
            }
        }
        file_entry_t *e = &l->items[l->count++];
        snprintf(e->name, sizeof(e->name), "%s", from->d_name);
        e->folder = is_folder;
        e->size = is_folder ? 0 : (uint32_t)st.st_size;
    }
    closedir(d);
    qsort(l->items, (size_t)l->count, sizeof(file_entry_t), compare);
    return l;
}

/* Ícone da VROM para o tipo do arquivo. */
const char *files_icon_for(const file_entry_t *e)
{
    if (e->folder) {
        return "folder";
    }
    const char *n = e->name;
    if (files_has_extension(n, ".bmp")) return "images";
    if (files_has_extension(n, ".mid") || files_has_extension(n, ".midi")) return "tape";
    if (files_has_extension(n, ".lua")) return "moon_face";
    if (files_has_extension(n, ".sav") || files_has_extension(n, ".dat") || files_has_extension(n, ".idx"))
        return "save";
    if (files_has_extension(n, ".txt")) return "txt";
    if (files_has_extension(n, ".ini") || files_has_extension(n, ".cfg")) return "gear";
    return "file";
}

/* Tamanho com no máximo 4 caracteres (B, K, M, G). */
void files_format_size(uint64_t b, char *buf, int length)
{
    if (b < 1000) {
        snprintf(buf, (size_t)length, "%uB", (unsigned)b);
    } else if (b < 1000000) {
        snprintf(buf, (size_t)length, "%uK", (unsigned)(b / 1024 ? b / 1024 : 1));
    } else if (b < 1000000000ull) {
        snprintf(buf, (size_t)length, "%uM", (unsigned)(b / (1024 * 1024)));
    } else {
        snprintf(buf, (size_t)length, "%uG", (unsigned)(b / (1024ull * 1024 * 1024)));
    }
}

/* Nome sem extensão e com espaços no lugar de '_'. */
void files_friendly_name(const char *name, char *buf, int length)
{
    snprintf(buf, (size_t)length, "%s", name);
    char *point = strrchr(buf, '.');
    if (point && point != buf) {
        *point = '\0';
    }
    for (char *p = buf; *p; p++) {
        if (*p == '_') {
            *p = ' ';
        }
    }
}

/* Lê o arquivo inteiro para a memória (até `maximum` bytes). */
uint8_t *files_read(const char *path, size_t *size, size_t maximum)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || (size_t)n > maximum) {
        fclose(f);
        return NULL;
    }
    uint8_t *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf) {
        buf[n] = '\0';
        *size = (size_t)n;
    }
    return buf;
}

/* Carrega um BMP 1-bit do cartão para o formato da gfx. */
bool files_load_bmp(const char *path, gfx_bitmap_t *bmp)
{
    size_t length;
    uint8_t *file_data = files_read(path, &length, 64 * 1024);
    if (!file_data) {
        return false;
    }
    int w, h;
    bool ok = gfx_bmp_info(file_data, length, &w, &h) == 0;
    uint8_t *data = NULL;
    if (ok) {
        size_t n = (size_t)((w + 7) / 8) * (size_t)h;
        data = malloc(n);
        ok = data && gfx_bmp_decode(file_data, length, data, n) == 0;
    }
    free(file_data);
    if (!ok) {
        free(data);
        return false;
    }
    bmp->width = (uint16_t)w;
    bmp->height = (uint16_t)h;
    bmp->data = data;
    return true;
}

/* Libera a memória de um bitmap carregado. */
void files_release_bmp(gfx_bitmap_t *bmp)
{
    free((void *)bmp->data);
    bmp->data = NULL;
}
