#include "arquivos.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "gfx_bmp.h"

bool arquivos_tem_extensao(const char *nome, const char *ext)
{
    size_t n = strlen(nome), e = strlen(ext);
    return n >= e && strcasecmp(nome + n - e, ext) == 0;
}

static int comparar(const void *a, const void *b)
{
    const entrada_t *x = a, *y = b;
    if (x->pasta != y->pasta) {
        return x->pasta ? -1 : 1;
    }
    return strcasecmp(x->nome, y->nome);
}

lista_arquivos_t *arquivos_listar(const char *pasta, const char *const *extensoes, bool incluir_pastas, int maximo)
{
    lista_arquivos_t *l = calloc(1, sizeof(lista_arquivos_t) + sizeof(entrada_t) * (size_t)maximo);
    if (!l) {
        return NULL;
    }
    DIR *d = opendir(pasta);
    if (!d) {
        return l;
    }
    struct dirent *de;
    char caminho[160];
    while ((de = readdir(d)) != NULL && l->quantidade < maximo) {
        if (de->d_name[0] == '.') {
            continue;
        }
        snprintf(caminho, sizeof(caminho), "%s/%s", pasta, de->d_name);
        struct stat st;
        if (stat(caminho, &st) != 0) {
            continue;
        }
        bool eh_pasta = S_ISDIR(st.st_mode);
        if (eh_pasta && !incluir_pastas) {
            continue;
        }
        if (!eh_pasta && extensoes) {
            bool ok = false;
            for (int i = 0; extensoes[i]; i++) {
                ok |= arquivos_tem_extensao(de->d_name, extensoes[i]);
            }
            if (!ok) {
                continue;
            }
        }
        entrada_t *e = &l->itens[l->quantidade++];
        snprintf(e->nome, sizeof(e->nome), "%s", de->d_name);
        e->pasta = eh_pasta;
        e->tamanho = eh_pasta ? 0 : (uint32_t)st.st_size;
    }
    closedir(d);
    qsort(l->itens, (size_t)l->quantidade, sizeof(entrada_t), comparar);
    return l;
}

const char *arquivos_icone(const entrada_t *e)
{
    if (e->pasta) {
        return "folder";
    }
    const char *n = e->nome;
    if (arquivos_tem_extensao(n, ".bmp")) return "images";
    if (arquivos_tem_extensao(n, ".mid") || arquivos_tem_extensao(n, ".midi")) return "tape";
    if (arquivos_tem_extensao(n, ".lua")) return "moon_face";
    if (arquivos_tem_extensao(n, ".sav") || arquivos_tem_extensao(n, ".dat") || arquivos_tem_extensao(n, ".idx"))
        return "save";
    if (arquivos_tem_extensao(n, ".txt")) return "txt";
    if (arquivos_tem_extensao(n, ".ini") || arquivos_tem_extensao(n, ".cfg")) return "gear";
    return "file";
}

void arquivos_formatar_tamanho(uint64_t b, char *buf, int tam)
{
    if (b < 1000) {
        snprintf(buf, (size_t)tam, "%uB", (unsigned)b);
    } else if (b < 1000000) {
        snprintf(buf, (size_t)tam, "%uK", (unsigned)(b / 1024 ? b / 1024 : 1));
    } else if (b < 1000000000ull) {
        snprintf(buf, (size_t)tam, "%uM", (unsigned)(b / (1024 * 1024)));
    } else {
        snprintf(buf, (size_t)tam, "%uG", (unsigned)(b / (1024ull * 1024 * 1024)));
    }
}

void arquivos_nome_amigavel(const char *nome, char *buf, int tam)
{
    snprintf(buf, (size_t)tam, "%s", nome);
    char *ponto = strrchr(buf, '.');
    if (ponto && ponto != buf) {
        *ponto = '\0';
    }
    for (char *p = buf; *p; p++) {
        if (*p == '_') {
            *p = ' ';
        }
    }
}

uint8_t *arquivos_ler(const char *caminho, size_t *tamanho, size_t maximo)
{
    FILE *f = fopen(caminho, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || (size_t)n > maximo) {
        fclose(f);
        return NULL;
    }
    uint8_t *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf) {
        buf[n] = '\0';
        *tamanho = (size_t)n;
    }
    return buf;
}

bool arquivos_carregar_bmp(const char *caminho, gfx_bitmap_t *bmp)
{
    size_t tam;
    uint8_t *arq = arquivos_ler(caminho, &tam, 64 * 1024);
    if (!arq) {
        return false;
    }
    int w, h;
    bool ok = gfx_bmp_info(arq, tam, &w, &h) == 0;
    uint8_t *dados = NULL;
    if (ok) {
        size_t n = (size_t)((w + 7) / 8) * (size_t)h;
        dados = malloc(n);
        ok = dados && gfx_bmp_decodificar(arq, tam, dados, n) == 0;
    }
    free(arq);
    if (!ok) {
        free(dados);
        return false;
    }
    bmp->largura = (uint16_t)w;
    bmp->altura = (uint16_t)h;
    bmp->dados = dados;
    return true;
}

void arquivos_liberar_bmp(gfx_bitmap_t *bmp)
{
    free((void *)bmp->dados);
    bmp->dados = NULL;
}
