/*
 * Conhecidos do Conectar, gravados no cartão SD em KNOWN_FILE: uma linha por
 * conhecido, "uid;nível;nome", do mais antigo para o mais recente. A lista fica
 * em cache na RAM e o arquivo só é regravado quando ela muda.
 */
#include "known.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"
#include "sdcard.h"

static known_t list[KNOWN_MAX];
static int count;

/* Grava a lista no cartão. */
static bool write_to_disk(void)
{
    if (!sd_mounted() || !sd_ensure_folder(SD_ROOT "/guara")) {
        return false;
    }
    FILE *f = fopen(KNOWN_FILE, "w");
    if (!f) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        fprintf(f, "%08lx;%u;%s\n", (unsigned long)list[i].uid, list[i].level, list[i].name);
    }
    fclose(f);
    return true;
}

/* Lê o arquivo do cartão; sem ele, traz a lista que a v0.18.0 gravava na NVS (e a apaga de lá). */
void known_load(void)
{
    count = 0;
    struct stat info;
    if (sd_mounted() && stat(KNOWN_FILE, &info) != 0) {
        known_t legacy[KNOWN_MAX];
        int n = config_take_legacy_connections(legacy, KNOWN_MAX);
        if (n > 0) {
            memcpy(list, legacy, sizeof(known_t) * (size_t)n);
            count = n;
            write_to_disk();
        }
        return;
    }
    FILE *f = sd_mounted() ? fopen(KNOWN_FILE, "r") : NULL;
    if (!f) {
        return;
    }
    char line[48];
    while (count < KNOWN_MAX && fgets(line, sizeof(line), f)) {
        unsigned long uid;
        unsigned level;
        known_t k = {0};
        if (sscanf(line, "%lx;%u;%10[^\r\n]", &uid, &level, k.name) == 3 && uid != 0) {
            k.uid = (uint32_t)uid;
            k.level = (uint8_t)level;
            list[count++] = k;
        }
    }
    fclose(f);
}

int known_list(known_t *out)
{
    memcpy(out, list, sizeof(known_t) * (size_t)count);
    return count;
}

/* Salva ou atualiza: o conhecido vai para o fim (mais recente); cheia, sai o mais antigo. */
bool known_save(uint32_t uid, const char *name, uint8_t level)
{
    if (uid == 0 || !sd_mounted()) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (list[i].uid == uid) {
            memmove(&list[i], &list[i + 1], sizeof(known_t) * (size_t)(count - 1 - i));
            count--;
            break;
        }
    }
    if (count == KNOWN_MAX) {
        memmove(&list[0], &list[1], sizeof(known_t) * (KNOWN_MAX - 1));
        count--;
    }
    known_t *k = &list[count++];
    memset(k, 0, sizeof(*k));
    k->uid = uid;
    snprintf(k->name, sizeof(k->name), "%s", name);
    k->level = level;
    return write_to_disk();
}

/* Remove um conhecido e regrava o arquivo. */
bool known_forget(int position)
{
    if (position < 0 || position >= count) {
        return false;
    }
    memmove(&list[position], &list[position + 1], sizeof(known_t) * (size_t)(count - 1 - position));
    count--;
    return write_to_disk();
}
