#ifndef GUARA_STORAGE_TEST_H
#define GUARA_STORAGE_TEST_H

#include <stdio.h>
#include <stdlib.h>

#include "storage.h"

/*
 * Carrega a imagem de teste (.pio/storage_test.bin), gerada com o catálogo de
 * test/fixtures/catalog pelo pré-script tools/pio_storage_test.py.
 */
static inline int load_test_storage(void)
{
    static uint8_t *image;
    if (image) {
        return 1;
    }
    FILE *f = fopen(".pio/storage_test.bin", "rb");
    if (!f) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    image = malloc((size_t)length);
    size_t read_count = fread(image, 1, (size_t)length, f);
    fclose(f);
    return read_count == (size_t)length && storage_open(image, (size_t)length) == STORAGE_OK;
}

#endif
