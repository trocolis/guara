#!/usr/bin/env python3
"""
tools/bmp-builder/bmp_builder.py
Conversor universal de imagens para BMP monocromático 1-bit (1 bpp) canônico para o Guará.

Suporta qualquer formato de imagem comum (PNG, JPG, WebP, GIF, SVG, BMP, TIFF)
e gera arquivos BMP 1-bit padrão Windows DIB com paleta de 2 cores (Preto/Branco),
alinhamento DWORD (múltiplo de 4 bytes por linha) e varredura bottom-up,
100% compatíveis com a VROM do Guará e os displays OLED SSD1306 / SH1106.
"""

import os
import sys
import argparse
import struct
import math
import glob

# ==============================================================================
# Presets de Resolução do Guará
# ==============================================================================
PRESETS = {
    'icon': (16, 16, 'Ícone de catálogo / habilidade (16x16)'),
    'sprite': (32, 32, 'Sprite de Guará / Pet / NPC (32x32)'),
    'mannequin': (32, 56, 'Manequim de equipamentos (32x56)'),
    'boss': (64, 64, 'Sprite grande de Pet / Chefe (64x64)'),
    'screen': (128, 64, 'Tela cheia do display OLED (128x64)'),
}

# Matriz de Bayer 4x4 para Ordered Dithering
BAYER_4X4 = [
    [ 0,  8,  2, 10],
    [12,  4, 14,  6],
    [ 3, 11,  1,  9],
    [15,  7, 13,  5]
]

# ==============================================================================
# Carregador de Imagens com Fallbacks Resilientes
# ==============================================================================
def load_image(filepath):
    """
    Carrega qualquer formato de imagem e retorna:
    (width, height, list_of_rgba_tuples) onde cada tupla é (r, g, b, a) com 0..255.
    Tenta em ordem: GdkPixbuf -> PIL/Pillow -> ImageMagick (magick/convert).
    """
    if not os.path.isfile(filepath):
        raise FileNotFoundError(f"Arquivo não encontrado: {filepath}")

    # Método 1: GdkPixbuf (presente nativamente em sistemas Linux desktop)
    try:
        import gi
        gi.require_version('GdkPixbuf', '2.0')
        from gi.repository import GdkPixbuf
        pb = GdkPixbuf.Pixbuf.new_from_file(filepath)
        w, h = pb.get_width(), pb.get_height()
        channels = pb.get_n_channels()
        rowstride = pb.get_rowstride()
        pixels = pb.get_pixels()

        rgba_data = []
        for y in range(h):
            row_start = y * rowstride
            for x in range(w):
                p_start = row_start + x * channels
                r = pixels[p_start]
                g = pixels[p_start + 1]
                b = pixels[p_start + 2]
                a = pixels[p_start + 3] if channels >= 4 else 255
                rgba_data.append((r, g, b, a))
        return w, h, rgba_data
    except Exception:
        pass

    # Método 2: PIL / Pillow
    try:
        from PIL import Image
        im = Image.open(filepath).convert('RGBA')
        w, h = im.size
        rgba_data = list(im.getdata())
        return w, h, rgba_data
    except Exception:
        pass

    # Método 3: ImageMagick CLI (magick ou convert)
    import subprocess
    import shutil
    cmd = shutil.which('magick') or shutil.which('convert')
    if cmd:
        try:
            # Obter dimensões
            dim_out = subprocess.check_output([cmd, 'identify', '-format', '%w %h', filepath]).decode().strip()
            w, h = map(int, dim_out.split())
            # Obter raw RGBA
            raw_bytes = subprocess.check_output([cmd, filepath, '-depth', '8', 'rgba:-'])
            rgba_data = []
            for i in range(0, len(raw_bytes), 4):
                rgba_data.append((raw_bytes[i], raw_bytes[i+1], raw_bytes[i+2], raw_bytes[i+3]))
            return w, h, rgba_data
        except Exception:
            pass

    raise RuntimeError(
        f"Não foi possível carregar a imagem '{filepath}'.\n"
        "Instale GdkPixbuf (python3-gi / python3-gobject), Pillow (pip install pillow) ou ImageMagick."
    )

