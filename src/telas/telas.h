#ifndef GUARA_TELAS_H
#define GUARA_TELAS_H

#include <stdbool.h>

#include "ui.h"

extern const tela_t TELA_PRINCIPAL;
extern const tela_t TELA_NASCIMENTO;
extern const tela_t TELA_MENU;
extern const tela_t TELA_GUARA;
extern const tela_t TELA_INVENTARIO;
extern const tela_t TELA_EQUIPAMENTOS;
extern const tela_t TELA_HABILIDADES;
extern const tela_t TELA_BIBLIOTECA;
extern const tela_t TELA_JOGOS;
extern const tela_t TELA_MUSICAS;
extern const tela_t TELA_IMAGENS;
extern const tela_t TELA_CARTAO_SD;

/* Menus que são sub-menus padrão */
void arena_abrir_menu(void);
void relogio_abrir_menu(void);
void ajustes_abrir_menu(void);

/* Usadas entre telas */
void telas_usar_item(int item_id);          /* animação na tela principal + efeito */
void telas_ver_imagem(const char *caminho); /* visualizador de .bmp */
void telas_ler_texto(const char *caminho);  /* leitor da Biblioteca */
void telas_executar_jogo(const char *caminho); /* pasta de jogo ou .lua avulso */
void telas_tocar_musica(const char *caminho);
/* Toca sem abrir o tocador (trilha de jogos). */
bool telas_tocar_musica_fundo(const char *caminho);
void telas_disparar_alarme(const char *titulo);
bool telas_alarme_tocando(void);

#endif
