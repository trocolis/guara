#ifndef GUARA_CRC32_H
#define GUARA_CRC32_H

#include <stddef.h>
#include <stdint.h>

/*
 * CRC-32 (IEEE 802.3, polinômio refletido 0xEDB88320), o mesmo de
 * zlib.crc32() usado pelo storage-packer. Usado no cabeçalho do storage
 * e no ChecksumDomain do Save State.
 *
 * Para calcular em partes: crc = guara_crc32(0, a, n); crc = guara_crc32(crc, b, m);
 */
uint32_t guara_crc32(uint32_t crc, const void *data, size_t size);

#endif
