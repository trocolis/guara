#!/usr/bin/env python3
"""
tools/font-builder/build_font.py
Gera lib/gfx/src/fonte_5x8.c a partir de fonte_5x8.txt.

Cada glyph vira 5 bytes (uma coluna por byte, bit 0 = linha do topo), o
formato nativo das páginas do SH1106. A tabela é ordenada por codepoint para
busca binária no firmware.
"""

import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
ORIGEM = Path(__file__).with_name("fonte_5x8.txt")
DESTINO = RAIZ / "lib" / "gfx" / "src" / "fonte_5x8.c"


def ler_glyphs(caminho: Path) -> dict:
    glyphs = {}
    atual = None
    linhas = []

    def fechar():
        if atual is None:
            return
        if not 7 <= len(linhas) <= 8:
            raise ValueError(f"Glyph U+{atual:04X}: {len(linhas)} linhas (esperado 7 ou 8)")
        if atual in glyphs:
            raise ValueError(f"Glyph U+{atual:04X} duplicado")
        glyphs[atual] = linhas + ["....."] * (8 - len(linhas))

    for n, linha in enumerate(caminho.read_text(encoding="utf-8").splitlines(), 1):
        if linha.startswith("#") and not (len(linha) == 5 and set(linha) <= {"#", "."}):
            continue  # comentário
        if not linha.strip():
            continue
        if linha.startswith("== "):
            fechar()
            nome = linha[3:]
            atual = int(nome[2:], 16) if nome.startswith("U+") else ord(nome)
            if len(nome) != 1 and not nome.startswith("U+"):
                raise ValueError(f"Linha {n}: nome de glyph inválido '{nome}'")
            linhas = []
            continue
        if len(linha) != 5 or not set(linha) <= {"#", "."}:
            raise ValueError(f"Linha {n}: esperado 5 colunas de '#'/'.', obtido '{linha}'")
        linhas.append(linha)
    fechar()
    return glyphs


def colunas(linhas: list) -> list:
    return [sum(1 << y for y in range(8) if linhas[y][x] == "#") for x in range(5)]


def main():
    glyphs = ler_glyphs(ORIGEM)
    codigos = sorted(glyphs)
    saida = [
        "/* Gerado por tools/font-builder/build_font.py a partir de fonte_5x8.txt. Não edite. */",
        '#include "fonte.h"',
        "",
        "static const uint16_t codigos[] = {",
    ]
    saida += [f"    0x{c:04X}," for c in codigos]
    saida += ["};", "", "static const uint8_t colunas[][5] = {"]
    for c in codigos:
        bytes_ = ", ".join(f"0x{b:02X}" for b in colunas(glyphs[c]))
        rotulo = chr(c) if c > 0x20 and chr(c) not in "\\" else f"U+{c:04X}"
        saida.append(f"    {{{bytes_}}}, /* {rotulo} */")
    saida += [
        "};",
        "",
        "const fonte_t FONTE_5X8 = {",
        "    .largura = 5,",
        "    .altura = 8,",
        "    .avanco = 6,",
        "    .entrelinha = 9,",
        f"    .quantidade = {len(codigos)},",
        "    .codigos = codigos,",
        "    .colunas = colunas,",
        "};",
        "",
    ]
    DESTINO.parent.mkdir(parents=True, exist_ok=True)
    DESTINO.write_text("\n".join(saida), encoding="utf-8")
    print(f"[OK] {len(codigos)} glyphs -> {DESTINO.relative_to(RAIZ)}")


if __name__ == "__main__":
    try:
        main()
    except ValueError as erro:
        print(f"Erro: {erro}", file=sys.stderr)
        sys.exit(1)
