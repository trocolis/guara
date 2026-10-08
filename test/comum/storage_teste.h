#ifndef GUARA_STORAGE_TESTE_H
#define GUARA_STORAGE_TESTE_H

#include <stdio.h>
#include <stdlib.h>

#include "storage.h"

/*
 * Carrega a imagem de teste (.pio/storage_teste.bin), gerada com o catálogo de
 * test/fixtures/catalog pelo pré-script tools/pio_storage_teste.py.
 */
static inline int carregar_storage_teste(void)
{
    static uint8_t *imagem;
    if (imagem) {
        return 1;
    }
    FILE *f = fopen(".pio/storage_teste.bin", "rb");
    if (!f) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long tam = ftell(f);
    fseek(f, 0, SEEK_SET);
    imagem = malloc((size_t)tam);
    size_t lidos = fread(imagem, 1, (size_t)tam, f);
    fclose(f);
    return lidos == (size_t)tam && storage_abrir(imagem, (size_t)tam) == STORAGE_OK;
}

#endif
