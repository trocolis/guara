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

#define MIDI_MAX_TRILHAS 16

typedef struct {
    const uint8_t *ini, *fim, *pos;
    uint32_t proximo_tick;
    uint8_t status;
    bool acabou;
} midi_trilha_t;

typedef struct {
    const uint8_t *dados;
    size_t tamanho;
    uint16_t divisao;      /* ticks por semínima */
    uint32_t tempo_us;     /* microssegundos por semínima */
    int n_trilhas;
    midi_trilha_t trilhas[MIDI_MAX_TRILHAS];
    uint8_t ativas[128];
    uint32_t tick_atual;
    uint64_t resto_us;     /* tempo acumulado ainda não emitido (em us * divisao) */
    int nota_atual;        /* -1 = pausa */
    bool terminou;
} midi_t;

typedef struct {
    uint16_t freq; /* 0 = pausa */
    uint16_t ms;
} midi_nota_t;

bool midi_abrir(midi_t *m, const uint8_t *dados, size_t tamanho);
void midi_reiniciar(midi_t *m);
/* Próximo trecho com nota constante. Retorna false no fim da música. */
bool midi_proxima(midi_t *m, midi_nota_t *saida);
/* Duração total (percorre o arquivo numa cópia do estado). */
uint32_t midi_duracao_ms(const uint8_t *dados, size_t tamanho);
uint16_t midi_frequencia(int nota);

#endif
