#!/usr/bin/env python3
"""
tools/storage-packer/pack_storage.py
Empacotador da partição 'storage' (1024 KB, subtipo 0x99) do Guará — formato v3.

Gera 'storage.bin', somente leitura no aparelho, contendo:
1. Cabeçalho (64 bytes): magic 'GSTR', versão, offsets/tamanhos da VROM, do
   catálogo e do pacote de certificados, CRC32 dos 60 bytes anteriores.
2. VROM 'GVRO': índice ordenado por nome (busca binária) + bitmaps 1-bit em
   linhas (row-major, MSB primeiro, bit 1 = pixel aceso), sem cabeçalho BMP.
3. Catálogo 'GCAT': registros de 176 bytes de itens, equipamentos e habilidades,
   lidos de catalog/*.csv.
4. Certificados (opcional): o pacote de raízes HTTPS no formato do ESP-IDF
   (esp_crt_bundle), gerado por tools/pio_storage.py com o gen_crt_bundle.py do
   próprio ESP-IDF. Sem ele (offset e tamanho 0), o firmware usa as raízes
   embutidas (certs/github_roots.pem).

Os saves ficam na partição 'save' e as configurações na NVS: gravar esta
imagem nunca apaga o Guará.

Qualquer inconsistência (nome duplicado, bitmap que não é 1 bpp, ícone
inexistente no catálogo, texto longo demais) interrompe a geração.
"""

import argparse
import csv
import struct
import sys
import zlib
from pathlib import Path

# Tamanho total da partição 'storage' (ver partitions.csv)
PARTITION_SIZE = 1024 * 1024
# A imagem é arredondada para setores de 4 KB (unidade de apagamento da Flash)
FLASH_SECTOR = 4096
# Versões dos formatos binários; o firmware recusa versões diferentes
STORAGE_FORMAT_VERSION = 3
CATALOG_FORMAT_VERSION = 1

# Pastas de bitmaps que não vão para a VROM (mockups de documentação)
IGNORED_FOLDERS = {"exemple"}

# Campo de nome de cada entrada da VROM (31 bytes + terminador)
VROM_NAME_SIZE = 32
# Tamanho fixo de cada registro do catálogo
CATALOG_RECORD_SIZE = 176

# ---------------------------------------------------------------------------
# Enumerações — manter em sincronia com lib/core/src/types.h
# A posição na lista é o valor numérico gravado no binário.
# ---------------------------------------------------------------------------
EFFECT_TARGETS = ["HP", "MP", "ENERGY", "MOOD", "SATIETY", "HEALTH",
                  "ATK", "DEF", "STR", "AGI", "INT", "LUK"]
NATURES = ["FIRE", "EARTH", "WATER", "AIR", "NEUTRAL", "ELECTRICITY", "PLANT",
           "ICE", "STEAM", "CRYSTAL", "SHADOW", "LIGHT", "PSYCHIC", "LAVA"]
# Estados usam o número do bit no campo de estados do Guará
STATES = {"SLEEPING": 0, "FAINTED": 1, "POISONED": 2, "PARALYZED": 3,
          "BURNED": 4, "FROZEN": 5, "CONFUSED": 6, "CURSED": 7,
          "TIRED": 8, "HUNGRY": 9,
          "HAPPY": 16, "SAD": 17, "ANGRY": 18, "BORED": 19,
          "ALL": 255}
ITEM_TYPES = ["FOOD", "DRINK", "POTION"]
EQUIPMENT_TYPES = ["HEAD", "BODY", "HAND_ATK", "HAND_DEF", "FEET", "ACCESSORY"]
SKILL_TYPES = ["ATTACK", "HEAL", "EFFECT"]
SKILL_TARGETS = ["USER", "OPPONENT"]


class PackError(Exception):
    """Erro de validação: interrompe a geração com uma mensagem clara."""


