#ifndef GUARA_PINS_H
#define GUARA_PINS_H

/* Mapa de pinos do Guará (README > Hardware > Esquema). */

/* I2C compartilhado: display SH1106 (0x3C) e RTC DS3231 (0x68) */
#define PIN_SDA 21
#define PIN_SCL 22
#define I2C_FREQ_HZ 400000
#define ADDRESS_SH1106 0x3C
#define ADDRESS_DS3231 0x68

/* Cartão MicroSD (VSPI) */
#define PIN_SD_CS 5
#define PIN_SD_SCK 18
#define PIN_SD_MISO 19
#define PIN_SD_MOSI 23

/* Buzzer (LEDC canal 0) */
#define PIN_BUZZER 25

/* Bateria: divisor 100k/100k no ADC1_CH6 e CHRG do TP4056 (ativo em nível baixo) */
#define PIN_BATTERY 34
#define PIN_CHRG 35

/*
 * Botões (pull-up interno, ativos em nível baixo). Nos botões direcionais,
 * "Direita"/"Esquerda" são a direção do movimento na tela (vista do usuário):
 * o botão Direita (GPIO27) move o cursor para +X. Esta é a exceção à
 * convenção de lados do README, que vale para a disposição dos elementos.
 */
#define PIN_UP 32
#define PIN_DOWN 33
#define PIN_RIGHT 27
#define PIN_LEFT 14
#define PIN_A 13
#define PIN_B 26

#define PIN_SCREEN_LEFT PIN_LEFT /* move para -X na tela */
#define PIN_SCREEN_RIGHT PIN_RIGHT /* move para +X na tela */

#endif
