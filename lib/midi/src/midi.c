/*
 * Leitor de Standard MIDI File que reduz a música a uma voz (a nota ativa mais
 * aguda), para tocar no buzzer piezoelétrico.
 */
#include "midi.h"

#include <string.h>

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

/* Frequências (mHz) da oitava 5 do MIDI (notas 60..71: C4..B4 em Hz x1000). */
static const uint32_t OCTAVE[12] = {261626, 277183, 293665, 311127, 329628, 349228,
                                    369994, 391995, 415305, 440000, 466164, 493883};

/* Frequência (Hz) de uma nota MIDI, limitada à faixa útil do piezo. */
uint16_t midi_frequency(int note)
{
    if (note < 0 || note > 127) {
        return 0;
    }
    int octave = note / 12 - 5;
    uint32_t f = OCTAVE[note % 12];
    f = octave >= 0 ? f << octave : f >> -octave;
    f /= 1000;
    if (f < 100) {
        f = 100; /* abaixo disso o piezo quase não soa */
    }
    if (f > 8000) {
        f = 8000;
    }
    return (uint16_t)f;
}

/* Lê uma quantidade de tamanho variável (VLQ) da trilha. */
static bool read_vlq(midi_track_t *t, uint32_t *v)
{
    *v = 0;
    for (int i = 0; i < 4; i++) {
        if (t->pos >= t->end) {
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

/* Lê o delta-time do próximo evento e atualiza o tick da trilha. */
static void read_delta(midi_track_t *t)
{
    uint32_t delta;
    if (t->done || !read_vlq(t, &delta)) {
        t->done = true;
        return;
    }
    t->next_tick += delta;
}

/* Valida o cabeçalho e localiza as trilhas (até 16). */
bool midi_open(midi_t *m, const uint8_t *data, size_t size)
{
    memset(m, 0, sizeof(*m));
    if (size < 14 || memcmp(data, "MThd", 4) != 0 || be32(data + 4) < 6) {
        return false;
    }
    uint16_t format = be16(data + 8);
    uint16_t n = be16(data + 10);
    uint16_t division = be16(data + 12);
    if (format > 1 || n == 0) {
        return false;
    }
    if (division & 0x8000) {
        /* SMPTE: aproximação para ticks por segundo como "semínima de 1 s" */
        int fps = -(int8_t)(division >> 8);
        division = (uint16_t)(fps * (division & 0xFF));
        m->time_us = 1000000;
    } else {
        m->time_us = 500000; /* 120 BPM */
    }
    if (division == 0) {
        return false;
    }
    m->data = data;
    m->size = size;
    m->division = division;
    const uint8_t *p = data + 8 + be32(data + 4);
    const uint8_t *end = data + size;
    while (p + 8 <= end && m->n_tracks < MIDI_MAX_TRACKS) {
        uint32_t length = be32(p + 4);
        if (memcmp(p, "MTrk", 4) == 0) {
            midi_track_t *t = &m->tracks[m->n_tracks++];
            t->ini = t->pos = p + 8;
            t->end = (p + 8 + length <= end) ? p + 8 + length : end;
        }
        p += 8 + length;
    }
    if (m->n_tracks == 0) {
        return false;
    }
    midi_restart(m);
    return true;
}

/* Volta todas as trilhas para o início. */
void midi_restart(midi_t *m)
{
    for (int i = 0; i < m->n_tracks; i++) {
        midi_track_t *t = &m->tracks[i];
        t->pos = t->ini;
        t->next_tick = 0;
        t->status = 0;
        t->done = false;
        read_delta(t);
    }
    memset(m->active_skills, 0, sizeof(m->active_skills));
    m->current_tick = 0;
    m->remainder_us = 0;
    m->current_note = -1;
    m->ended = false;
    if (m->time_us != 1000000) {
        m->time_us = 500000;
    }
}

/* Nota ativa mais aguda (a melodia costuma estar no topo), ou -1. */
static int highest_active_note(const midi_t *m)
{
    for (int n = 127; n >= 0; n--) {
        if (m->active_skills[n]) {
            return n;
        }
    }
    return -1;
}

/* Processa o próximo evento da trilha (já no tick dela). */
static void process_event(midi_t *m, midi_track_t *t)
{
    if (t->pos >= t->end) {
        t->done = true;
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
        uint8_t type = t->pos < t->end ? *t->pos++ : 0;
        uint32_t length;
        if (!read_vlq(t, &length) || t->pos + length > t->end) {
            t->done = true;
            return;
        }
        if (type == 0x51 && length == 3) {
            m->time_us = ((uint32_t)t->pos[0] << 16) | (t->pos[1] << 8) | t->pos[2];
        } else if (type == 0x2F) {
            t->done = true;
        }
        t->pos += length;
    } else if (b == 0xF0 || b == 0xF7) {
        uint32_t length;
        if (!read_vlq(t, &length) || t->pos + length > t->end) {
            t->done = true;
            return;
        }
        t->pos += length;
    } else {
        uint8_t type = b & 0xF0, channel = b & 0x0F;
        int n_data = (type == 0xC0 || type == 0xD0) ? 1 : 2;
        if (t->pos + n_data > t->end) {
            t->done = true;
            return;
        }
        uint8_t d1 = t->pos[0], d2 = n_data > 1 ? t->pos[1] : 0;
        t->pos += n_data;
        if (channel != 9 && d1 < 128) {
            if (type == 0x90 && d2 > 0) {
                if (m->active_skills[d1] < 255) {
                    m->active_skills[d1]++;
                }
            } else if (type == 0x80 || (type == 0x90 && d2 == 0)) {
                if (m->active_skills[d1]) {
                    m->active_skills[d1]--;
                }
            }
        }
    }
    read_delta(t);
}

/* Trilha com o próximo evento mais cedo. */
static midi_track_t *next_track(midi_t *m)
{
    midi_track_t *best = NULL;
    for (int i = 0; i < m->n_tracks; i++) {
        midi_track_t *t = &m->tracks[i];
        if (!t->done && (!best || t->next_tick < best->next_tick)) {
            best = t;
        }
    }
    return best;
}

/* Avança até a nota de saída mudar e devolve o trecho anterior (nota + duração). */
bool midi_next(midi_t *m, midi_note_t *output)
{
    if (m->ended) {
        return false;
    }
    for (;;) {
        midi_track_t *t = next_track(m);
        if (!t) {
            m->ended = true;
            if (m->remainder_us) {
                uint32_t ms = (uint32_t)(m->remainder_us / 1000);
                m->remainder_us = 0;
                if (ms) {
                    output->freq = midi_frequency(m->current_note);
                    output->ms = (uint16_t)(ms > 65535 ? 65535 : ms);
                    return true;
                }
            }
            return false;
        }
        if (t->next_tick > m->current_tick) {
            uint32_t ticks = t->next_tick - m->current_tick;
            m->remainder_us += (uint64_t)ticks * m->time_us / m->division;
            m->current_tick = t->next_tick;
        }
        /* Processa todos os eventos deste tick antes de decidir a nota */
        while ((t = next_track(m)) && t->next_tick == m->current_tick) {
            process_event(m, t);
        }
        int new = highest_active_note(m);
        if (new != m->current_note) {
            uint32_t ms = (uint32_t)(m->remainder_us / 1000);
            int previous = m->current_note;
            m->current_note = new;
            m->remainder_us -= (uint64_t)ms * 1000;
            if (ms > 0) {
                output->freq = midi_frequency(previous);
                output->ms = (uint16_t)(ms > 65535 ? 65535 : ms);
                return true;
            }
        }
    }
}

/* Duração total percorrendo uma cópia do estado. */
uint32_t midi_duration_ms(const uint8_t *data, size_t size)
{
    static midi_t m; /* grande demais para a pilha das tasks */
    if (!midi_open(&m, data, size)) {
        return 0;
    }
    uint32_t total = 0;
    midi_note_t n;
    while (midi_next(&m, &n)) {
        total += n.ms;
    }
    return total;
}
