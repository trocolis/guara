#include "midi.h"

#include <string.h>

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

/* Frequências (mHz) da oitava 5 do MIDI (notas 60..71: C4..B4 em Hz x1000). */
static const uint32_t OITAVA[12] = {261626, 277183, 293665, 311127, 329628, 349228,
                                    369994, 391995, 415305, 440000, 466164, 493883};

uint16_t midi_frequencia(int nota)
{
    if (nota < 0 || nota > 127) {
        return 0;
    }
    int oitava = nota / 12 - 5;
    uint32_t f = OITAVA[nota % 12];
    f = oitava >= 0 ? f << oitava : f >> -oitava;
    f /= 1000;
    if (f < 100) {
        f = 100; /* abaixo disso o piezo quase não soa */
    }
    if (f > 8000) {
        f = 8000;
    }
    return (uint16_t)f;
}

static bool ler_vlq(midi_trilha_t *t, uint32_t *v)
{
    *v = 0;
    for (int i = 0; i < 4; i++) {
        if (t->pos >= t->fim) {
            return false;
        }
        uint8_t b = *t->pos++;
        *v = (*v << 7) | (b & 0x7F);
        if (!(b & 0x80)) {
            return true;
        }
    }
    return false;
}

static void ler_delta(midi_trilha_t *t)
{
    uint32_t delta;
    if (t->acabou || !ler_vlq(t, &delta)) {
        t->acabou = true;
        return;
    }
    t->proximo_tick += delta;
}

bool midi_abrir(midi_t *m, const uint8_t *dados, size_t tamanho)
{
    memset(m, 0, sizeof(*m));
    if (tamanho < 14 || memcmp(dados, "MThd", 4) != 0 || be32(dados + 4) < 6) {
        return false;
    }
    uint16_t formato = be16(dados + 8);
    uint16_t n = be16(dados + 10);
    uint16_t divisao = be16(dados + 12);
    if (formato > 1 || n == 0) {
        return false;
    }
    if (divisao & 0x8000) {
        /* SMPTE: aproximação para ticks por segundo como "semínima de 1 s" */
        int fps = -(int8_t)(divisao >> 8);
        divisao = (uint16_t)(fps * (divisao & 0xFF));
        m->tempo_us = 1000000;
    } else {
        m->tempo_us = 500000; /* 120 BPM */
    }
    if (divisao == 0) {
        return false;
    }
    m->dados = dados;
    m->tamanho = tamanho;
    m->divisao = divisao;
    const uint8_t *p = dados + 8 + be32(dados + 4);
    const uint8_t *fim = dados + tamanho;
    while (p + 8 <= fim && m->n_trilhas < MIDI_MAX_TRILHAS) {
        uint32_t tam = be32(p + 4);
        if (memcmp(p, "MTrk", 4) == 0) {
            midi_trilha_t *t = &m->trilhas[m->n_trilhas++];
            t->ini = t->pos = p + 8;
            t->fim = (p + 8 + tam <= fim) ? p + 8 + tam : fim;
        }
        p += 8 + tam;
    }
    if (m->n_trilhas == 0) {
        return false;
    }
    midi_reiniciar(m);
    return true;
}

void midi_reiniciar(midi_t *m)
{
    for (int i = 0; i < m->n_trilhas; i++) {
        midi_trilha_t *t = &m->trilhas[i];
        t->pos = t->ini;
        t->proximo_tick = 0;
        t->status = 0;
        t->acabou = false;
        ler_delta(t);
    }
    memset(m->ativas, 0, sizeof(m->ativas));
    m->tick_atual = 0;
    m->resto_us = 0;
    m->nota_atual = -1;
    m->terminou = false;
    if (m->tempo_us != 1000000) {
        m->tempo_us = 500000;
    }
}

static int nota_mais_aguda(const midi_t *m)
{
    for (int n = 127; n >= 0; n--) {
        if (m->ativas[n]) {
            return n;
        }
    }
    return -1;
}

