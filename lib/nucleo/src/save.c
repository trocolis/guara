#include "save.h"

#include <string.h>

#include "crc32.h"

/* Offsets dos domínios no bloco de 512 bytes. */
enum {
    OFF_HEADER = 0,       /* 16: magic[5] versao u8 flags u16 timestamp u32 contador u32 */
    OFF_IDENTIDADE = 16,  /* 20: nome[11] uid u32 cabeca cor nat_nasc natureza estagio */
    OFF_VITAIS = 36,      /* 16: hp hp_max mp mp_max energia humor alimentacao saude (u16) */
    OFF_PROGRESSO = 52,   /* 21: nivel u8 xp u32 idade_min u32 atributos[6] u16 */
    OFF_MEDIDORES = 73,   /*  8: 4 x i16 */
    OFF_AFINIDADES = 81,  /* 10: 5 x u16 */
    OFF_INVENTARIO = 91,  /* 64: quantidade por item */
    OFF_EQUIPAMENTO = 155,/* 15: possui u64 + 7 slots */
    OFF_HABILIDADES = 170,/* 12: possui u64 + 4 ativas */
    OFF_ESTADOS = 182,    /*  8: estados u32 desmaio_min u16 ocioso_min u16 */
    OFF_FIM_DADOS = 190,
    OFF_CRC = SAVE_TAMANHO - 4,
};

static void p16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void p32(uint8_t *p, uint32_t v) { p16(p, (uint16_t)v); p16(p + 2, (uint16_t)(v >> 16)); }
static void p64(uint8_t *p, uint64_t v) { p32(p, (uint32_t)v); p32(p + 4, (uint32_t)(v >> 32)); }
static uint16_t l16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t l32(const uint8_t *p) { return l16(p) | ((uint32_t)l16(p + 2) << 16); }
static uint64_t l64(const uint8_t *p) { return l32(p) | ((uint64_t)l32(p + 4) << 32); }

void save_serializar(const guara_t *g, uint32_t contador, uint8_t s[SAVE_TAMANHO])
{
    memset(s, 0, SAVE_TAMANHO);
    memcpy(s + OFF_HEADER, "GUARA", 5);
    s[OFF_HEADER + 5] = SAVE_VERSAO;
    p32(s + OFF_HEADER + 8, g->timestamp);
    p32(s + OFF_HEADER + 12, contador);

    uint8_t *p = s + OFF_IDENTIDADE;
    memcpy(p, g->nome, TAM_NOME);
    p32(p + 11, g->uid);
    p[15] = g->cabeca;
    p[16] = g->cor;
    p[17] = g->natureza_nascimento;
    p[18] = g->natureza;
    p[19] = g->estagio;

    p = s + OFF_VITAIS;
    const uint16_t vitais[8] = {g->hp, g->hp_max, g->mp, g->mp_max, g->energia, g->humor, g->alimentacao, g->saude};
    for (int i = 0; i < 8; i++) {
        p16(p + 2 * i, vitais[i]);
    }

    p = s + OFF_PROGRESSO;
    p[0] = g->nivel;
    p32(p + 1, g->xp);
    p32(p + 5, g->idade_min);
    for (int i = 0; i < QTD_ATRIBUTOS; i++) {
        p16(p + 9 + 2 * i, g->atributos[i]);
    }
    for (int i = 0; i < QTD_MEDIDORES; i++) {
        p16(s + OFF_MEDIDORES + 2 * i, (uint16_t)g->medidores[i]);
    }
    for (int i = 0; i < QTD_AFINIDADES; i++) {
        p16(s + OFF_AFINIDADES + 2 * i, g->afinidades[i]);
    }
    memcpy(s + OFF_INVENTARIO, g->inventario, QTD_ITENS);
    p64(s + OFF_EQUIPAMENTO, g->equip_possui);
    memcpy(s + OFF_EQUIPAMENTO + 8, g->equipado, QTD_SLOTS_EQUIP);
    p64(s + OFF_HABILIDADES, g->hab_possui);
    memcpy(s + OFF_HABILIDADES + 8, g->hab_ativas, QTD_HAB_ATIVAS);
    p32(s + OFF_ESTADOS, g->estados);
    p16(s + OFF_ESTADOS + 4, g->desmaio_min);
    p16(s + OFF_ESTADOS + 6, g->ocioso_min);

    p32(s + OFF_CRC, guara_crc32(0, s, OFF_CRC));
}