# ==============================================================================
# Redimensionamento e Ajuste de Escala
# ==============================================================================
def resize_rgba(w, h, rgba, target_w, target_h, fit_mode='stretch', interpolation='bilinear', bg_color=(0, 0, 0, 0)):
    """
    Redimensiona dados RGBA para (target_w, target_h).
    fit_mode: 'stretch', 'fit' (aspect ratio com padding), 'crop' (aspect ratio central)
    interpolation: 'nearest' (pixel art), 'bilinear' (suave)
    """
    if w == target_w and h == target_h:
        return target_w, target_h, rgba

    if fit_mode == 'stretch':
        return _scale_canvas(w, h, rgba, target_w, target_h, interpolation)

    elif fit_mode == 'fit':
        scale = min(target_w / w, target_h / h)
        scaled_w = max(1, int(round(w * scale)))
        scaled_h = max(1, int(round(h * scale)))
        _, _, scaled_rgba = _scale_canvas(w, h, rgba, scaled_w, scaled_h, interpolation)

        # Centraliza no canvas target
        out_rgba = [bg_color] * (target_w * target_h)
        offset_x = (target_w - scaled_w) // 2
        offset_y = (target_h - scaled_h) // 2

        for sy in range(scaled_h):
            for sx in range(scaled_w):
                tx = offset_x + sx
                ty = offset_y + sy
                if 0 <= tx < target_w and 0 <= ty < target_h:
                    out_rgba[ty * target_w + tx] = scaled_rgba[sy * scaled_w + sx]
        return target_w, target_h, out_rgba

    elif fit_mode == 'crop':
        scale = max(target_w / w, target_h / h)
        scaled_w = max(1, int(round(w * scale)))
        scaled_h = max(1, int(round(h * scale)))
        _, _, scaled_rgba = _scale_canvas(w, h, rgba, scaled_w, scaled_h, interpolation)

        # Recorta o centro
        crop_x = (scaled_w - target_w) // 2
        crop_y = (scaled_h - target_h) // 2
        out_rgba = []
        for ty in range(target_h):
            for tx in range(target_w):
                sx = crop_x + tx
                sy = crop_y + ty
                out_rgba.append(scaled_rgba[sy * scaled_w + sx])
        return target_w, target_h, out_rgba

    else:
        raise ValueError(f"Modo fit inválido: {fit_mode}")

def _scale_canvas(w, h, rgba, target_w, target_h, interpolation):
    out = []
    x_ratio = float(w) / target_w
    y_ratio = float(h) / target_h

    if interpolation == 'nearest':
        for ty in range(target_h):
            sy = min(int(ty * y_ratio), h - 1)
            row_start = sy * w
            for tx in range(target_w):
                sx = min(int(tx * x_ratio), w - 1)
                out.append(rgba[row_start + sx])
    else:  # bilinear
        for ty in range(target_h):
            src_y = ty * y_ratio
            y1 = int(src_y)
            y2 = min(y1 + 1, h - 1)
            dy = src_y - y1

            for tx in range(target_w):
                src_x = tx * x_ratio
                x1 = int(src_x)
                x2 = min(x1 + 1, w - 1)
                dx = src_x - x1

                p11 = rgba[y1 * w + x1]
                p12 = rgba[y1 * w + x2]
                p21 = rgba[y2 * w + x1]
                p22 = rgba[y2 * w + x2]

                res = []
                for c in range(4):
                    val = (p11[c] * (1 - dx) * (1 - dy) +
                           p12[c] * dx * (1 - dy) +
                           p21[c] * (1 - dx) * dy +
                           p22[c] * dx * dy)
                    res.append(int(round(val)))
                out.append(tuple(res))
    return target_w, target_h, out