# ---------------------------------------------------------------------------
# VROM
# ---------------------------------------------------------------------------
def read_1bit_bmp(path: Path):
    """Lê um BMP de 1 bpp e devolve (largura, altura, dados row-major com 1 = aceso)."""
    raw = path.read_bytes()
    if len(raw) < 62 or raw[0:2] != b"BM":
        raise PackError(f"{path}: não é um arquivo BMP")
    pixel_offset = struct.unpack_from("<I", raw, 10)[0]
    width, height, _, bits_per_pixel, compression = struct.unpack_from("<iiHHI", raw, 18)
    if bits_per_pixel != 1:
        raise PackError(f"{path}: {bits_per_pixel} bpp (esperado 1 bpp; converta com tools/bmp-builder)")
    if compression != 0:
        raise PackError(f"{path}: BMP comprimido não suportado")
    header_size = struct.unpack_from("<I", raw, 14)[0]
    palette = raw[14 + header_size:14 + header_size + 8]
    # Índice da paleta -> aceso se a cor for clara (a paleta pode estar invertida)
    palette_lit = [sum(palette[i * 4:i * 4 + 3]) / 3 > 127 for i in range(2)]

    # No BMP cada linha é alinhada a 4 bytes; na VROM, só ao byte
    bmp_stride = ((width + 31) // 32) * 4
    stride = (width + 7) // 8
    bottom_up = height > 0  # BMP com altura positiva é gravado de baixo para cima
    height = abs(height)
    pixels = bytearray(stride * height)
    for y in range(height):
        source_y = height - 1 - y if bottom_up else y
        row = raw[pixel_offset + source_y * bmp_stride:pixel_offset + (source_y + 1) * bmp_stride]
        for x in range(width):
            palette_index = (row[x // 8] >> (7 - x % 8)) & 1
            if palette_lit[palette_index]:
                pixels[y * stride + x // 8] |= 0x80 >> (x % 8)
    return width, height, bytes(pixels)


def collect_bitmaps(folder: Path) -> dict:
    """Lê todos os .bmp (recursivamente). O nome na VROM é o nome do arquivo sem extensão."""
    bitmaps = {}
    origin = {}
    for path in sorted(folder.rglob("*.bmp")):
        if IGNORED_FOLDERS & set(path.relative_to(folder).parts[:-1]):
            continue
        name = path.stem
        if len(name.encode("utf-8")) >= VROM_NAME_SIZE:
            raise PackError(f"{path}: nome com mais de {VROM_NAME_SIZE - 1} bytes")
        if name in bitmaps:
            raise PackError(f"Nome duplicado na VROM: '{name}' ({origin[name]} e {path})")
        bitmaps[name] = read_1bit_bmp(path)
        origin[name] = path
    return bitmaps


def build_vrom(bitmaps: dict) -> bytes:
    """Monta a VROM: cabeçalho, índice ordenado por nome (para busca binária) e dados."""
    names = sorted(bitmaps, key=lambda n: n.encode("utf-8"))
    index = []
    pixel_data = bytearray()
    for name in names:
        width, height, bits = bitmaps[name]
        index.append(struct.pack("<32sHHII", name.encode("utf-8"), width, height, len(pixel_data), len(bits)))
        pixel_data += bits
    header = struct.pack("<4sIII", b"GVRO", len(names), 44, len(pixel_data))
    return header + b"".join(index) + bytes(pixel_data)


# ---------------------------------------------------------------------------
# Catálogo
# ---------------------------------------------------------------------------
def fixed_text(value: str, field_size: int, max_chars: int, context: str) -> bytes:
    """Valida um texto para um campo fixo (limite em caracteres e em bytes UTF-8)."""
    if len(value) > max_chars:
        raise PackError(f"{context}: '{value}' tem {len(value)} caracteres (máx. {max_chars})")
    encoded = value.encode("utf-8")
    if len(encoded) >= field_size:
        raise PackError(f"{context}: '{value}' ocupa {len(encoded)} bytes (máx. {field_size - 1})")
    return encoded


def parse_enum(value: str, options, context: str) -> int:
    """Converte o nome de uma enumeração no valor numérico (lista = posição, dict = valor)."""
    value = value.strip().upper()
    if value not in options:
        raise PackError(f"{context}: '{value}' inválido (opções: {', '.join(options)})")
    return options[value] if isinstance(options, dict) else options.index(value)


def parse_modifiers(text: str, names, context: str, maximum: int, allowed_range):
    """Converte 'HP+5;MP-3' em [(código, valor)]."""
    modifiers = []
    for part in filter(None, (p.strip() for p in text.split(";"))):
        sign_position = max(part.rfind("+"), part.rfind("-"))
        if sign_position <= 0:
            raise PackError(f"{context}: modificador '{part}' sem sinal (+/-)")
        code = parse_enum(part[:sign_position], names, context)
        value = int(part[sign_position:])
        if not allowed_range[0] <= value <= allowed_range[1]:
            raise PackError(f"{context}: valor {value} fora da faixa {allowed_range}")
        modifiers.append((code, value))
    if len(modifiers) > maximum:
        raise PackError(f"{context}: no máximo {maximum} modificadores")
    return modifiers


def build_record(fields: dict) -> bytes:
    """Serializa um registro de 176 bytes (layout lido por catalog_read no firmware)."""
    effects = fields.get("effects", [])
    status_effects = fields.get("status_effects", [])
    record = struct.pack("<8B", fields["id"], fields["type"], fields["nature"], fields.get("target", 0),
                         fields.get("power", 0), fields.get("mp_cost", 0), len(effects), len(status_effects))
    for code, value in effects + [(0, 0)] * (4 - len(effects)):
        record += struct.pack("<Bxh", code, value)
    for code, value in status_effects + [(0, 0)] * (2 - len(status_effects)):
        record += struct.pack("<Bb", code, value)
    record += b"\x00" * 4
    record += struct.pack("<16s32s32s64s", fields["slug"], fields["icon"], fields["name"], fields["description"])
    assert len(record) == CATALOG_RECORD_SIZE
    return record


def read_csv(path: Path, kind: str, bitmaps: dict) -> list:
    """Lê um CSV do catálogo ('item', 'equipment' ou 'skill') e devolve os registros binários."""
    records = []
    slugs = set()
    with path.open(encoding="utf-8", newline="") as csv_file:
        for line_number, row in enumerate(csv.DictReader(csv_file), 2):
            context = f"{path.name}:{line_number}"
            record_id = int(row["id"])
            if record_id != len(records):
                raise PackError(f"{context}: ids devem ser sequenciais a partir de 0 (esperado {len(records)})")
            if record_id > 63:
                raise PackError(f"{context}: no máximo 64 registros por catálogo")
            slug = row["slug"].strip()
            if slug in slugs:
                raise PackError(f"{context}: slug '{slug}' duplicado")
            slugs.add(slug)
            icon = row["icon"].strip()
            if icon not in bitmaps:
                raise PackError(f"{context}: ícone '{icon}' não existe na VROM")
            fields = {
                "id": record_id,
                "slug": fixed_text(slug, 16, 15, context),
                "icon": fixed_text(icon, 32, 31, context),
                "name": fixed_text(row["name"].strip(), 32, 20, context),
                "description": fixed_text(row["description"].strip(), 64, 48, context),
            }
            if kind == "skill":
                fields["type"] = parse_enum(row["type"], SKILL_TYPES, context)
                fields["target"] = parse_enum(row["target"], SKILL_TARGETS, context)
                fields["nature"] = parse_enum(row["nature"], NATURES, context)
                fields["power"] = int(row["power"])
                fields["mp_cost"] = int(row["mp_cost"])
                # Em habilidades, o sobre-efeito é a chance (%) de aplicar o estado
                fields["status_effects"] = parse_modifiers(row["status_effects"], STATES, context, 2, (0, 100))
            else:
                types = ITEM_TYPES if kind == "item" else EQUIPMENT_TYPES
                fields["type"] = parse_enum(row["type"], types, context)
                # Itens e equipamentos só têm afinidade com os 5 elementos básicos
                fields["nature"] = parse_enum(row["affinity"], NATURES[:5], context)
                fields["effects"] = parse_modifiers(row["effects"], EFFECT_TARGETS, context, 4, (-9999, 9999))
                fields["status_effects"] = parse_modifiers(row["status_effects"], STATES, context, 2, (-100, 100))
            records.append(build_record(fields))
    return records


def build_catalog(folder: Path, bitmaps: dict) -> bytes:
    """Monta o catálogo GCAT a partir de items.csv, equipment.csv e skills.csv."""
    items = read_csv(folder / "items.csv", "item", bitmaps)
    equipment = read_csv(folder / "equipment.csv", "equipment", bitmaps)
    skills = read_csv(folder / "skills.csv", "skill", bitmaps)
    header = struct.pack("<4sHHHHHH", b"GCAT", CATALOG_FORMAT_VERSION, CATALOG_RECORD_SIZE,
                         len(items), len(equipment), len(skills), 0)
    print(f"[CAT]  {len(items)} itens, {len(equipment)} equipamentos, {len(skills)} habilidades")
    return header + b"".join(items + equipment + skills)


# ---------------------------------------------------------------------------
# Imagem
# ---------------------------------------------------------------------------
def align(value: int, step: int) -> int:
    """Arredonda `value` para cima até um múltiplo de `step`."""
    return (value + step - 1) // step * step


def build_image(bitmaps_folder: Path, catalog_folder: Path, full_size: bool, cert_bundle: Path = None) -> bytes:
    """Monta a imagem completa: cabeçalho + VROM + catálogo + certificados, com o restante em 0xFF."""
    bitmaps = collect_bitmaps(bitmaps_folder)
    vrom = build_vrom(bitmaps)
    print(f"[VROM] {len(bitmaps)} bitmaps, {len(vrom)} bytes")
    catalog = build_catalog(catalog_folder, bitmaps)
    certs = cert_bundle.read_bytes() if cert_bundle else b""
    if cert_bundle:
        # O pacote começa com a lista de offsets (4 bytes por raiz); o primeiro offset é o tamanho dela
        print(f"[CERT] {struct.unpack_from('<I', certs, 0)[0] // 4} raízes, {len(certs)} bytes")

    vrom_offset = 64
    catalog_offset = align(vrom_offset + len(vrom), 4)
    certs_offset = align(catalog_offset + len(catalog), 4) if certs else 0
    data_end = (certs_offset + len(certs)) if certs else catalog_offset + len(catalog)
    if data_end > PARTITION_SIZE:
        raise PackError(f"Dados ({data_end} bytes) excedem a partição de {PARTITION_SIZE} bytes")

    header = struct.pack("<4sHHIIIIII28s", b"GSTR", STORAGE_FORMAT_VERSION, 0,
                         vrom_offset, len(vrom), catalog_offset, len(catalog),
                         certs_offset, len(certs), b"\x00" * 28)
    header += struct.pack("<I", zlib.crc32(header) & 0xFFFFFFFF)

    # 0xFF é o valor da Flash apagada: gravar menos bytes acelera o upload
    image_size = PARTITION_SIZE if full_size else align(data_end, FLASH_SECTOR)
    image = bytearray(b"\xFF" * image_size)
    image[0:64] = header
    image[vrom_offset:vrom_offset + len(vrom)] = vrom
    image[catalog_offset:catalog_offset + len(catalog)] = catalog
    if certs:
        image[certs_offset:certs_offset + len(certs)] = certs
    return bytes(image)


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description="Empacotador da partição storage (VROM + catálogo) do Guará.")
    parser.add_argument("-b", "--bitmaps", default=root / "bitmaps", type=Path, help="Pasta dos bitmaps")
    parser.add_argument("-c", "--catalog", default=root / "catalog", type=Path, help="Pasta dos CSVs do catálogo")
    parser.add_argument("-o", "--output", default=root / ".pio" / "storage.bin", type=Path, help="Arquivo de saída")
    parser.add_argument("--full", action="store_true", help="Gera a imagem com os 1024 KB completos")
    parser.add_argument("--cert-bundle", type=Path, help="Pacote de certificados do ESP-IDF (x509_crt_bundle)")
    args = parser.parse_args()

    try:
        image = build_image(args.bitmaps, args.catalog, args.full, args.cert_bundle)
    except PackError as error:
        print(f"ERRO: {error}", file=sys.stderr)
        sys.exit(1)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    print(f"[OK]   {args.output} ({len(image)} bytes)")


if __name__ == "__main__":
    main()
