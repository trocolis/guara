/*
 * Telas do menu principal e funções que uma tela usa para abrir outra.
 */
#ifndef GUARA_SCREENS_H
#define GUARA_SCREENS_H

#include <stdbool.h>

#include "ui.h"

extern const screen_t SCREEN_HOME;
extern const screen_t SCREEN_BIRTH;
extern const screen_t SCREEN_MENU;
extern const screen_t SCREEN_GUARA;
extern const screen_t SCREEN_INVENTORY;
extern const screen_t SCREEN_EQUIPMENT;
extern const screen_t SCREEN_SKILLS;
extern const screen_t SCREEN_LIBRARY;
extern const screen_t SCREEN_GAMES;
extern const screen_t SCREEN_MUSIC;
extern const screen_t SCREEN_IMAGES;
extern const screen_t SCREEN_CARD_SD;

/* Menus que são sub-menus padrão */
void arena_open_menu(void);
void clock_open_menu(void);
void settings_open_menu(void);

/* Usadas entre screens */
void screens_use_item(int item_id);        /* animação na tela principal + efeito */
void screens_view_image(const char *path); /* visualizador de .bmp */
void screens_read_text(const char *path);  /* leitor da Biblioteca */
void screens_run_game(const char *path);   /* pasta de jogo ou .lua avulso */
void screens_play_music(const char *path);
/* Toca sem abrir o tocador (trilha de jogos). */
bool screens_play_background_music(const char *path);
void screens_trigger_alarm(const char *title);
bool screens_alarm_ringing(void);
void screens_offer_update(const char *tag); /* confirma a release achada no GitHub */
void screens_open_meet(int item_id, int count); /* Conectar: -1 só recebe; senão envia o item */
/* Combate contra outro Guará pelo Conectar (quem desafiou é o anfitrião) e o motivo de não poder lutar */
void arena_open_versus(const uint8_t mac[6], uint32_t uid, const char *name, bool host);
const char *arena_cannot_fight(const char **icon);

#endif
