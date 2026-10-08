#include "storage.h"

#include <string.h>

#include "crc32.h"

#define VERSAO_STORAGE 2
#define TAM_ENTRADA_VROM 44
#define TAM_NOME_VROM 32
#define TAM_REGISTRO 176

static struct {
    const uint8_t *imagem;
    size_t tamanho;
    const uint8_t *indice;
    const uint8_t *dados_vrom;
    uint32_t qtd_vrom;
    uint32_t tam_dados_vrom;
    const uint8_t *registros[QTD_CATALOGOS];
    uint16_t qtd[QTD_CATALOGOS];
} st;

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

storage_status_t storage_abrir(const uint8_t *imagem, size_t tamanho)
{
    memset(&st, 0, sizeof(st));
    if (tamanho < 64 || memcmp(imagem, "GSTR", 4) != 0) {
        return STORAGE_ERRO_MAGIC;
    }
    if (le16(imagem + 4) != VERSAO_STORAGE) {
        return STORAGE_ERRO_VERSAO;
    }
    if (guara_crc32(0, imagem, 60) != le32(imagem + 60)) {
        return STORAGE_ERRO_CRC;
    }
    uint32_t off_vrom = le32(imagem + 8), tam_vrom = le32(imagem + 12);
    uint32_t off_cat = le32(imagem + 16), tam_cat = le32(imagem + 20);
    if ((uint64_t)off_vrom + tam_vrom > tamanho || (uint64_t)off_cat + tam_cat > tamanho || tam_vrom < 16 || tam_cat < 16) {
        return STORAGE_ERRO_LIMITES;
    }

    const uint8_t *v = imagem + off_vrom;
    if (memcmp(v, "GVRO", 4) != 0 || le32(v + 8) != TAM_ENTRADA_VROM) {
        return STORAGE_ERRO_MAGIC;
    }
    st.qtd_vrom = le32(v + 4);
    st.tam_dados_vrom = le32(v + 12);
    st.indice = v + 16;
    st.dados_vrom = st.indice + st.qtd_vrom * TAM_ENTRADA_VROM;
    if (16 + (uint64_t)st.qtd_vrom * TAM_ENTRADA_VROM + st.tam_dados_vrom > tam_vrom) {
        return STORAGE_ERRO_LIMITES;
    }

    const uint8_t *c = imagem + off_cat;
    if (memcmp(c, "GCAT", 4) != 0 || le16(c + 6) != TAM_REGISTRO) {
        return STORAGE_ERRO_MAGIC;
    }
    const uint8_t *reg = c + 16;
    uint32_t total = 0;
    for (int i = 0; i < QTD_CATALOGOS; i++) {
        st.qtd[i] = le16(c + 8 + 2 * i);
        st.registros[i] = reg + total * TAM_REGISTRO;
        total += st.qtd[i];
    }
    if (16 + (uint64_t)total * TAM_REGISTRO > tam_cat) {
        return STORAGE_ERRO_LIMITES;
    }

    st.imagem = imagem;
    st.tamanho = tamanho;
    return STORAGE_OK;
}

bool storage_aberto(void)
{
    return st.imagem != NULL;
}

int vrom_quantidade(void)
{
    return (int)st.qtd_vrom;
}

bool vrom_buscar(const char *nome, gfx_bitmap_t *saida)
{
    if (!st.imagem || !nome) {
        return false;
    }
    int ini = 0, fim = (int)st.qtd_vrom - 1;
    while (ini <= fim) {
        int meio = (ini + fim) / 2;
        const uint8_t *e = st.indice + meio * TAM_ENTRADA_VROM;
        int cmp = strncmp(nome, (const char *)e, TAM_NOME_VROM);
        if (cmp == 0) {
            uint32_t off = le32(e + 36), tam = le32(e + 40);
            if ((uint64_t)off + tam > st.tam_dados_vrom) {
                return false;
            }
            saida->largura = le16(e + 32);
            saida->altura = le16(e + 34);
            saida->dados = st.dados_vrom + off;
            return true;
        }
        if (cmp > 0) {
            ini = meio + 1;
        } else {
            fim = meio - 1;
        }
    }
    return false;
}

const gfx_bitmap_t *vrom(const char *nome)
{
    /* Pequeno cache circular para que vários ponteiros possam coexistir num mesmo quadro. */
    static gfx_bitmap_t cache[16];
    static unsigned prox;
    gfx_bitmap_t *b = &cache[prox % 16];
    if (!vrom_buscar(nome, b)) {
        return NULL;
    }
    prox++;
    return b;
}

int catalogo_quantidade(catalogo_t cat)
{
    return cat < QTD_CATALOGOS ? st.qtd[cat] : 0;
}

bool catalogo_ler(catalogo_t cat, int id, registro_t *r)
{
    if (cat >= QTD_CATALOGOS || id < 0 || id >= st.qtd[cat]) {
        return false;
    }
    const uint8_t *p = st.registros[cat] + id * TAM_REGISTRO;
    r->id = p[0];
    r->tipo = p[1];
    r->natureza = p[2];
    r->alvo = p[3];
    r->poder = p[4];
    r->custo_mp = p[5];
    r->n_efeitos = p[6] > 4 ? 4 : p[6];
    r->n_sobre = p[7] > 2 ? 2 : p[7];
    for (int i = 0; i < 4; i++) {
        r->efeitos[i].alvo = p[8 + i * 4];
        r->efeitos[i].valor = (int16_t)le16(p + 10 + i * 4);
    }
    for (int i = 0; i < 2; i++) {
        r->sobre[i].estado = p[24 + i * 2];
        r->sobre[i].valor = (int8_t)p[25 + i * 2];
    }
    r->slug = (const char *)p + 32;
    r->icone = (const char *)p + 48;
    r->nome = (const char *)p + 80;
    r->descricao = (const char *)p + 112;
    return true;
}

int catalogo_buscar_slug(catalogo_t cat, const char *slug)
{
    registro_t r;
    for (int i = 0; i < catalogo_quantidade(cat); i++) {
        if (catalogo_ler(cat, i, &r) && strcmp(r.slug, slug) == 0) {
            return i;
        }
    }
    return -1;
}
