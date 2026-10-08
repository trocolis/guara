#!/usr/bin/env python3
"""
tools/font-builder/build_font.py
Gera lib/gfx/src/font_5x8.c a partir de font_5x8.txt.

Cada glyph vira 5 bytes (uma coluna por byte, bit 0 = linha do topo), o
formato nativo das páginas do SH1106. A tabela é ordenada por codepoint para
busca binária no firmware.
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).with_name("font_5x8.txt")
TARGET = ROOT / "lib" / "gfx" / "src" / "font_5x8.c"


def is_pixel_row(line: str) -> bool:
    """Uma linha de glyph tem exatamente 5 colunas de '#' (aceso) ou '.' (apagado)."""
    return len(line) == 5 and set(line) <= {"#", "."}


def read_glyphs(path: Path) -> dict:
    """Lê o arquivo de texto e devolve {codepoint: [8 linhas de 5 colunas]}."""
    glyphs = {}
    current = None
    rows = []

    def close_glyph():
        # Fecha o glyph em andamento, completando até 8 linhas
        if current is None:
            return
        if not 7 <= len(rows) <= 8:
            raise ValueError(f"Glyph U+{current:04X}: {len(rows)} linhas (esperado 7 ou 8)")
        if current in glyphs:
            raise ValueError(f"Glyph U+{current:04X} duplicado")
        glyphs[current] = rows + ["....."] * (8 - len(rows))

    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if line.startswith("#") and not is_pixel_row(line):
            continue  # comentário
        if not line.strip():
            continue
        if line.startswith("== "):
            close_glyph()
            name = line[3:]
            if len(name) != 1 and not name.startswith("U+"):
                raise ValueError(f"Linha {line_number}: nome de glyph inválido '{name}'")
            current = int(name[2:], 16) if name.startswith("U+") else ord(name)
            rows = []
            continue
        if not is_pixel_row(line):
            raise ValueError(f"Linha {line_number}: esperado 5 colunas de '#'/'.', obtido '{line}'")
        rows.append(line)
    close_glyph()
    return glyphs


def to_columns(rows: list) -> list:
    """Converte 8 linhas em 5 bytes de coluna (bit y = pixel da linha y)."""
    return [sum(1 << y for y in range(8) if rows[y][x] == "#") for x in range(5)]


def main():
    glyphs = read_glyphs(SOURCE)
    codepoints = sorted(glyphs)
    output = [
        "/* Gerado por tools/font-builder/build_font.py a partir de font_5x8.txt. Não edite. */",
        '#include "font.h"',
        "",
        "/* Codepoints Unicode disponíveis, em ordem crescente (busca binária). */",
        "static const uint16_t codes[] = {",
    ]
    output += [f"    0x{c:04X}," for c in codepoints]
    output += ["};", "", "/* Colunas de cada glyph, na mesma ordem de `codes`. */",
               "static const uint8_t columns[][5] = {"]
    for c in codepoints:
        column_bytes = ", ".join(f"0x{b:02X}" for b in to_columns(glyphs[c]))
        label = chr(c) if c > 0x20 and chr(c) != "\\" else f"U+{c:04X}"
        output.append(f"    {{{column_bytes}}}, /* {label} */")
    output += [
        "};",
        "",
        "const font_t FONT_5X8 = {",
        "    .width = 5,",
        "    .height = 8,",
        "    .advance = 6,",
        "    .line_height = 9,",
        f"    .count = {len(codepoints)},",
        "    .codes = codes,",
        "    .columns = columns,",
        "};",
        "",
    ]
    TARGET.parent.mkdir(parents=True, exist_ok=True)
    TARGET.write_text("\n".join(output), encoding="utf-8")
    print(f"[OK] {len(codepoints)} glyphs -> {TARGET.relative_to(ROOT)}")


if __name__ == "__main__":
    try:
        main()
    except ValueError as error:
        print(f"Erro: {error}", file=sys.stderr)
        sys.exit(1)
