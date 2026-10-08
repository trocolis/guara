#ifndef GUARA_AUDIO_H
#define GUARA_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

/* Buzzer piezoelétrico: onda quadrada via LEDC, tocada por uma task própria. */

typedef struct {
    uint16_t freq; /* Hz; 0 = pausa */
    uint16_t ms;
} nota_t;

/* Fonte de música em segundo plano (ex.: leitor de MIDI). Chamada pela task de áudio. */
typedef struct {
    bool (*proxima)(void *ctx, nota_t *nota); /* false = fim */
    void (*fechar)(void *ctx);
    void *ctx;
} fonte_musica_t;

void audio_iniciar(void);
void audio_mudo(bool mudo);
bool audio_esta_mudo(void);
void audio_volume(int nivel); /* 1..3 */
int audio_nivel_volume(void);

/* Efeitos sonoros: interrompem a música enquanto tocam. `forcar` toca mesmo no mudo (alarmes). */
void audio_efeito(const nota_t *notas, int quantidade, bool forcar);
void audio_tom(uint16_t freq, uint16_t ms);
void audio_parar_efeitos(void);

void audio_tocar_musica(const fonte_musica_t *fonte);
void audio_parar_musica(void);
void audio_pausar_musica(bool pausar);
bool audio_musica_ativa(void);
bool audio_musica_pausada(void);

/* Efeitos prontos */
void audio_bip(void);
void audio_erro(void);
void audio_mordida(void);
void audio_vitoria(void);
void audio_derrota(void);
void audio_alarme(void);
void audio_level_up(void);

#endif
