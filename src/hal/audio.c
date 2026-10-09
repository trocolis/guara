/*
 * Buzzer piezoelétrico: uma task toca efeitos sonoros (fila) e, nos intervalos,
 * a música em segundo plano fornecida por uma fonte (ex.: leitor de MIDI).
 */
#include "audio.h"

#include "driver/ledc.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pins.h"

typedef struct {
    note_t note;
    bool force;
} sound_effect_t;

static QueueHandle_t queue;
static SemaphoreHandle_t lock;
static music_source_t music;
static volatile bool music_active, music_paused, mute;
static volatile int volume = 3;

static const uint32_t DUTY_VOLUME[] = {0, 24, 96, 512}; /* de 1024 (10 bits) */

static esp_pm_lock_handle_t lock_sound;

/* Segura a trava de light sleep enquanto há som: o LEDC para no light sleep. */
static void hold_awake(bool sounding)
{
    static bool holding;
    if (lock_sound && sounding != holding) {
        holding = sounding;
        if (sounding) {
            esp_pm_lock_acquire(lock_sound);
        } else {
            esp_pm_lock_release(lock_sound);
        }
    }
}

/* Liga o PWM na frequência pedida (ou silencia). `force` ignora o mudo (alarmes). */
static void sound(uint16_t freq, bool force)
{
    hold_awake(freq != 0 && (!mute || force));
    if (freq == 0 || (mute && !force)) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    } else {
        ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, force ? 512 : DUTY_VOLUME[volume]);
    }
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/* Descarta a música atual; o callback de fechamento libera a memória da fonte. */
static void close_music_source(void)
{
    music_source_t old_source = music;
    music_active = false;
    music_paused = false;
    music = (music_source_t){0};
    if (old_source.on_close) {
        old_source.on_close(old_source.ctx);
    }
}

/* Task de áudio: efeitos têm prioridade e interrompem a música, que toca em fatias de 20 ms. */
static void job_audio(void *arg)
{
    (void)arg;
    sound_effect_t fx;
    for (;;) {
        if (xQueueReceive(queue, &fx, 0) == pdTRUE) {
            sound(fx.note.freq, fx.force);
            vTaskDelay(pdMS_TO_TICKS(fx.note.ms ? fx.note.ms : 1));
            if (uxQueueMessagesWaiting(queue) == 0) {
                sound(0, false);
            }
            continue;
        }
        xSemaphoreTake(lock, portMAX_DELAY);
        bool play = music_active && !music_paused;
        note_t n = {0, 0};
        if (play && !music.next_note(music.ctx, &n)) {
            close_music_source();
            play = false;
        }
        xSemaphoreGive(lock);
        if (play) {
            sound(n.freq, false);
            /* Toca a nota em fatias para atender efeitos rapidamente */
            uint32_t remaining = n.ms;
            while (remaining > 0 && uxQueueMessagesWaiting(queue) == 0) {
                uint32_t slice = remaining > 20 ? 20 : remaining;
                vTaskDelay(pdMS_TO_TICKS(slice));
                remaining -= slice;
            }
            sound(0, false);
        } else {
            sound(0, false);
            if (xQueuePeek(queue, &fx, pdMS_TO_TICKS(50)) != pdTRUE) {
                continue;
            }
        }
    }
}

/* Configura o LEDC (timer 0, canal 0, 10 bits) e cria a task de áudio. */
void audio_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);
    if (esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "audio", &lock_sound) != ESP_OK) {
        lock_sound = NULL;
    }
    ledc_channel_config_t channel = {
        .gpio_num = PIN_BUZZER,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&channel);
    queue = xQueueCreate(64, sizeof(sound_effect_t));
    lock = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(job_audio, "audio", 4096, NULL, 7, NULL, 0);
}

void audio_mute(bool m) { mute = m; }
bool audio_is_mute(void) { return mute; }

/* Ajusta o volume (1..3), que muda o duty cycle do PWM. */
void audio_volume(int level)
{
    volume = level < 1 ? 1 : (level > 3 ? 3 : level);
}

int audio_volume_level(void) { return volume; }

/* Enfileira as notas de um efeito sonoro. */
void audio_effect(const note_t *notes, int count, bool force)
{
    if (!queue || (mute && !force)) {
        return;
    }
    for (int i = 0; i < count; i++) {
        sound_effect_t fx = {notes[i], force};
        xQueueSend(queue, &fx, 0);
    }
}

/* Enfileira um único tom. */
void audio_tone(uint16_t freq, uint16_t ms)
{
    note_t n = {freq, ms};
    audio_effect(&n, 1, false);
}

/* Descarta os efeitos ainda não tocados. */
void audio_stop_effects(void)
{
    if (queue) {
        xQueueReset(queue);
    }
}

/* Troca a música de fundo pela nova fonte. */
void audio_play_music(const music_source_t *source)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    close_music_source();
    music = *source;
    music_active = true;
    xSemaphoreGive(lock);
}

/* Para e descarta a música de fundo. */
void audio_stop_music(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    close_music_source();
    xSemaphoreGive(lock);
}

void audio_pause_music(bool pause) { music_paused = pause; }
bool audio_music_active(void) { return music_active; }
bool audio_music_paused(void) { return music_paused; }

/* Bip curto de confirmação. */
void audio_beep(void)
{
    audio_tone(2000, 30);
}

/* Som de erro. */
void audio_error(void)
{
    static const note_t n[] = {{300, 80}, {0, 30}, {250, 120}};
    audio_effect(n, 3, false);
}

/* Som de mordida (animação de comer). */
void audio_bite(void)
{
    static const note_t n[] = {{180, 25}, {0, 15}, {120, 35}};
    audio_effect(n, 3, false);
}

/* Melodia de vitória. */
void audio_victory(void)
{
    static const note_t n[] = {{523, 120}, {659, 120}, {784, 120}, {1047, 300}, {0, 60}, {784, 120}, {1047, 400}};
    audio_effect(n, 7, false);
}

/* Melodia de derrota. */
void audio_defeat(void)
{
    static const note_t n[] = {{392, 200}, {349, 200}, {330, 200}, {262, 500}};
    audio_effect(n, 4, false);
}

/* Bipes do despertador (tocam mesmo no mudo). */
void audio_alarm(void)
{
    static const note_t n[] = {{2093, 120}, {0, 80}, {2093, 120}, {0, 80}, {2093, 120}, {0, 500}};
    audio_effect(n, 6, true);
}

/* Arpejo de subida de nível. */
void audio_level_up(void)
{
    static const note_t n[] = {{659, 90}, {784, 90}, {988, 90}, {1319, 250}};
    audio_effect(n, 4, false);
}
