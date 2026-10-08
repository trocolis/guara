#ifndef GUARA_SUBMENU_H
#define GUARA_SUBMENU_H

/* Sub-menu padrão estático: título + lista de 3 linhas com ícone, texto e info. */
typedef struct {
    const char *icon;
    const char *text;
    void (*info)(char *buf, int length); /* opcional: valor à direita (até 4 caracteres) */
    void (*action)(void);
} submenu_item_t;

typedef struct {
    const char *title;
    const submenu_item_t *items;
    int count;
} submenu_t;

void submenu_open(const submenu_t *def);

#endif
