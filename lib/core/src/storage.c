/*
 * Leitura da imagem da partição 'storage' (VROM e catálogo) gerada pelo
 * storage-packer. A imagem fica mapeada na memória e não é copiada.
 */
#include "storage.h"

#include <string.h>

#include "crc32.h"

#define VERSION_STORAGE 2
#define SIZE_ENTRY_VROM 44
#define SIZE_NAME_VROM 32
#define SIZE_RECORD 176

static struct {
    const uint8_t *image;
    size_t size;
    const uint8_t *position;
    const uint8_t *vrom_data;
    uint32_t vrom_entry_count;
    uint32_t vrom_data_size;
    const uint8_t *records[COUNT_CATALOGS];
    uint16_t amount[COUNT_CATALOGS];
} st;

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

/* Valida o cabeçalho e localiza a VROM e o catálogo na imagem. */
storage_status_t storage_open(const uint8_t *image, size_t size)
{
    memset(&st, 0, sizeof(st));
    if (size < 64 || memcmp(image, "GSTR", 4) != 0) {
        return STORAGE_ERROR_MAGIC;
    }
    if (le16(image + 4) != VERSION_STORAGE) {
        return STORAGE_ERROR_VERSION;
    }
    if (guara_crc32(0, image, 60) != le32(image + 60)) {
        return STORAGE_ERROR_CRC;
    }
    uint32_t off_vrom = le32(image + 8), vrom_size = le32(image + 12);
    uint32_t off_cat = le32(image + 16), catalog_size = le32(image + 20);
    if ((uint64_t)off_vrom + vrom_size > size || (uint64_t)off_cat + catalog_size > size || vrom_size < 16 || catalog_size < 16) {
        return STORAGE_ERROR_BOUNDS;
    }

    const uint8_t *v = image + off_vrom;
    if (memcmp(v, "GVRO", 4) != 0 || le32(v + 8) != SIZE_ENTRY_VROM) {
        return STORAGE_ERROR_MAGIC;
    }
    st.vrom_entry_count = le32(v + 4);
    st.vrom_data_size = le32(v + 12);
    st.position = v + 16;
    st.vrom_data = st.position + st.vrom_entry_count * SIZE_ENTRY_VROM;
    if (16 + (uint64_t)st.vrom_entry_count * SIZE_ENTRY_VROM + st.vrom_data_size > vrom_size) {
        return STORAGE_ERROR_BOUNDS;
    }

    const uint8_t *c = image + off_cat;
    if (memcmp(c, "GCAT", 4) != 0 || le16(c + 6) != SIZE_RECORD) {
        return STORAGE_ERROR_MAGIC;
    }
    const uint8_t *reg = c + 16;
    uint32_t total = 0;
    for (int i = 0; i < COUNT_CATALOGS; i++) {
        st.amount[i] = le16(c + 8 + 2 * i);
        st.records[i] = reg + total * SIZE_RECORD;
        total += st.amount[i];
    }
    if (16 + (uint64_t)total * SIZE_RECORD > catalog_size) {
        return STORAGE_ERROR_BOUNDS;
    }

    st.image = image;
    st.size = size;
    return STORAGE_OK;
}

/* Verdadeiro se uma imagem válida foi aberta. */
bool storage_is_open(void)
{
    return st.image != NULL;
}

/* Quantidade de bitmaps na VROM. */
int vrom_count(void)
{
    return (int)st.vrom_entry_count;
}

/* Busca binária de um bitmap pelo nome no índice ordenado da VROM. */
bool vrom_lookup(const char *name, gfx_bitmap_t *output)
{
    if (!st.image || !name) {
        return false;
    }
    int ini = 0, end = (int)st.vrom_entry_count - 1;
    while (ini <= end) {
        int mid = (ini + end) / 2;
        const uint8_t *e = st.position + mid * SIZE_ENTRY_VROM;
        int cmp = strncmp(name, (const char *)e, SIZE_NAME_VROM);
        if (cmp == 0) {
            uint32_t off = le32(e + 36), length = le32(e + 40);
            if ((uint64_t)off + length > st.vrom_data_size) {
                return false;
            }
            output->width = le16(e + 32);
            output->height = le16(e + 34);
            output->data = st.vrom_data + off;
            return true;
        }
        if (cmp > 0) {
            ini = mid + 1;
        } else {
            end = mid - 1;
        }
    }
    return false;
}

/* Atalho para desenho imediato: devolve o bitmap num cache circular de 16 posições. */
const gfx_bitmap_t *vrom(const char *name)
{
    /* Pequeno cache circular para que vários ponteiros possam coexistir num mesmo quadro. */
    static gfx_bitmap_t cache[16];
    static unsigned next_entry;
    gfx_bitmap_t *b = &cache[next_entry % 16];
    if (!vrom_lookup(name, b)) {
        return NULL;
    }
    next_entry++;
    return b;
}

/* Quantidade de registros de um catálogo. */
int catalog_record_count(catalog_t cat)
{
    return cat < COUNT_CATALOGS ? st.amount[cat] : 0;
}

/* Decodifica o registro `id` do catálogo (as strings apontam para a imagem). */
bool catalog_read(catalog_t cat, int id, catalog_record_t *r)
{
    if (cat >= COUNT_CATALOGS || id < 0 || id >= st.amount[cat]) {
        return false;
    }
    const uint8_t *p = st.records[cat] + id * SIZE_RECORD;
    r->id = p[0];
    r->type = p[1];
    r->nature = p[2];
    r->target = p[3];
    r->power = p[4];
    r->mp_cost = p[5];
    r->n_effects = p[6] > 4 ? 4 : p[6];
    r->n_status = p[7] > 2 ? 2 : p[7];
    for (int i = 0; i < 4; i++) {
        r->effects[i].target = p[8 + i * 4];
        r->effects[i].value = (int16_t)le16(p + 10 + i * 4);
    }
    for (int i = 0; i < 2; i++) {
        r->status[i].state = p[24 + i * 2];
        r->status[i].value = (int8_t)p[25 + i * 2];
    }
    r->slug = (const char *)p + 32;
    r->icon = (const char *)p + 48;
    r->name = (const char *)p + 80;
    r->description = (const char *)p + 112;
    return true;
}

/* Busca linear de um registro pelo slug. Retorna o id ou -1. */
int catalog_find_slug(catalog_t cat, const char *slug)
{
    catalog_record_t r;
    for (int i = 0; i < catalog_record_count(cat); i++) {
        if (catalog_read(cat, i, &r) && strcmp(r.slug, slug) == 0) {
            return i;
        }
    }
    return -1;
}
