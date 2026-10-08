#!/usr/bin/env python3
"""
tools/storage-packer/pack_storage.py
Empacotador da partição 'storage' (1024 KB, subtipo 0x99) do Guará — formato v2.

Gera 'storage.bin', somente leitura no aparelho, contendo:
1. Cabeçalho (64 bytes): magic 'GSTR', versão, offsets/tamanhos da VROM e do
   catálogo, CRC32 dos 60 bytes anteriores.
2. VROM 'GVRO': índice ordenado por nome (busca binária) + bitmaps 1-bit em
   linhas (row-major, MSB primeiro, bit 1 = pixel aceso), sem cabeçalho BMP.
3. Catálogo 'GCAT': registros de 176 bytes de itens, equipamentos e habilidades,
   lidos de catalog/*.csv.

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

TAMANHO_PARTICAO = 1024 * 1024
SETOR = 4096
VERSAO_STORAGE = 2
VERSAO_CATALOGO = 1

# Pastas de bitmaps que não vão para a VROM (mockups de documentação).
PASTAS_IGNORADAS = {"exemple"}

TAM_NOME_VROM = 32
TAM_REGISTRO = 176

# ---------------------------------------------------------------------------
# Enumerações — manter em sincronia com lib/dados/src/tipos.h
# ---------------------------------------------------------------------------
ALVOS_EFEITO = ["HP", "MP", "ENERGIA", "HUMOR", "ALIMENTACAO", "SAUDE",
                "ATK", "DEF", "FOR", "AGI", "INT", "SOR"]
NATUREZAS = ["FOGO", "TERRA", "AGUA", "AR", "NEUTRA", "ELETRICIDADE", "PLANTA",
             "GELO", "VAPOR", "CRISTAL", "SOMBRA", "LUZ", "PSIQUICA", "LAVA"]
ESTADOS = {"DORMINDO": 0, "DESMAIADO": 1, "ENVENENADO": 2, "PARALISADO": 3,
           "QUEIMADO": 4, "CONGELADO": 5, "CONFUSO": 6, "AMALDICOADO": 7,
           "CANSADO": 8, "FAMINTO": 9,
           "FELIZ": 16, "TRISTE": 17, "ZANGADO": 18, "ENTEDIADO": 19,
           "TODOS": 255}
TIPOS_ITEM = ["COMIDA", "BEBIDA", "POCAO"]
TIPOS_EQUIPAMENTO = ["CABECA", "CORPO", "MAO_ATK", "MAO_DEF", "PES", "ACESSORIO"]
TIPOS_HABILIDADE = ["ATAQUE", "CURA", "EFEITO"]
ALVOS_HABILIDADE = ["USUARIO", "OPONENTE"]


class ErroPacote(Exception):
    pass


# ---------------------------------------------------------------------------
# VROM
# ---------------------------------------------------------------------------
def ler_bmp_1bit(caminho: Path):
    """Lê um BMP de 1 bpp e devolve (largura, altura, dados row-major com 1 = aceso)."""
    b = caminho.read_bytes()
    if len(b) < 62 or b[0:2] != b"BM":
        raise ErroPacote(f"{caminho}: não é um arquivo BMP")
    offset = struct.unpack_from("<I", b, 10)[0]
    largura, altura, _, bpp, compressao = struct.unpack_from("<iiHHI", b, 18)
    if bpp != 1:
        raise ErroPacote(f"{caminho}: {bpp} bpp (esperado 1 bpp; converta com tools/bmp-builder)")
    if compressao != 0:
        raise ErroPacote(f"{caminho}: BMP comprimido não suportado")
    tam_cabecalho = struct.unpack_from("<I", b, 14)[0]
    paleta = b[14 + tam_cabecalho:14 + tam_cabecalho + 8]
    # Índice da paleta -> aceso se a cor for clara.
    aceso = [sum(paleta[i * 4:i * 4 + 3]) / 3 > 127 for i in range(2)]

    passo_bmp = ((largura + 31) // 32) * 4
    passo = (largura + 7) // 8
    de_baixo_pra_cima = altura > 0
    altura = abs(altura)
    dados = bytearray(passo * altura)
    for y in range(altura):
        sy = altura - 1 - y if de_baixo_pra_cima else y
        linha = b[offset + sy * passo_bmp:offset + (sy + 1) * passo_bmp]
        for x in range(largura):
            indice = (linha[x // 8] >> (7 - x % 8)) & 1
            if aceso[indice]:
                dados[y * passo + x // 8] |= 0x80 >> (x % 8)
    return largura, altura, bytes(dados)


def coletar_bitmaps(pasta: Path) -> dict:
    bitmaps = {}
    origem = {}
    for caminho in sorted(pasta.rglob("*.bmp")):
        if PASTAS_IGNORADAS & set(caminho.relative_to(pasta).parts[:-1]):
            continue
        nome = caminho.stem
        if len(nome.encode("utf-8")) >= TAM_NOME_VROM:
            raise ErroPacote(f"{caminho}: nome com mais de {TAM_NOME_VROM - 1} bytes")
        if nome in bitmaps:
            raise ErroPacote(f"Nome duplicado na VROM: '{nome}' ({origem[nome]} e {caminho})")
        bitmaps[nome] = ler_bmp_1bit(caminho)
        origem[nome] = caminho
    return bitmaps


def montar_vrom(bitmaps: dict) -> bytes:
    nomes = sorted(bitmaps, key=lambda n: n.encode("utf-8"))
    indice = []
    dados = bytearray()
    for nome in nomes:
        largura, altura, bits = bitmaps[nome]
        indice.append(struct.pack("<32sHHII", nome.encode("utf-8"), largura, altura, len(dados), len(bits)))
        dados += bits
    cabecalho = struct.pack("<4sIII", b"GVRO", len(nomes), 44, len(dados))
    return cabecalho + b"".join(indice) + bytes(dados)


# ---------------------------------------------------------------------------
# Catálogo
# ---------------------------------------------------------------------------
def texto_fixo(valor: str, tamanho: int, max_chars: int, contexto: str) -> bytes:
    if len(valor) > max_chars:
        raise ErroPacote(f"{contexto}: '{valor}' tem {len(valor)} caracteres (máx. {max_chars})")
    dados = valor.encode("utf-8")
    if len(dados) >= tamanho:
        raise ErroPacote(f"{contexto}: '{valor}' ocupa {len(dados)} bytes (máx. {tamanho - 1})")
    return dados


def enum(valor: str, opcoes, contexto: str) -> int:
    valor = valor.strip().upper()
    if isinstance(opcoes, dict):
        if valor not in opcoes:
            raise ErroPacote(f"{contexto}: '{valor}' inválido (opções: {', '.join(opcoes)})")
        return opcoes[valor]
    if valor not in opcoes:
        raise ErroPacote(f"{contexto}: '{valor}' inválido (opções: {', '.join(opcoes)})")
    return opcoes.index(valor)


def lista_modificadores(texto: str, nomes, contexto: str, maximo: int, faixa):
    """Converte 'HP+5;MP-3' em [(codigo, valor)]."""
    saida = []
    for parte in filter(None, (p.strip() for p in texto.split(";"))):
        pos = max(parte.rfind("+"), parte.rfind("-"))
        if pos <= 0:
            raise ErroPacote(f"{contexto}: modificador '{parte}' sem sinal (+/-)")
        codigo = enum(parte[:pos], nomes, contexto)
        valor = int(parte[pos:])
        if not faixa[0] <= valor <= faixa[1]:
            raise ErroPacote(f"{contexto}: valor {valor} fora da faixa {faixa}")
        saida.append((codigo, valor))
    if len(saida) > maximo:
        raise ErroPacote(f"{contexto}: no máximo {maximo} modificadores")
    return saida


def montar_registro(campos: dict) -> bytes:
    efeitos = campos.get("efeitos", [])
    sobre = campos.get("sobre", [])
    reg = struct.pack("<8B", campos["id"], campos["tipo"], campos["natureza"], campos.get("alvo", 0),
                      campos.get("poder", 0), campos.get("custo_mp", 0), len(efeitos), len(sobre))
    for codigo, valor in efeitos + [(0, 0)] * (4 - len(efeitos)):
        reg += struct.pack("<Bxh", codigo, valor)
    for codigo, valor in sobre + [(0, 0)] * (2 - len(sobre)):
        reg += struct.pack("<Bb", codigo, valor)
    reg += b"\x00" * 4
    reg += struct.pack("<16s32s32s64s", campos["slug"], campos["icone"], campos["nome"], campos["descricao"])
    assert len(reg) == TAM_REGISTRO
    return reg


def ler_csv(caminho: Path, tipo: str, bitmaps: dict) -> list:
    registros = []
    slugs = set()
    with caminho.open(encoding="utf-8", newline="") as arquivo:
        for n, linha in enumerate(csv.DictReader(arquivo), 2):
            ctx = f"{caminho.name}:{n}"
            ident = int(linha["id"])
            if ident != len(registros):
                raise ErroPacote(f"{ctx}: ids devem ser sequenciais a partir de 0 (esperado {len(registros)})")
            if ident > 63:
                raise ErroPacote(f"{ctx}: no máximo 64 registros por catálogo")
            slug = linha["slug"].strip()
            if slug in slugs:
                raise ErroPacote(f"{ctx}: slug '{slug}' duplicado")
            slugs.add(slug)
            icone = linha["icone"].strip()
            if icone not in bitmaps:
                raise ErroPacote(f"{ctx}: ícone '{icone}' não existe na VROM")
            campos = {
                "id": ident,
                "slug": texto_fixo(slug, 16, 15, ctx),
                "icone": texto_fixo(icone, 32, 31, ctx),
                "nome": texto_fixo(linha["nome"].strip(), 32, 20, ctx),
                "descricao": texto_fixo(linha["descricao"].strip(), 64, 40, ctx),
            }
            if tipo == "habilidade":
                campos["tipo"] = enum(linha["tipo"], TIPOS_HABILIDADE, ctx)
                campos["alvo"] = enum(linha["alvo"], ALVOS_HABILIDADE, ctx)
                campos["natureza"] = enum(linha["natureza"], NATUREZAS, ctx)
                campos["poder"] = int(linha["poder"])
                campos["custo_mp"] = int(linha["custo_mp"])
                campos["sobre"] = lista_modificadores(linha["sobre_efeitos"], ESTADOS, ctx, 2, (0, 100))
            else:
                tipos = TIPOS_ITEM if tipo == "item" else TIPOS_EQUIPAMENTO
                campos["tipo"] = enum(linha["tipo"], tipos, ctx)
                campos["natureza"] = enum(linha["afinidade"], NATUREZAS[:5], ctx)
                campos["efeitos"] = lista_modificadores(linha["efeitos"], ALVOS_EFEITO, ctx, 4, (-999, 999))
                campos["sobre"] = lista_modificadores(linha["sobre_efeitos"], ESTADOS, ctx, 2, (-100, 100))
            registros.append(montar_registro(campos))
    return registros


def montar_catalogo(pasta: Path, bitmaps: dict) -> bytes:
    itens = ler_csv(pasta / "itens.csv", "item", bitmaps)
    equipamentos = ler_csv(pasta / "equipamentos.csv", "equipamento", bitmaps)
    habilidades = ler_csv(pasta / "habilidades.csv", "habilidade", bitmaps)
    cabecalho = struct.pack("<4sHHHHHH", b"GCAT", VERSAO_CATALOGO, TAM_REGISTRO,
                            len(itens), len(equipamentos), len(habilidades), 0)
    print(f"[CAT]  {len(itens)} itens, {len(equipamentos)} equipamentos, {len(habilidades)} habilidades")
    return cabecalho + b"".join(itens + equipamentos + habilidades)


# ---------------------------------------------------------------------------
# Imagem
# ---------------------------------------------------------------------------
def alinhar(valor: int, passo: int) -> int:
    return (valor + passo - 1) // passo * passo


def montar_imagem(pasta_bitmaps: Path, pasta_catalogo: Path, completa: bool) -> bytes:
    bitmaps = coletar_bitmaps(pasta_bitmaps)
    vrom = montar_vrom(bitmaps)
    print(f"[VROM] {len(bitmaps)} bitmaps, {len(vrom)} bytes")
    catalogo = montar_catalogo(pasta_catalogo, bitmaps)

    off_vrom = 64
    off_cat = alinhar(off_vrom + len(vrom), 4)
    fim = off_cat + len(catalogo)
    if fim > TAMANHO_PARTICAO:
        raise ErroPacote(f"Dados ({fim} bytes) excedem a partição de {TAMANHO_PARTICAO} bytes")

    cabecalho = struct.pack("<4sHHIIII36s", b"GSTR", VERSAO_STORAGE, 0,
                            off_vrom, len(vrom), off_cat, len(catalogo), b"\x00" * 36)
    cabecalho += struct.pack("<I", zlib.crc32(cabecalho) & 0xFFFFFFFF)

    tamanho = TAMANHO_PARTICAO if completa else alinhar(fim, SETOR)
    imagem = bytearray(b"\xFF" * tamanho)
    imagem[0:64] = cabecalho
    imagem[off_vrom:off_vrom + len(vrom)] = vrom
    imagem[off_cat:off_cat + len(catalogo)] = catalogo
    return bytes(imagem)


def main():
    raiz = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description="Empacotador da partição storage (VROM + catálogo) do Guará.")
    parser.add_argument("-b", "--bitmaps", default=raiz / "bitmaps", type=Path, help="Pasta dos bitmaps")
    parser.add_argument("-c", "--catalogo", default=raiz / "catalog", type=Path, help="Pasta dos CSVs do catálogo")
    parser.add_argument("-o", "--output", default=raiz / ".pio" / "storage.bin", type=Path, help="Arquivo de saída")
    parser.add_argument("--completa", action="store_true", help="Gera a imagem com os 1024 KB completos")
    args = parser.parse_args()

    try:
        imagem = montar_imagem(args.bitmaps, args.catalogo, args.completa)
    except ErroPacote as erro:
        print(f"ERRO: {erro}", file=sys.stderr)
        sys.exit(1)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(imagem)
    print(f"[OK]   {args.output} ({len(imagem)} bytes)")


if __name__ == "__main__":
    main()