/* Processa o próximo evento da trilha (já no tick dela). */
static void processar_evento(midi_t *m, midi_trilha_t *t)
{
    if (t->pos >= t->fim) {
        t->acabou = true;
        return;
    }
    uint8_t b = *t->pos;
    if (b & 0x80) {
        t->pos++;
        if (b < 0xF0) {
            t->status = b;
        }
    } else {
        b = t->status; /* running status */
    }
    if (b == 0xFF) {
        uint8_t tipo = t->pos < t->fim ? *t->pos++ : 0;
        uint32_t tam;
        if (!ler_vlq(t, &tam) || t->pos + tam > t->fim) {
            t->acabou = true;
            return;
        }
        if (tipo == 0x51 && tam == 3) {
            m->tempo_us = ((uint32_t)t->pos[0] << 16) | (t->pos[1] << 8) | t->pos[2];
        } else if (tipo == 0x2F) {
            t->acabou = true;
        }
        t->pos += tam;
    } else if (b == 0xF0 || b == 0xF7) {
        uint32_t tam;
        if (!ler_vlq(t, &tam) || t->pos + tam > t->fim) {
            t->acabou = true;
            return;
        }
        t->pos += tam;
    } else {
        uint8_t tipo = b & 0xF0, canal = b & 0x0F;
        int n_dados = (tipo == 0xC0 || tipo == 0xD0) ? 1 : 2;
        if (t->pos + n_dados > t->fim) {
            t->acabou = true;
            return;
        }
        uint8_t d1 = t->pos[0], d2 = n_dados > 1 ? t->pos[1] : 0;
        t->pos += n_dados;
        if (canal != 9 && d1 < 128) {
            if (tipo == 0x90 && d2 > 0) {
                if (m->ativas[d1] < 255) {
                    m->ativas[d1]++;
                }
            } else if (tipo == 0x80 || (tipo == 0x90 && d2 == 0)) {
                if (m->ativas[d1]) {
                    m->ativas[d1]--;
                }
            }
        }
    }
    ler_delta(t);
}

static midi_trilha_t *proxima_trilha(midi_t *m)
{
    midi_trilha_t *melhor = NULL;
    for (int i = 0; i < m->n_trilhas; i++) {
        midi_trilha_t *t = &m->trilhas[i];
        if (!t->acabou && (!melhor || t->proximo_tick < melhor->proximo_tick)) {
            melhor = t;
        }
    }
    return melhor;
}

bool midi_proxima(midi_t *m, midi_nota_t *saida)
{
    if (m->terminou) {
        return false;
    }
    for (;;) {
        midi_trilha_t *t = proxima_trilha(m);
        if (!t) {
            m->terminou = true;
            if (m->resto_us) {
                uint32_t ms = (uint32_t)(m->resto_us / 1000);
                m->resto_us = 0;
                if (ms) {
                    saida->freq = midi_frequencia(m->nota_atual);
                    saida->ms = (uint16_t)(ms > 65535 ? 65535 : ms);
                    return true;
                }
            }
            return false;
        }
        if (t->proximo_tick > m->tick_atual) {
            uint32_t ticks = t->proximo_tick - m->tick_atual;
            m->resto_us += (uint64_t)ticks * m->tempo_us / m->divisao;
            m->tick_atual = t->proximo_tick;
        }
        /* Processa todos os eventos deste tick antes de decidir a nota */
        while ((t = proxima_trilha(m)) && t->proximo_tick == m->tick_atual) {
            processar_evento(m, t);
        }
        int nova = nota_mais_aguda(m);
        if (nova != m->nota_atual) {
            uint32_t ms = (uint32_t)(m->resto_us / 1000);
            int anterior = m->nota_atual;
            m->nota_atual = nova;
            m->resto_us -= (uint64_t)ms * 1000;
            if (ms > 0) {
                saida->freq = midi_frequencia(anterior);
                saida->ms = (uint16_t)(ms > 65535 ? 65535 : ms);
                return true;
            }
        }
    }
}

uint32_t midi_duracao_ms(const uint8_t *dados, size_t tamanho)
{
    static midi_t m; /* grande demais para a pilha das tasks */
    if (!midi_abrir(&m, dados, tamanho)) {
        return 0;
    }
    uint32_t total = 0;
    midi_nota_t n;
    while (midi_proxima(&m, &n)) {
        total += n.ms;
    }
    return total;
}