# ==============================================================================
# Algoritmos de Conversão Monocromática (1-Bit)
# ==============================================================================
def rgba_to_luminance(rgba, brightness=0.0, contrast=1.0, alpha_mode='black', alpha_threshold=128):
    """
    Converte lista de (R, G, B, A) em mapa de luminância contínua (0.0 .. 255.0).
    """
    lum_map = []
    for r, g, b, a in rgba:
        # Padrão ITU-R BT.601
        lum = 0.299 * r + 0.587 * g + 0.114 * b

        # Ajuste de Contraste e Brilho
        if contrast != 1.0 or brightness != 0.0:
            lum = (lum - 128.0) * contrast + 128.0 + brightness
        lum = max(0.0, min(255.0, lum))

        # Tratamento do Canal Alfa / Transparência
        alpha_factor = a / 255.0
        if alpha_mode == 'black':
            lum = lum * alpha_factor
        elif alpha_mode == 'white':
            lum = lum * alpha_factor + 255.0 * (1.0 - alpha_factor)
        elif alpha_mode == 'threshold':
            if a < alpha_threshold:
                lum = 0.0

        lum_map.append(max(0.0, min(255.0, lum)))
    return lum_map

def calculate_otsu_threshold(lum_map):
    """
    Algoritmo de Otsu para cálculo automático do threshold ótimo baseado em histograma.
    """
    hist = [0] * 256
    for val in lum_map:
        idx = max(0, min(255, int(round(val))))
        hist[idx] += 1

    total = len(lum_map)
    sum_all = sum(i * hist[i] for i in range(256))

    sum_b = 0
    w_b = 0
    max_variance = 0.0
    best_thresh = 128

    for t in range(256):
        w_b += hist[t]
        if w_b == 0:
            continue
        w_f = total - w_b
        if w_f == 0:
            break

        sum_b += t * hist[t]
        m_b = sum_b / w_b
        m_f = (sum_all - sum_b) / w_f

        # Variância entre classes
        variance = w_b * w_f * ((m_b - m_f) ** 2)
        if variance > max_variance:
            max_variance = variance
            best_thresh = t

    return best_thresh

def binarize(w, h, lum_map, method='threshold', threshold=128, invert=False):
    """
    Binariza o mapa de luminância em lista de booleanos (True = Branco/Aceso, False = Preto/Apagado).
    Métodos: 'threshold', 'otsu', 'floyd-steinberg', 'atkinson', 'bayer'.
    """
    pixels = [False] * (w * h)

    if method == 'otsu':
        threshold = calculate_otsu_threshold(lum_map)
        method = 'threshold'

    if method == 'threshold':
        for i, val in enumerate(lum_map):
            is_white = (val >= threshold)
            pixels[i] = not is_white if invert else is_white

    elif method == 'floyd-steinberg':
        work = list(lum_map)
        for y in range(h):
            for x in range(w):
                idx = y * w + x
                old_val = work[idx]
                new_val = 255.0 if old_val >= threshold else 0.0
                err = old_val - new_val

                is_white = (new_val == 255.0)
                pixels[idx] = not is_white if invert else is_white

                # Difusão de erro: 7/16, 3/16, 5/16, 1/16
                if x + 1 < w:
                    work[y * w + (x + 1)] += err * (7.0 / 16.0)
                if x - 1 >= 0 and y + 1 < h:
                    work[(y + 1) * w + (x - 1)] += err * (3.0 / 16.0)
                if y + 1 < h:
                    work[(y + 1) * w + x] += err * (5.0 / 16.0)
                if x + 1 < w and y + 1 < h:
                    work[(y + 1) * w + (x + 1)] += err * (1.0 / 16.0)

    elif method == 'atkinson':
        # Dithering de Bill Atkinson (Macintosh 1984): dispersa 1/8 do erro para 6 vizinhos
        work = list(lum_map)
        for y in range(h):
            for x in range(w):
                idx = y * w + x
                old_val = work[idx]
                new_val = 255.0 if old_val >= threshold else 0.0
                err = old_val - new_val
                disp = err / 8.0

                is_white = (new_val == 255.0)
                pixels[idx] = not is_white if invert else is_white

                if x + 1 < w:
                    work[y * w + (x + 1)] += disp
                if x + 2 < w:
                    work[y * w + (x + 2)] += disp
                if x - 1 >= 0 and y + 1 < h:
                    work[(y + 1) * w + (x - 1)] += disp
                if y + 1 < h:
                    work[(y + 1) * w + x] += disp
                if x + 1 < w and y + 1 < h:
                    work[(y + 1) * w + (x + 1)] += disp
                if y + 2 < h:
                    work[(y + 2) * w + x] += disp

    elif method == 'bayer':
        for y in range(h):
            for x in range(w):
                idx = y * w + x
                m_val = (BAYER_4X4[y % 4][x % 4] / 16.0 - 0.5) * 64.0
                local_thresh = max(0.0, min(255.0, threshold + m_val))
                is_white = (lum_map[idx] >= local_thresh)
                pixels[idx] = not is_white if invert else is_white

    else:
        raise ValueError(f"Método de binarização desconhecido: {method}")

    return pixels

