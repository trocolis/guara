#!/usr/bin/env python3
"""
tools/console.py
Console de depuração do Guará pela serial: envia teclas e captura a tela.

    python3 tools/console.py [-p /dev/ttyACM0] keys "jjsk"          # envia teclas (w s a d j k)
    python3 tools/console.py screen saida.png [--scale 4]          # captura o framebuffer
    python3 tools/console.py state                                 # estado do Guará
    python3 tools/console.py log [segundos]                        # mostra o log
    python3 tools/console.py script "wait 4" "screen a.png" "keys jj" "screen b.png" "state"
    python3 tools/console.py script "wait 4" "sleep" "log 60"         # dorme e mostra o log ao acordar

Neste adaptador USB (CH343) abrir a porta reinicia o ESP32: use "script" para
executar vários passos na mesma conexão.

Usa o pyserial do ambiente do PlatformIO (~/.platformio/penv/bin/python).
"""

import argparse
import struct
import sys
import time
import zlib

import serial

# Cores usadas no PNG: pixel aceso (branco azulado do OLED) e apagado
LIT_RGB = b"\xd8\xee\xff"
DARK_RGB = b"\x0a\x0c\x14"


def open_port(port_name):
    """Abre a porta serial sem segurar o ESP32 em modo de gravação."""
    port = serial.Serial()
    port.port = port_name
    port.baudrate = 115200
    port.timeout = 0.2
    port.dtr = False  # DTR e RTS inativos: GPIO0 alto, boot normal (abrir a porta reinicia o ESP32)
    port.rts = False
    port.open()
    return port


def read_line_with_prefix(port, prefix, timeout=3.0):
    """Lê linhas até achar uma que comece com `prefix`; devolve o resto da linha ou None."""
    deadline = time.time() + timeout
    buffer = b""
    while time.time() < deadline:
        buffer += port.read(4096)
        while b"\n" in buffer:
            line, buffer = buffer.split(b"\n", 1)
            line = line.strip()
            if line.startswith(prefix):
                return line[len(prefix):].decode(errors="replace")
    return None


def save_png(framebuffer, path, scale):
    """Converte o framebuffer (8 páginas x 128 colunas) num PNG ampliado."""
    width, height = 128 * scale, 64 * scale
    rows = []
    for y in range(64):
        row = bytearray()
        for x in range(128):
            lit = (framebuffer[(y // 8) * 128 + x] >> (y % 8)) & 1
            row += (LIT_RGB if lit else DARK_RGB) * scale
        for _ in range(scale):
            rows.append(b"\x00" + bytes(row))  # filtro PNG "none" em cada linha

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b"")
    with open(path, "wb") as output:
        output.write(png)


def wake(port):
    """Acorda o ESP32 do light sleep (modo Ocioso): os bytes do despertar se perdem."""
    port.write(b"\0\0\0\0")
    port.flush()
    time.sleep(0.05)


def capture_screen(port, path, scale):
    """Pede o framebuffer ('P') e salva como PNG. Retorna False se não houve resposta."""
    time.sleep(0.3)
    port.reset_input_buffer()
    wake(port)
    port.write(b"P")
    hex_frame = read_line_with_prefix(port, b"FB:")
    if not hex_frame or len(hex_frame) != 2048:
        return False
    save_png(bytes.fromhex(hex_frame), path, scale)
    return True


def send_keys(port, sequence, interval=0.35):
    """Envia teclas uma a uma (w s a d = direcional, j = A, k = B)."""
    for key in sequence:
        wake(port)
        port.write(key.encode())
        port.flush()
        time.sleep(interval)


def request_state(port):
    """Pede o estado do Guará ('S') e devolve a linha recebida."""
    port.reset_input_buffer()
    wake(port)
    port.write(b"S")
    return read_line_with_prefix(port, b"ST:")


def show_log(port, seconds):
    """Repassa a saída da serial para o terminal por alguns segundos."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        data = port.read(4096)
        if data:
            sys.stdout.write(data.decode(errors="replace"))
            sys.stdout.flush()


def run_script(port, steps, scale):
    """Executa os passos de um roteiro na mesma conexão (o ESP32 só reinicia uma vez)."""
    for step in steps:
        name, _, argument = step.partition(" ")
        if name == "wait":
            time.sleep(float(argument))
            port.reset_input_buffer()
        elif name == "keys":
            send_keys(port, argument)
        elif name == "screen":
            if capture_screen(port, argument, scale):
                print("screen:", argument)
            else:
                print("screen: sem resposta", file=sys.stderr)
        elif name == "state":
            print(request_state(port) or "state: sem resposta")
        elif name == "log":
            show_log(port, float(argument or 2))
        elif name == "sleep":
            wake(port)
            port.write(b"Z")  # deep sleep imediato
            port.flush()
        else:
            print(f"passo desconhecido: {step}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-p", "--port", default="/dev/ttyACM0")
    commands = parser.add_subparsers(dest="command", required=True)
    keys = commands.add_parser("keys")
    keys.add_argument("sequence")
    keys.add_argument("--interval", type=float, default=0.35)
    screen = commands.add_parser("screen")
    screen.add_argument("output")
    screen.add_argument("--scale", type=int, default=4)
    commands.add_parser("state")
    log = commands.add_parser("log")
    log.add_argument("seconds", type=float, nargs="?", default=5)
    script = commands.add_parser("script")
    script.add_argument("steps", nargs="+")
    script.add_argument("--scale", type=int, default=4)
    args = parser.parse_args()

    port = open_port(args.port)
    if args.command == "keys":
        send_keys(port, args.sequence, args.interval)
    elif args.command == "screen":
        if not capture_screen(port, args.output, args.scale):
            print("Sem resposta do aparelho", file=sys.stderr)
            sys.exit(1)
        print(args.output)
    elif args.command == "state":
        print(request_state(port) or "Sem resposta")
    elif args.command == "log":
        show_log(port, args.seconds)
    else:
        run_script(port, args.steps, args.scale)
    port.close()


if __name__ == "__main__":
    main()
