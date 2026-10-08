# BMP Builder — Conversor de Imagens Monocromáticas (1-Bit) para o Guará

Ferramenta desenvolvida especificamente para converter imagens de formatos comuns (**PNG**, **JPG/JPEG**, **WebP**, **GIF**, **SVG**, **BMP 24/32 bits**) para o padrão **BMP monocromático de 1-bit** (1 bpp) canônico do ecossistema Guará.

O arquivo gerado segue rigorosamente o padrão Windows DIB / BMP (paleta de 2 cores com índice 0 = preto e índice 1 = branco, alinhamento DWORD de 32 bits por scanline e varredura bottom-up), 100% compatível com a ROM e o decodificador do display OLED SSD1306 / SH1106 do console.

---

## 🚀 Funcionalidades

- **Formatos de Entrada Suportados:** PNG, JPG, JPEG, WebP, GIF, SVG, BMP (todas as profundidades), TIFF e ICO.
- **Presets de Resolução do Console:**
  - `--preset icon` : **16x16** (Ícones do Catálogo de Comidas, Poções e Habilidades)
  - `--preset sprite` : **32x32** (Poses do Guará, Pets e NPCs)
  - `--preset mannequin` : **32x56** (Manequim de Equipamentos)
  - `--preset boss` : **64x64** (Sprites gigantes e chefes de batalha)
  - `--preset screen` : **128x64** (Telas completas do display OLED)
- **Algoritmos de Binarização e Dithering:**
  - `threshold` : Limiar de luminância direto (0..255, configurável com `-t`)
  - `otsu` : Determinação automática do limiar ideal por análise de histograma bimodal
  - `floyd-steinberg` : Difusão de erro clássica (ótimo para fotos e gradientes suaves)
  - `atkinson` : Dithering retrô clássico (preserva alto contraste e nitidez em displays OLED de 1-bit)
  - `bayer` : Dithering ordenado matricial 4x4 (estilo reticulado / Game Boy)
- **Ajustes:**
  - `-i, --invert` : Inversão de cores (preto vira branco e vice-versa)
  - `--contrast` : Ajuste fino de contraste (0.1 a 3.0)
  - `--brightness` : Ajuste de brilho (-100 a +100)
  - `--resample {nearest,bilinear}` : Interpolação Nearest Neighbor (essencial para pixel art nítido) ou Bilinear
  - `--fit {fit,stretch,crop}` : Enquadramento preservando proporção ou preenchimento total
  - `--alpha-mode {black,white,threshold}` : Controle de transparência para imagens com canal alfa
- **Preview em ASCII/Unicode no Terminal (`-p, --preview`):** Desenha a imagem resultante diretamente no terminal com contagem de pixels ativos e percentual de preenchimento.
- **Exportação para C Header (`-c, --c-array`):** Gera também o arquivo `.h` contendo o array estático em C pronto para colar no firmware.
- **Processamento em Lote (Batch):** Aceita pastas inteiras ou padrões com wildcards (ex: `*.png`).

---

## 🛠️ Como Usar

A ferramenta pode ser executada diretamente via Python (sem compilação) ou usando a versão compilada em C++.

### Opção 1: Via Python CLI (Recomendado)

O script `bmp_builder.py` é executável:

```bash
# Converter um ícone para 16x16 com detecção automática de limiar (Otsu) e preview
./tools/bmp-builder/bmp_builder.py item.png -o icones/item.bmp --preset icon --method otsu -p

# Converter sprite de pet 32x32 preservando pixels nítidos (Nearest Neighbor)
./tools/bmp-builder/bmp_builder.py pet.png -o pet.bmp --preset sprite --resample nearest

# Converter imagem complexa com Dithering Atkinson para tela OLED (128x64)
./tools/bmp-builder/bmp_builder.py tela.png -o tela.bmp --preset screen --method atkinson -p

# Converter em lote todos os PNGs de uma pasta para ícones 16x16
./tools/bmp-builder/bmp_builder.py "imagens/*.png" -o icones_1bit/ --preset icon

# Inverter cores e exportar código C (.h) além do BMP
./tools/bmp-builder/bmp_builder.py logo.png -o logo.bmp -w 32 -H 32 --invert -c
```

### Opção 2: Versão Nativa Compilada (C++)

Se preferir o executável nativo em C++:

```bash
cd tools/bmp-builder
make
./bmp-builder-native imagem.png -o saida.bmp --preset sprite -p
```

---

## 📋 Tabela de Presets

| Preset | Dimensões | Uso no Guará | Tamanho do BMP 1-Bit |
|---|---|---|---|
| `icon` | 16 x 16 | Ícones do catálogo de comidas, poções e habilidades | 126 bytes |
| `sprite` | 32 x 32 | Sprites de poses normais do Guará e espécies modulares | 190 bytes |
| `mannequin` | 32 x 56 | Manequim corporal do menu de Equipamentos | 286 bytes |
| `boss` | 64 x 64 | Formas avançadas de bosses e pets grandes | 574 bytes |
| `screen` | 128 x 64 | Imagem de splash / tela cheia do display OLED | 1.086 bytes |

---

## 💡 Dicas para Telas OLED Monocromáticas

1. **Pixel Art Original:** Para sprites e ícones já desenhados em pixel art, utilize sempre `--resample nearest` e `--method threshold` (ou `--method otsu`) para evitar que a interpolação borre as bordas.
2. **Ilustrações e Fotos:** Utilize `--method atkinson` ou `--method floyd-steinberg`. O método Atkinson tende a gerar menos ruído solto e bordas mais definidas no painel SSD1306 de 0,96" / 1,3".
3. **Fundo Transparente:** Por padrão (`--alpha-mode black`), áreas transparentes viram pixels pretos (apagados no display OLED), o que é ideal para a maioria dos sprites. Se a arte exigir o inverso, utilize `--alpha-mode white` ou `--invert`.