# ==============================================================================
# Gerador de Arquivo BMP 1-Bit Canônico
# ==============================================================================
def create_bmp_1bit(width, height, bit_pixels):
    """
    Empacota a lista de bits booleanos (largura x altura) em um arquivo BMP 1-bit padrão:
    - BITMAPFILEHEADER (14 bytes)
    - BITMAPINFOHEADER (40 bytes)
    - Paleta de 2 cores (8 bytes): Index 0 = Preto (0,0,0,0), Index 1 = Branco (255,255,255,0)
    - Varredura de linhas bottom-up (de baixo para cima)
    - Alinhamento DWORD (linhas preenchidas com zeros para múltiplos de 4 bytes)
    - MSB-first dentro de cada byte (bit 7 = pixel mais à esquerda)
    """
    palette = b'\x00\x00\x00\x00\xff\xff\xff\x00'
    row_bytes = ((width + 31) // 32) * 4
    image_size = row_bytes * height
    offset = 14 + 40 + len(palette)  # 62 bytes
    file_size = offset + image_size

    # BITMAPFILEHEADER
    header = struct.pack('<2sIHHI', b'BM', file_size, 0, 0, offset)

    # BITMAPINFOHEADER (biWidth, biHeight positivo = bottom-up, biPlanes=1, biBitCount=1, biCompression=0)
    info = struct.pack('<IIIHHIIIIII',
                       40,            # biSize
                       width,         # biWidth
                       height,        # biHeight
                       1,             # biPlanes
                       1,             # biBitCount (1 bpp)
                       0,             # biCompression (BI_RGB)
                       image_size,    # biSizeImage
                       2835,          # biXPelsPerMeter (~72 DPI)
                       2835,          # biYPelsPerMeter
                       2,             # biClrUsed
                       2)             # biClrImportant

    # Dados de pixel
    pixel_data = bytearray(image_size)
    for y in range(height):
        # Varredura de baixo para cima (bottom-up)
        src_y = height - 1 - y
        row_offset = y * row_bytes
        for x in range(width):
            if bit_pixels[src_y * width + x]:
                byte_pos = row_offset + (x // 8)
                bit_pos = 7 - (x % 8)
                pixel_data[byte_pos] |= (1 << bit_pos)

    return header + info + palette + bytes(pixel_data)

# ==============================================================================
# Exportação em Formato C Array (.h)
# ==============================================================================
def create_c_array(width, height, bit_pixels, var_name):
    """
    Gera array de bytes em código C (1 bit por pixel, MSB-first) pronto para firmware.
    """
    bytes_per_row = (width + 7) // 8
    total_bytes = bytes_per_row * height
    raw_bytes = bytearray(total_bytes)

    for y in range(height):
        for x in range(width):
            if bit_pixels[y * width + x]:
                byte_idx = y * bytes_per_row + (x // 8)
                bit_idx = 7 - (x % 8)
                raw_bytes[byte_idx] |= (1 << bit_idx)

    lines = []
    lines.append(f"// Gerado automaticamente por bmp-builder (Guará)")
    lines.append(f"// Dimensões: {width}x{height} pixels ({total_bytes} bytes)")
    lines.append(f"#pragma once")
    lines.append(f"#include <stdint.h>")
    lines.append(f"")
    lines.append(f"#define {var_name.upper()}_WIDTH  {width}")
    lines.append(f"#define {var_name.upper()}_HEIGHT {height}")
    lines.append(f"")
    lines.append(f"static const uint8_t {var_name}[{total_bytes}] = {{")

    # Formata em blocos legíveis
    for y in range(height):
        row_slice = raw_bytes[y * bytes_per_row : (y + 1) * bytes_per_row]
        hex_row = ", ".join(f"0x{b:02X}" for b in row_slice)
        lines.append(f"    {hex_row}, // Linha {y}")

    lines.append(f"}};\n")
    return "\n".join(lines)

# ==============================================================================
# Visualizador ASCII / Unicode no Terminal
# ==============================================================================
def render_terminal_preview(width, height, bit_pixels, title="PREVIEW MONOCROMÁTICO (OLED 1-BIT)"):
    """
    Renderiza os pixels 1-bit diretamente no terminal usando blocos Unicode de meio caractere (2 pixels por linha de texto).
    """
    lit_pixels = sum(1 for p in bit_pixels if p)
    fill_pct = (lit_pixels / (width * height)) * 100.0

    border_top = "┌" + "─" * width + "┐"
    border_bot = "└" + "─" * width + "┘"

    output = []
    output.append(f"\n{title}")
    output.append(f"Dimensões: {width}x{height} | Pixels Acesos: {lit_pixels}/{width*height} ({fill_pct:.1f}%)")
    output.append(border_top)

    for y in range(0, height, 2):
        line = ["│"]
        for x in range(width):
            top_lit = bit_pixels[y * width + x]
            bot_lit = bit_pixels[(y + 1) * width + x] if (y + 1 < height) else False

            if top_lit and bot_lit:
                line.append("█")
            elif top_lit:
                line.append("▀")
            elif bot_lit:
                line.append("▄")
            else:
                line.append(" ")
        line.append("│")
        output.append("".join(line))

    output.append(border_bot)
    return "\n".join(output)

# ==============================================================================
# Função Principal de Conversão
# ==============================================================================
def convert_image(input_path, output_path=None, preset=None, width=None, height=None,
                  fit='fit', interpolation='nearest', method='threshold',
                  threshold=128, invert=False, brightness=0.0, contrast=1.0,
                  alpha_mode='black', alpha_threshold=128, gen_c_array=False,
                  preview=False):
    """
    Executa o fluxo completo de conversão de uma imagem.
    """
    # 1. Carrega imagem original
    orig_w, orig_h, rgba = load_image(input_path)

    # 2. Determina resolução alvo
    if preset and preset in PRESETS:
        target_w, target_h = PRESETS[preset][0], PRESETS[preset][1]
    elif width and height:
        target_w, target_h = width, height
    elif width and not height:
        target_w = width
        target_h = max(1, int(round(orig_h * (width / orig_w))))
    elif height and not width:
        target_h = height
        target_w = max(1, int(round(orig_w * (height / orig_h))))
    else:
        target_w, target_h = orig_w, orig_h

    # 3. Redimensiona conforme o modo
    scaled_w, scaled_h, scaled_rgba = resize_rgba(
        orig_w, orig_h, rgba, target_w, target_h,
        fit_mode=fit, interpolation=interpolation
    )

    # 4. Extrai luminância e aplica ajustes de brilho/contraste/alfa
    lum_map = rgba_to_luminance(
        scaled_rgba, brightness=brightness, contrast=contrast,
        alpha_mode=alpha_mode, alpha_threshold=alpha_threshold
    )

    # 5. Binariza para 1-bit
    bit_pixels = binarize(
        scaled_w, scaled_h, lum_map,
        method=method, threshold=threshold, invert=invert
    )

    # 6. Gera BMP 1-bit
    bmp_bytes = create_bmp_1bit(scaled_w, scaled_h, bit_pixels)

    # 7. Salva arquivo se caminho de saída fornecido
    if output_path:
        os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
        with open(output_path, 'wb') as f:
            f.write(bmp_bytes)

    # 8. Exporta C Array se solicitado
    c_header_content = None
    if gen_c_array:
        base_name = os.path.splitext(os.path.basename(output_path or input_path))[0]
        c_name = "".join(c if c.isalnum() else "_" for c in base_name)
        c_header_content = create_c_array(scaled_w, scaled_h, bit_pixels, c_name)
        if output_path:
            h_path = os.path.splitext(output_path)[0] + ".h"
            with open(h_path, 'w', encoding='utf-8') as f:
                f.write(c_header_content)

    # 9. Exibe preview no terminal se solicitado
    if preview:
        print(render_terminal_preview(scaled_w, scaled_h, bit_pixels, f"PREVIEW: {os.path.basename(input_path)} -> {scaled_w}x{scaled_h}"))

    return {
        'orig_size': (orig_w, orig_h),
        'target_size': (scaled_w, scaled_h),
        'bytes_len': len(bmp_bytes),
        'c_array': c_header_content
    }

# ==============================================================================
# Interface de Linha de Comando (CLI)
# ==============================================================================
def main():
    parser = argparse.ArgumentParser(
        description="Conversor de imagens para BMP monocromático 1-bit compatível com o Guará.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Exemplos de uso:
  # Converter um ícone para 16x16 com detecção automática Otsu
  python3 bmp_builder.py item.png -o icones/item.bmp --preset icon --method otsu

  # Converter sprite para 32x32 preservando pixel art (nearest neighbor)
  python3 bmp_builder.py pet.png -o pet.bmp --preset sprite --resample nearest

  # Converter imagem grande para tela OLED 128x64 com Dithering Atkinson e preview
  python3 bmp_builder.py splash.png -o splash.bmp --preset screen --method atkinson -p

  # Converter todos os PNGs de uma pasta para ícones 16x16
  python3 bmp_builder.py "raw_icons/*.png" -o icones/ --preset icon

  # Inverter cores e exportar também código C (.h)
  python3 bmp_builder.py logo.png -o logo.bmp -w 32 -h 32 --invert --c-array
        """
    )

    parser.add_argument("input", nargs="+", help="Arquivo de imagem de entrada, diretório ou padrão glob (*.png)")
    parser.add_argument("-o", "--output", help="Arquivo de saída .bmp ou diretório de destino")
    
    # Dimensões e Presets
    group_dim = parser.add_argument_group("Dimensões")
    group_dim.add_argument("--preset", choices=list(PRESETS.keys()),
                           help="Preset Guará: icon (16x16), sprite (32x32), mannequin (32x56), boss (64x64), screen (128x64)")
    group_dim.add_argument("-w", "--width", type=int, help="Largura desejada em pixels")
    group_dim.add_argument("-H", "--height", type=int, help="Altura desejada em pixels")
    group_dim.add_argument("--fit", choices=['stretch', 'fit', 'crop'], default='fit',
                           help="Modo de enquadramento (padrão: fit com proporção mantida)")
    group_dim.add_argument("--resample", choices=['nearest', 'bilinear'], default='nearest',
                           help="Algoritmo de interpolação: nearest (pixel art) ou bilinear (suave)")

    # Binarização e Dithering
    group_algo = parser.add_argument_group("Algoritmo de Binarização")
    group_algo.add_argument("-m", "--method", choices=['threshold', 'otsu', 'floyd-steinberg', 'atkinson', 'bayer'],
                            default='threshold', help="Método de conversão (padrão: threshold)")
    group_algo.add_argument("-t", "--threshold", type=int, default=128,
                            help="Limiar de corte para binarização (0..255, padrão: 128)")
    group_algo.add_argument("-i", "--invert", action="store_true",
                            help="Inverter cores (preto vira branco e vice-versa)")

    # Ajustes de Cor e Transparência
    group_adj = parser.add_argument_group("Ajustes de Imagem")
    group_adj.add_argument("--brightness", type=float, default=0.0,
                           help="Ajuste de brilho (-100 a +100, padrão: 0)")
    group_adj.add_argument("--contrast", type=float, default=1.0,
                           help="Multiplicador de contraste (0.1 a 3.0, padrão: 1.0)")
    group_adj.add_argument("--alpha-mode", choices=['black', 'white', 'threshold'], default='black',
                           help="Como tratar canal alfa transparente (padrão: black -> vira fundo desligado)")
    group_adj.add_argument("--alpha-threshold", type=int, default=128,
                           help="Limiar para considerar pixel transparente quando alpha-mode=threshold")

    # Saídas Adicionais
    group_out = parser.add_argument_group("Saídas e Visualização")
    group_out.add_argument("-p", "--preview", action="store_true",
                           help="Exibir preview visual da imagem no terminal (ASCII/Unicode)")
    group_out.add_argument("-c", "--c-array", action="store_true",
                           help="Exportar também arquivo C Header (.h) com o array de bytes embutido")

    args = parser.parse_args()

    # Expansão de arquivos de entrada
    input_files = []
    for item in args.input:
        if os.path.isdir(item):
            for ext in ('*.png', '*.jpg', '*.jpeg', '*.bmp', '*.webp', '*.gif', '*.svg'):
                input_files.extend(glob.glob(os.path.join(item, ext)))
                input_files.extend(glob.glob(os.path.join(item, ext.upper())))
        elif '*' in item or '?' in item:
            input_files.extend(glob.glob(item))
        else:
            input_files.append(item)

    input_files = sorted(list(set(input_files)))
    if not input_files:
        print("Erro: Nenhum arquivo de imagem encontrado para conversão.", file=sys.stderr)
        sys.exit(1)

    # Determinação do destino
    is_multi = len(input_files) > 1
    out_dir = None
    if is_multi:
        out_dir = args.output or "."
        os.makedirs(out_dir, exist_ok=True)
    elif args.output and (os.path.isdir(args.output) or not args.output.lower().endswith('.bmp')):
        out_dir = args.output
        os.makedirs(out_dir, exist_ok=True)

    print(f"=== Guará 1-Bit BMP Builder ===")
    print(f"Total de imagens: {len(input_files)}")
    if args.preset:
        print(f"Preset ativo: {args.preset} ({PRESETS[args.preset][0]}x{PRESETS[args.preset][1]})")
    print(f"Método: {args.method} (Threshold: {args.threshold}, Invert: {args.invert})\n")

    success_count = 0
    for idx, in_file in enumerate(input_files, 1):
        if out_dir:
            base_name = os.path.splitext(os.path.basename(in_file))[0]
            out_file = os.path.join(out_dir, f"{base_name}.bmp")
        else:
            out_file = args.output or (os.path.splitext(in_file)[0] + "_1bit.bmp")

        try:
            res = convert_image(
                input_path=in_file,
                output_path=out_file,
                preset=args.preset,
                width=args.width,
                height=args.height,
                fit=args.fit,
                interpolation=args.resample,
                method=args.method,
                threshold=args.threshold,
                invert=args.invert,
                brightness=args.brightness,
                contrast=args.contrast,
                alpha_mode=args.alpha_mode,
                alpha_threshold=args.alpha_threshold,
                gen_c_array=args.c_array,
                preview=args.preview
            )
            w, h = res['target_size']
            sz = res['bytes_len']
            print(f"[{idx}/{len(input_files)}] OK: {in_file} ({res['orig_size'][0]}x{res['orig_size'][1]}) -> {out_file} ({w}x{h}, {sz} bytes)")
            success_count += 1
        except Exception as e:
            print(f"[{idx}/{len(input_files)}] ERRO ao processar {in_file}: {e}", file=sys.stderr)

    print(f"\nConcluído com sucesso: {success_count}/{len(input_files)} arquivo(s).")

if __name__ == "__main__":
    main()
