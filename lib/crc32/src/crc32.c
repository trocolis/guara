/*
 * CRC-32 (IEEE 802.3), compatível com zlib.crc32.
 */
#include "crc32.h"

/* Calcula o CRC-32 bit a bit (sem tabela, para economizar Flash). */
uint32_t guara_crc32(uint32_t crc, const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;

    crc = ~crc;
    while (size--) {
        crc ^= *p++;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}
