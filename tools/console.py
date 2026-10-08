#!/usr/bin/env python3
"""
tools/console.py
Console de depuração do Guará pela serial: envia teclas e captura a tela.

    python3 tools/console.py [-p /dev/ttyACM0] teclas "jjsk"       # envia teclas (w s a d j k)
    python3 tools/console.py tela saida.png [--escala 4]           # captura o framebuffer
    python3 tools/console.py estado                                # estado do Guará
    python3 tools/console.py log [segundos]                        # mostra o log
    python3 tools/console.py roteiro "espera 4" "tela a.png" "teclas jj" "tela b.png" "estado"

Neste adaptador USB (CH343) abrir a porta reinicia o ESP32: use "roteiro" para
executar vários passos na mesma conexão.

Usa o pyserial do ambiente do PlatformIO (~/.platformio/penv/bin/python).
"""

import argparse
import struct
import sys
import time
import zlib

import serial


def abrir(porta):
    s = serial.Serial()
    s.port = porta
    s.baudrate = 115200
    s.timeout = 0.2
    s.dtr = False  # DTR e RTS inativos: GPIO0 alto, boot normal (abrir a porta reinicia o ESP32)
    s.rts = False
    s.open()
    return s


def ler_linha_com(s, prefixo, espera=3.0):
    fim = time.time() + espera
    buf = b""
    while time.time() < fim:
        buf += s.read(4096)
        while b"\n" in buf:
            linha, buf = buf.split(b"\n", 1)
            linha = linha.strip()
            if linha.startswith(prefixo):
                return linha[len(prefixo):].decode(errors="replace")
    return None


def salvar_png(quadro, caminho, escala):
    largura, altura = 128 * escala, 64 * escala
    linhas = []
    for y in range(64):
        linha = bytearray()
        for x in range(128):
            aceso = (quadro[(y // 8) * 128 + x] >> (y % 8)) & 1
            linha += (b"\xd8\xee\xff" if aceso else b"\x0a\x0c\x14") * escala
        for _ in range(escala):
            linhas.append(b"\x00" + bytes(linha))

    def bloco(tipo, dados):
        return struct.pack(">I", len(dados)) + tipo + dados + struct.pack(">I", zlib.crc32(tipo + dados) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + bloco(b"IHDR", struct.pack(">IIBBBBB", largura, altura, 8, 2, 0, 0, 0))
    png += bloco(b"IDAT", zlib.compress(b"".join(linhas))) + bloco(b"IEND", b"")
    with open(caminho, "wb") as f:
        f.write(png)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--porta", default="/dev/ttyACM0")
    sub = ap.add_subparsers(dest="cmd", required=True)
    t = sub.add_parser("teclas")
    t.add_argument("sequencia")
    t.add_argument("--intervalo", type=float, default=0.35)
    c = sub.add_parser("tela")
    c.add_argument("saida")
    c.add_argument("--escala", type=int, default=4)
    sub.add_parser("estado")
    lg = sub.add_parser("log")
    lg.add_argument("segundos", type=float, nargs="?", default=5)
    r = sub.add_parser("roteiro")
    r.add_argument("passos", nargs="+")
    r.add_argument("--escala", type=int, default=4)
    args = ap.parse_args()

    s = abrir(args.porta)
    if args.cmd == "teclas":
        for ch in args.sequencia:
            s.write(ch.encode())
            s.flush()
            time.sleep(args.intervalo)
    elif args.cmd == "tela":
        s.reset_input_buffer()
        s.write(b"P")
        hexa = ler_linha_com(s, b"FB:")
        if not hexa or len(hexa) != 2048:
            print("Sem resposta do aparelho", file=sys.stderr)
            sys.exit(1)
        salvar_png(bytes.fromhex(hexa), args.saida, args.escala)
        print(args.saida)
    elif args.cmd == "estado":
        s.reset_input_buffer()
        s.write(b"S")
        print(ler_linha_com(s, b"ST:") or "Sem resposta")
    elif args.cmd == "roteiro":
        for passo in args.passos:
            nome, _, arg = passo.partition(" ")
            if nome == "espera":
                time.sleep(float(arg))
                s.reset_input_buffer()
            elif nome == "teclas":
                for ch in arg:
                    s.write(ch.encode())
                    s.flush()
                    time.sleep(0.35)
            elif nome == "tela":
                time.sleep(0.3)
                s.reset_input_buffer()
                s.write(b"P")
                hexa = ler_linha_com(s, b"FB:")
                if hexa and len(hexa) == 2048:
                    salvar_png(bytes.fromhex(hexa), arg, args.escala)
                    print("tela:", arg)
                else:
                    print("tela: sem resposta", file=sys.stderr)
            elif nome == "estado":
                s.reset_input_buffer()
                s.write(b"S")
                print(ler_linha_com(s, b"ST:") or "estado: sem resposta")
            elif nome == "log":
                fim = time.time() + float(arg or 2)
                while time.time() < fim:
                    dados = s.read(4096)
                    if dados:
                        sys.stdout.write(dados.decode(errors="replace"))
            else:
                print(f"passo desconhecido: {passo}", file=sys.stderr)
    else:
        fim = time.time() + args.segundos
        while time.time() < fim:
            dados = s.read(4096)
            if dados:
                sys.stdout.write(dados.decode(errors="replace"))
                sys.stdout.flush()
    s.close()


if __name__ == "__main__":
    main()
