#ifndef GUARA_AUDIO_H
#define GUARA_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

/* Buzzer piezoelétrico: onda quadrada via LEDC, tocada por uma task própria. */

typedef struct {
    uint16_t freq; /* Hz; 0 = pausa */
    uint16_t ms;
} note_t;

/* Fonte de música em segundo plano (ex.: leitor de MIDI). Chamada pela task de áudio. */
typedef struct {
    bool (*next_note)(void *ctx, note_t *note); /* false = fim */
    void (*on_close)(void *ctx);
    void *ctx;
} music_source_t;

void audio_init(void);
void audio_mute(bool mute);
bool audio_is_mute(void);
void audio_volume(int level); /* 1..3 */
int audio_volume_level(void);

/* Efeitos sonoros: interrompem a música enquanto tocam. `forcar` toca mesmo no mudo (alarmes). */
void audio_effect(const note_t *notes, int count, bool force);
void audio_tone(uint16_t freq, uint16_t ms);
void audio_stop_effects(void);

void audio_play_music(const music_source_t *source);
void audio_stop_music(void);
void audio_pause_music(bool pause);
bool audio_music_active(void);
bool audio_music_paused(void);

/* Efeitos prontos */
void audio_beep(void);
void audio_error(void);
void audio_bite(void);
void audio_gulp(void);
void audio_magic(void);
void audio_victory(void);
void audio_defeat(void);
void audio_alarm(void);
void audio_level_up(void);

#endif
