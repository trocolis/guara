#ifndef GUARA_SUBMENU_H
#define GUARA_SUBMENU_H

/* Sub-menu padrão estático: título + lista de 3 linhas com ícone, texto e info. */
typedef struct {
    const char *icone;
    const char *texto;
    void (*info)(char *buf, int tam); /* opcional: valor à direita (até 4 caracteres) */
    void (*acao)(void);
} submenu_item_t;

typedef struct {
    const char *titulo;
    const submenu_item_t *itens;
    int quantidade;
} submenu_t;

void submenu_abrir(const submenu_t *def);

#endif
