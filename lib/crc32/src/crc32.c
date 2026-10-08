#include "crc32.h"

uint32_t guara_crc32(uint32_t crc, const void *dados, size_t tamanho)
{
    const uint8_t *p = (const uint8_t *)dados;

    crc = ~crc;
    while (tamanho--) {
        crc ^= *p++;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}
