/**
 * @file main.cpp
 * @brief Conversor de imagens para BMP 1-bit em C++ (CLI nativa) para o Guará
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <gdk-pixbuf/gdk-pixbuf.h>

struct ConvertConfig {
    std::string input_path;
    std::string output_path;
    int target_width = 0;
    int target_height = 0;
    int threshold = 128;
    bool invert = false;
    bool preview = false;
    bool c_array = false;
    std::string method = "threshold"; // threshold, otsu, floyd-steinberg, atkinson
    std::string fit = "fit";          // stretch, fit
    std::string resample = "nearest"; // nearest, bilinear
};

static void print_help(const char* prog) {
    std::cout << "Uso: " << prog << " <imagem_entrada> -o <saida.bmp> [opções]\n\n"
              << "Opções:\n"
              << "  -o <arquivo>       Arquivo de saída .bmp\n"
              << "  -w <largura>       Largura desejada em pixels\n"
              << "  -H <altura>        Altura desejada em pixels\n"
              << "  --preset <nome>    Preset Guará (icon=16x16, sprite=32x32, mannequin=32x56, boss=64x64, screen=128x64)\n"
              << "  -m <método>        Método (threshold, otsu, floyd-steinberg, atkinson)\n"
              << "  -t <limiar>        Limiar de corte 0..255 (padrão: 128)\n"
              << "  -i                 Inverter cores (preto vira branco)\n"
              << "  -p                 Exibir preview no terminal\n"
              << "  -c                 Exportar também arquivo C Header (.h)\n"
              << "  -h, --help         Exibir esta ajuda\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }

    ConvertConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "-o" && i + 1 < argc) {
            cfg.output_path = argv[++i];
        } else if (arg == "-w" && i + 1 < argc) {
            cfg.target_width = std::stoi(argv[++i]);
        } else if (arg == "-H" && i + 1 < argc) {
            cfg.target_height = std::stoi(argv[++i]);
        } else if (arg == "--preset" && i + 1 < argc) {
            std::string p = argv[++i];
            if (p == "icon") { cfg.target_width = 16; cfg.target_height = 16; }
            else if (p == "sprite") { cfg.target_width = 32; cfg.target_height = 32; }
            else if (p == "mannequin") { cfg.target_width = 32; cfg.target_height = 56; }
            else if (p == "boss") { cfg.target_width = 64; cfg.target_height = 64; }
            else if (p == "screen") { cfg.target_width = 128; cfg.target_height = 64; }
        } else if (arg == "-m" && i + 1 < argc) {
            cfg.method = argv[++i];
        } else if (arg == "-t" && i + 1 < argc) {
            cfg.threshold = std::stoi(argv[++i]);
        } else if (arg == "-i") {
            cfg.invert = true;
        } else if (arg == "-p") {
            cfg.preview = true;
        } else if (arg == "-c") {
            cfg.c_array = true;
        } else if (arg[0] != '-') {
            cfg.input_path = arg;
        }
    }

    if (cfg.input_path.empty()) {
        std::cerr << "Erro: Arquivo de entrada não especificado.\n";
        return 1;
    }

    GError* error = nullptr;
    GdkPixbuf* src = gdk_pixbuf_new_from_file(cfg.input_path.c_str(), &error);
    if (!src) {
        std::cerr << "Erro ao carregar imagem: " << (error ? error->message : "desconhecido") << "\n";
        if (error) g_error_free(error);
        return 1;
    }

    int orig_w = gdk_pixbuf_get_width(src);
    int orig_h = gdk_pixbuf_get_height(src);
    int target_w = cfg.target_width > 0 ? cfg.target_width : orig_w;
    int target_h = cfg.target_height > 0 ? cfg.target_height : orig_h;

    // Escala
    GdkInterpType interp = (cfg.resample == "bilinear") ? GDK_INTERP_BILINEAR : GDK_INTERP_NEAREST;
    GdkPixbuf* scaled = nullptr;

    if (cfg.fit == "fit" && (target_w != orig_w || target_h != orig_h)) {
        double scale = std::min((double)target_w / orig_w, (double)target_h / orig_h);
        int sw = std::max(1, (int)std::round(orig_w * scale));
        int sh = std::max(1, (int)std::round(orig_h * scale));
        GdkPixbuf* temp = gdk_pixbuf_scale_simple(src, sw, sh, interp);

        scaled = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, target_w, target_h);
        gdk_pixbuf_fill(scaled, 0x00000000); // fundo preto transparente
        int ox = (target_w - sw) / 2;
        int oy = (target_h - sh) / 2;
        gdk_pixbuf_copy_area(temp, 0, 0, sw, sh, scaled, ox, oy);
        g_object_unref(temp);
    } else {
        scaled = gdk_pixbuf_scale_simple(src, target_w, target_h, interp);
    }
    g_object_unref(src);

    int channels = gdk_pixbuf_get_n_channels(scaled);
    int rowstride = gdk_pixbuf_get_rowstride(scaled);
    const guchar* pixels = gdk_pixbuf_get_pixels(scaled);

    std::vector<float> lum_map(target_w * target_h);
    for (int y = 0; y < target_h; ++y) {
        const guchar* row = pixels + y * rowstride;
        for (int x = 0; x < target_w; ++x) {
            const guchar* p = row + x * channels;
            float r = p[0], g = p[1], b = p[2];
            float a = (channels >= 4) ? (p[3] / 255.0f) : 1.0f;
            float lum = (0.299f * r + 0.587f * g + 0.114f * b) * a;
            lum_map[y * target_w + x] = std::max(0.0f, std::min(255.0f, lum));
        }
    }
    g_object_unref(scaled);

    // Binarização
    std::vector<bool> bit_pixels(target_w * target_h, false);
    int thresh = cfg.threshold;

    if (cfg.method == "otsu") {
        std::vector<int> hist(256, 0);
        for (float v : lum_map) hist[std::max(0, std::min(255, (int)std::round(v)))]++;
        int total = target_w * target_h;
        double sum_all = 0;
        for (int i = 0; i < 256; ++i) sum_all += i * hist[i];
        double sum_b = 0, w_b = 0, max_var = 0;
        for (int t = 0; t < 256; ++t) {
            w_b += hist[t];
            if (w_b == 0) continue;
            double w_f = total - w_b;
            if (w_f == 0) break;
            sum_b += t * hist[t];
            double m_b = sum_b / w_b;
            double m_f = (sum_all - sum_b) / w_f;
            double var = w_b * w_f * (m_b - m_f) * (m_b - m_f);
            if (var > max_var) { max_var = var; thresh = t; }
        }
    }

    if (cfg.method == "floyd-steinberg") {
        std::vector<float> work = lum_map;
        for (int y = 0; y < target_h; ++y) {
            for (int x = 0; x < target_w; ++x) {
                int idx = y * target_w + x;
                float old_v = work[idx];
                float new_v = (old_v >= thresh) ? 255.0f : 0.0f;
                float err = old_v - new_v;
                bool is_white = (new_v == 255.0f);
                bit_pixels[idx] = cfg.invert ? !is_white : is_white;

                if (x + 1 < target_w) work[idx + 1] += err * (7.0f / 16.0f);
                if (x - 1 >= 0 && y + 1 < target_h) work[(y + 1) * target_w + (x - 1)] += err * (3.0f / 16.0f);
                if (y + 1 < target_h) work[(y + 1) * target_w + x] += err * (5.0f / 16.0f);
                if (x + 1 < target_w && y + 1 < target_h) work[(y + 1) * target_w + (x + 1)] += err * (1.0f / 16.0f);
            }
        }
    } else if (cfg.method == "atkinson") {
        std::vector<float> work = lum_map;
        for (int y = 0; y < target_h; ++y) {
            for (int x = 0; x < target_w; ++x) {
                int idx = y * target_w + x;
                float old_v = work[idx];
                float new_v = (old_v >= thresh) ? 255.0f : 0.0f;
                float err = old_v - new_v;
                float disp = err / 8.0f;
                bool is_white = (new_v == 255.0f);
                bit_pixels[idx] = cfg.invert ? !is_white : is_white;

                if (x + 1 < target_w) work[idx + 1] += disp;
                if (x + 2 < target_w) work[idx + 2] += disp;
                if (x - 1 >= 0 && y + 1 < target_h) work[(y + 1) * target_w + (x - 1)] += disp;
                if (y + 1 < target_h) work[(y + 1) * target_w + x] += disp;
                if (x + 1 < target_w && y + 1 < target_h) work[(y + 1) * target_w + (x + 1)] += disp;
                if (y + 2 < target_h) work[(y + 2) * target_w + x] += disp;
            }
        }
    } else { // threshold / otsu
        for (size_t i = 0; i < lum_map.size(); ++i) {
            bool is_white = (lum_map[i] >= thresh);
            bit_pixels[i] = cfg.invert ? !is_white : is_white;
        }
    }

    // Geração do BMP 1-Bit
    int row_bytes = ((target_w + 31) / 32) * 4;
    uint32_t image_size = row_bytes * target_h;
    uint32_t offset = 14 + 40 + 8;
    uint32_t file_size = offset + image_size;

    std::vector<uint8_t> bmp(file_size, 0);
    // Header
    bmp[0] = 'B'; bmp[1] = 'M';
    std::memcpy(&bmp[2], &file_size, 4);
    std::memcpy(&bmp[10], &offset, 4);

    // DIB
    uint32_t dib_size = 40;
    std::memcpy(&bmp[14], &dib_size, 4);
    std::memcpy(&bmp[18], &target_w, 4);
    std::memcpy(&bmp[22], &target_h, 4);
    uint16_t planes = 1, bpp = 1;
    std::memcpy(&bmp[26], &planes, 2);
    std::memcpy(&bmp[28], &bpp, 2);
    std::memcpy(&bmp[34], &image_size, 4);
    uint32_t clr = 2;
    std::memcpy(&bmp[46], &clr, 4);
    std::memcpy(&bmp[50], &clr, 4);

    // Paleta: index 0 = preto (0,0,0,0), index 1 = branco (255,255,255,0)
    bmp[54 + 4] = 255; bmp[54 + 5] = 255; bmp[54 + 6] = 255; bmp[54 + 7] = 0;

    // Scanlines bottom-up
    for (int y = 0; y < target_h; ++y) {
        int src_y = target_h - 1 - y;
        int row_offset = offset + y * row_bytes;
        for (int x = 0; x < target_w; ++x) {
            if (bit_pixels[src_y * target_w + x]) {
                int byte_pos = row_offset + (x / 8);
                int bit_pos = 7 - (x % 8);
                bmp[byte_pos] |= (1 << bit_pos);
            }
        }
    }

    if (!cfg.output_path.empty()) {
        std::ofstream out(cfg.output_path, std::ios::binary);
        if (out) {
            out.write(reinterpret_cast<const char*>(bmp.data()), bmp.size());
            std::cout << "Arquivo salvo com sucesso: " << cfg.output_path
                      << " (" << target_w << "x" << target_h << ", " << bmp.size() << " bytes)\n";
        } else {
            std::cerr << "Erro ao gravar arquivo de saída: " << cfg.output_path << "\n";
            return 1;
        }
    }

    if (cfg.preview) {
        std::cout << "\n--- PREVIEW (" << target_w << "x" << target_h << ") ---\n";
        for (int y = 0; y < target_h; y += 2) {
            for (int x = 0; x < target_w; ++x) {
                bool top = bit_pixels[y * target_w + x];
                bool bot = (y + 1 < target_h) ? bit_pixels[(y + 1) * target_w + x] : false;
                if (top && bot) std::cout << "\u2588";
                else if (top) std::cout << "\u2580";
                else if (bot) std::cout << "\u2584";
                else std::cout << " ";
            }
            std::cout << "\n";
        }
    }

    return 0;
}
