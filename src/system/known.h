/*
 * Conhecidos do Conectar: Guarás com quem uma troca ou um desafio foi aceito,
 * gravados no cartão SD (um por linha) para não desgastar a Flash.
 */
#ifndef GUARA_KNOWN_H
#define GUARA_KNOWN_H

#include <stdbool.h>
#include <stdint.h>

#define KNOWN_MAX 8
#define KNOWN_FILE "/sdcard/guara/known.txt"

typedef struct {
    uint32_t uid;
    char name[11]; /* SIZE_NAME + 1 */
    uint8_t level;
} known_t;

/* Lê o arquivo do cartão (no boot); migra a lista antiga da NVS, se houver. */
void known_load(void);
/* Conhecidos, do mais antigo para o mais recente. Retorna quantos há. */
int known_list(known_t *out);
/* Salva ou atualiza (nome e nível) um conhecido; cheia, descarta o mais antigo. Falso sem cartão. */
bool known_save(uint32_t uid, const char *name, uint8_t level);
/* Remove o conhecido da posição `position` (na ordem de known_list). */
bool known_forget(int position);

#endif
