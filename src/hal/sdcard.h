/*
 * Cartão MicroSD montado em /sdcard (FATFS sobre SPI).
 */
#ifndef GUARA_SDCARD_H
#define GUARA_SDCARD_H

#include <stdbool.h>
#include <stdint.h>

#define SD_ROOT "/sdcard"

bool sd_mount(void);
void sd_unmount(void);
bool sd_mounted(void);
/* Capacidade e espaço livre em bytes. */
bool sd_get_space(uint64_t *total, uint64_t *free_bytes);
/* Cria a pasta (e as intermediárias) se não existir. */
bool sd_ensure_folder(const char *path);

#endif