bool save_desserializar(const uint8_t s[SAVE_TAMANHO], guara_t *g, uint32_t *contador)
{
    if (memcmp(s, "GUARA", 5) != 0 || s[5] != SAVE_VERSAO) {
        return false;
    }
    if (guara_crc32(0, s, OFF_CRC) != l32(s + OFF_CRC)) {
        return false;
    }
    memset(g, 0, sizeof(*g));
    g->timestamp = l32(s + OFF_HEADER + 8);
    if (contador) {
        *contador = l32(s + OFF_HEADER + 12);
    }

    const uint8_t *p = s + OFF_IDENTIDADE;
    memcpy(g->nome, p, TAM_NOME);
    g->nome[TAM_NOME] = '\0';
    g->uid = l32(p + 11);
    g->cabeca = p[15];
    g->cor = p[16];
    g->natureza_nascimento = p[17];
    g->natureza = p[18];
    g->estagio = p[19];

    p = s + OFF_VITAIS;
    uint16_t *vitais[8] = {&g->hp, &g->hp_max, &g->mp, &g->mp_max, &g->energia, &g->humor, &g->alimentacao, &g->saude};
    for (int i = 0; i < 8; i++) {
        *vitais[i] = l16(p + 2 * i);
    }

    p = s + OFF_PROGRESSO;
    g->nivel = p[0];
    g->xp = l32(p + 1);
    g->idade_min = l32(p + 5);
    for (int i = 0; i < QTD_ATRIBUTOS; i++) {
        g->atributos[i] = l16(p + 9 + 2 * i);
    }
    for (int i = 0; i < QTD_MEDIDORES; i++) {
        g->medidores[i] = (int16_t)l16(s + OFF_MEDIDORES + 2 * i);
    }
    for (int i = 0; i < QTD_AFINIDADES; i++) {
        g->afinidades[i] = l16(s + OFF_AFINIDADES + 2 * i);
    }
    memcpy(g->inventario, s + OFF_INVENTARIO, QTD_ITENS);
    g->equip_possui = l64(s + OFF_EQUIPAMENTO);
    memcpy(g->equipado, s + OFF_EQUIPAMENTO + 8, QTD_SLOTS_EQUIP);
    g->hab_possui = l64(s + OFF_HABILIDADES);
    memcpy(g->hab_ativas, s + OFF_HABILIDADES + 8, QTD_HAB_ATIVAS);
    g->estados = l32(s + OFF_ESTADOS);
    g->desmaio_min = l16(s + OFF_ESTADOS + 4);
    g->ocioso_min = l16(s + OFF_ESTADOS + 6);
    return true;
}

/* ------------------------------------------------------------------------- */
/* Anel                                                                      */
/* ------------------------------------------------------------------------- */

#define SLOTS_POR_SETOR (ANEL_SETOR / SAVE_TAMANHO)

void anel_iniciar(anel_t *a, const flash_ops_t *ops)
{
    a->ops = *ops;
    a->ultimo_slot = -1;
    a->contador = 0;
}

bool anel_carregar(anel_t *a, guara_t *g)
{
    uint8_t bloco[SAVE_TAMANHO];
    guara_t tmp;
    bool achou = false;
    a->ultimo_slot = -1;
    for (int i = 0; i < ANEL_SLOTS; i++) {
        uint32_t contador;
        if (!a->ops.ler(a->ops.ctx, (uint32_t)i * SAVE_TAMANHO, bloco, SAVE_TAMANHO)) {
            continue;
        }
        if (save_desserializar(bloco, &tmp, &contador) && (!achou || contador > a->contador)) {
            achou = true;
            a->contador = contador;
            a->ultimo_slot = i;
            *g = tmp;
        }
    }
    return achou;
}

static bool slot_virgem(anel_t *a, int slot)
{
    uint8_t bloco[SAVE_TAMANHO];
    if (!a->ops.ler(a->ops.ctx, (uint32_t)slot * SAVE_TAMANHO, bloco, SAVE_TAMANHO)) {
        return false;
    }
    for (int i = 0; i < SAVE_TAMANHO; i++) {
        if (bloco[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

bool anel_salvar(anel_t *a, const guara_t *g)
{
    int slot = (a->ultimo_slot + 1) % ANEL_SLOTS;
    bool inicio_setor = (slot % SLOTS_POR_SETOR) == 0;
    if (!inicio_setor && !slot_virgem(a, slot)) {
        /* Slot sujo no meio do setor (ex.: queda de energia): pula para o próximo setor. */
        slot = ((slot / SLOTS_POR_SETOR + 1) % ANEL_SETORES) * SLOTS_POR_SETOR;
        inicio_setor = true;
    }
    if (inicio_setor && !a->ops.apagar_setor(a->ops.ctx, (uint32_t)(slot / SLOTS_POR_SETOR) * ANEL_SETOR)) {
        return false;
    }
    uint8_t bloco[SAVE_TAMANHO];
    save_serializar(g, a->contador + 1, bloco);
    if (!a->ops.escrever(a->ops.ctx, (uint32_t)slot * SAVE_TAMANHO, bloco, SAVE_TAMANHO)) {
        return false;
    }
    a->contador++;
    a->ultimo_slot = slot;
    return true;
}

bool anel_apagar(anel_t *a)
{
    bool ok = true;
    for (int s = 0; s < ANEL_SETORES; s++) {
        ok &= a->ops.apagar_setor(a->ops.ctx, (uint32_t)s * ANEL_SETOR);
    }
    a->ultimo_slot = -1;
    a->contador = 0;
    return ok;
}
