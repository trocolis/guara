#ifndef GUARA_MIDI_H
#define GUARA_MIDI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Leitor de Standard MIDI File (tipos 0 e 1) para o buzzer monofônico.
 * Todas as trilhas são mescladas em ordem de tempo e, a cada instante, toca a
 * nota ativa mais aguda (a melodia costuma estar no topo). O canal 10
 * (percussão) é ignorado.
 */

#define MIDI_MAX_TRACKS 16

typedef struct {
    const uint8_t *ini, *end, *pos;
    uint32_t next_tick;
    uint8_t status;
    bool done;
} midi_track_t;

typedef struct {
    const uint8_t *data;
    size_t size;
    uint16_t division; /* ticks por semínima */
    uint32_t time_us;  /* microssegundos por semínima */
    int n_tracks;
    midi_track_t tracks[MIDI_MAX_TRACKS];
    uint8_t active_skills[128];
    uint32_t current_tick;
    uint64_t remainder_us; /* tempo acumulado ainda não emitido (em us * divisao) */
    int current_note;      /* -1 = pausa */
    bool ended;
} midi_t;

typedef struct {
    uint16_t freq; /* 0 = pausa */
    uint16_t ms;
} midi_note_t;

bool midi_open(midi_t *m, const uint8_t *data, size_t size);
void midi_restart(midi_t *m);
/* Próximo trecho com nota constante. Retorna false no fim da música. */
bool midi_next(midi_t *m, midi_note_t *output);
/* Duração total (percorre o arquivo numa cópia do estado). */
uint32_t midi_duration_ms(const uint8_t *data, size_t size);
uint16_t midi_frequency(int note);

#endif
