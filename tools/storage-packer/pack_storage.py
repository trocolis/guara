#!/usr/bin/env python3
"""
tools/storage-packer/pack_storage.py
Empacotador da Partição de Dados e Storage (1024 KB, subtipo 0x99) do Guará.

Gera a imagem binária 'storage.bin' desacoplada do firmware contendo:
1. Cabeçalho de partição (Magic 'GSTR', versão, ponteiros de setores)
2. VROM (Virtual ROM): Catálogo indexado de bitmaps monocromáticos de 1-bit
3. Catálogo Binário: Especificações de itens, equipamentos e habilidades
4. Setores Rotativos de Save State (8 KB inicializados em 0xFF para flash limpa)
"""

import os
import sys
import struct
import zlib
import argparse
from pathlib import Path

# Constantes do layout de armazenamento
STORAGE_PARTITION_SIZE = 1024 * 1024  # 1 MB (1.048.576 bytes)
STORAGE_MAGIC = b'GSTR'               # Magic identifier: Guará Storage
STORAGE_VERSION = 1

SAVE_RING_SIZE = 8192                 # 8 KB (2 setores de 4 KB = 16 slots de 512B)
# Os setores rotativos ficam alocados nos últimos 8 KB da partição de 1MB
SAVE_RING_OFFSET = STORAGE_PARTITION_SIZE - SAVE_RING_SIZE

VROM_MAGIC = b'GVRO'                  # Magic identifier: Guará VROM


def pack_vrom(bitmaps_dir: Path) -> bytes:
    """
    Varre o diretório de bitmaps e empacota todos os arquivos .bmp de 1-bit
    em uma tabela binária indexada.
    """
    entries = []
    data_blobs = []
    current_data_offset = 0

    bmp_files = sorted(bitmaps_dir.glob("**/*.bmp"))
    print(f"[VROM] Processando {len(bmp_files)} bitmaps monocromáticos...")

    for bmp_path in bmp_files:
        rel_name = bmp_path.name
        content = bmp_path.read_bytes()
        
        # Validação básica de cabeçalho BMP Windows (BM)
        if len(content) < 54 or content[0:2] != b'BM':
            print(f"[AVISO] Ignorando arquivo inválido: {rel_name}")
            continue

        width, height, bpp = struct.unpack_from('<iiH', content, 18)
        if bpp != 1:
            print(f"[AVISO] Bitmap {rel_name} possui {bpp} bpp (esperado 1 bpp). Ignorando.")
            continue

        height = abs(height)
        data_size = len(content)

        # Entrada do índice:
        # - name (32 bytes char array)
        # - width (uint16)
        # - height (uint16)
        # - offset_rel (uint32)
        # - size (uint32)
        name_bytes = rel_name.encode('utf-8')[:31].ljust(32, b'\x00')
        entry_bytes = struct.pack('<32sHHII', name_bytes, width, height, current_data_offset, data_size)
        
        entries.append(entry_bytes)
        data_blobs.append(content)
        current_data_offset += data_size

    # Monta a estrutura binária da VROM:
    # Header: Magic (4B), Count (uint32), TableSize (uint32), DataSize (uint32)
    header_vrom = struct.pack('<4sIII', VROM_MAGIC, len(entries), len(entries) * 44, current_data_offset)
    index_table = b''.join(entries)
    raw_data = b''.join(data_blobs)

    vrom_binary = header_vrom + index_table + raw_data
    print(f"[VROM] Total empacotado: {len(vrom_binary)} bytes ({len(entries)} bitmaps indexados)")
    return vrom_binary


def build_storage_image(bitmaps_dir: Path, output_file: Path):
    """
    Constrói a imagem binária completa de 1024 KB para gravação na Flash.
    """
    print("==================================================")
    print("      📦 GUARÁ — EMPACOTADOR DE STORAGE BINÁRIO     ")
    print("==================================================")

    # 1. Empacota a VROM
    vrom_blob = pack_vrom(bitmaps_dir)
    vrom_offset = 64  # Logo após o cabeçalho principal de 64 bytes
    vrom_size = len(vrom_blob)

    # 2. Catálogo preliminar (placeholder estruturado para catálogo binário)
    catalog_offset = vrom_offset + vrom_size
    # Alinhamento em 4 bytes
    catalog_offset = (catalog_offset + 3) & ~3
    catalog_blob = b'GCAT\x01\x00\x00\x00'  # Header inicial de catálogo
    catalog_size = len(catalog_blob)

    # Verifica se os dados extrapolam o início do setor rotativo de Save State
    if catalog_offset + catalog_size > SAVE_RING_OFFSET:
        raise ValueError(f"ERRO: VROM + Catálogo ({catalog_offset + catalog_size} B) invadiram os setores de save ({SAVE_RING_OFFSET} B)!")

    # 3. Cabeçalho de Partição (64 bytes)
    # Magic (4B), Version (uint16), Flags (uint16),
    # VROM_Offset (uint32), VROM_Size (uint32),
    # Cat_Offset (uint32), Cat_Size (uint32),
    # SaveRing_Offset (uint32), SaveRing_Size (uint32),
    # Reserved (28 bytes), CRC32 (uint32)
    header_unhashed = struct.pack(
        '<4sHHIIIIII28s',
        STORAGE_MAGIC,
        STORAGE_VERSION,
        0,
        vrom_offset,
        vrom_size,
        catalog_offset,
        catalog_size,
        SAVE_RING_OFFSET,
        SAVE_RING_SIZE,
        b'\x00' * 28
    )
    header_crc = zlib.crc32(header_unhashed) & 0xFFFFFFFF
    header_full = header_unhashed + struct.pack('<I', header_crc)

    # 4. Monta a imagem preenchida com 0xFF (padrão de flash virgem)
    image = bytearray(b'\xFF' * STORAGE_PARTITION_SIZE)
    
    # Grava cabeçalho
    image[0:64] = header_full
    # Grava VROM
    image[vrom_offset:vrom_offset + vrom_size] = vrom_blob
    # Grava Catálogo
    image[catalog_offset:catalog_offset + catalog_size] = catalog_blob

    # Salva arquivo final
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_bytes(image)

    print("--------------------------------------------------")
    print(f"[OK] Imagem gerada: {output_file}")
    print(f"     Tamanho exato: {len(image)} bytes (1024 KB)")
    print(f"     Offset VROM:   0x{vrom_offset:06X} ({vrom_size} bytes)")
    print(f"     Offset Cat.:   0x{catalog_offset:06X} ({catalog_size} bytes)")
    print(f"     Offset Save:   0x{SAVE_RING_OFFSET:06X} ({SAVE_RING_SIZE} bytes)")
    print("==================================================")


def main():
    parser = argparse.ArgumentParser(description="Empacotador da partição storage (VROM + Catálogo + Save) para o Guará.")
    parser.add_argument("-b", "--bitmaps", default="bitmaps", help="Diretório de origem dos bitmaps")
    parser.add_argument("-o", "--output", default="storage.bin", help="Arquivo binário de saída")
    args = parser.parse_args()

    bitmaps_path = Path(args.bitmaps)
    output_path = Path(args.output)

    if not bitmaps_path.exists():
        print(f"Erro: Diretório de bitmaps '{bitmaps_path}' não encontrado.")
        sys.exit(1)

    build_storage_image(bitmaps_path, output_path)


if __name__ == "__main__":
    main()
