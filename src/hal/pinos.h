#ifndef GUARA_PINOS_H
#define GUARA_PINOS_H

/* Mapa de pinos do Guará (README > Hardware > Esquema). */

/* I2C compartilhado: display SH1106 (0x3C) e RTC DS3231 (0x68) */
#define PINO_SDA 21
#define PINO_SCL 22
#define I2C_FREQ_HZ 400000
#define ENDERECO_SH1106 0x3C
#define ENDERECO_DS3231 0x68

/* Cartão MicroSD (VSPI) */
#define PINO_SD_CS 5
#define PINO_SD_SCK 18
#define PINO_SD_MISO 19
#define PINO_SD_MOSI 23

/* Buzzer (LEDC canal 0) */
#define PINO_BUZZER 25

/* Bateria: divisor 100k/100k no ADC1_CH6 e CHRG do TP4056 (ativo em nível baixo) */
#define PINO_BATERIA 34
#define PINO_CHRG 35

/*
 * Botões (pull-up interno, ativos em nível baixo). Nos botões direcionais,
 * "Direita"/"Esquerda" são a direção do movimento na tela (vista do usuário):
 * o botão Direita (GPIO27) move o cursor para +X. Esta é a exceção à
 * convenção de lados do README, que vale para a disposição dos elementos.
 */
#define PINO_CIMA 32
#define PINO_BAIXO 33
#define PINO_DIREITA 27
#define PINO_ESQUERDA 14
#define PINO_A 13
#define PINO_B 26

#define PINO_TELA_ESQ PINO_ESQUERDA /* move para -X na tela */
#define PINO_TELA_DIR PINO_DIREITA  /* move para +X na tela */

#endif
