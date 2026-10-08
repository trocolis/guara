/*
 * Utilitários de arquivos do cartão SD usados pelas telas.
 */
#ifndef GUARA_FILES_H
#define GUARA_FILES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

typedef struct {
    char name[64];
    bool folder;
    uint32_t size;
} file_entry_t;

typedef struct {
    int count;
    file_entry_t items[];
} file_list_t;

/*
 * Lista uma pasta (pastas primeiro, ordem alfabética). `extensoes` é uma lista
 * terminada em NULL (ex.: {".bmp", NULL}); NULL aceita tudo. Liberar com free().
 */
file_list_t *files_list(const char *folder, const char *const *extensions, bool include_folders, int maximum);
bool files_has_extension(const char *name, const char *ext);
/* Ícone da VROM para o tipo do arquivo (README > Cartão SD). */
const char *files_icon_for(const file_entry_t *e);
void files_format_size(uint64_t bytes, char *buf, int length);
/* Nome amigável: sem extensão e com '_' trocado por espaço. */
void files_friendly_name(const char *name, char *buf, int length);
/* Lê o arquivo inteiro para a memória (malloc). */
uint8_t *files_read(const char *path, size_t *size, size_t maximum);
/* Carrega um BMP 1-bit do SD (malloc). Liberar com files_release_bmp. */
bool files_load_bmp(const char *path, gfx_bitmap_t *bmp);
void files_release_bmp(gfx_bitmap_t *bmp);

#endif
