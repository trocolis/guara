#ifndef GUARA_MODAL_H
#define GUARA_MODAL_H

/*
 * Modais padrão: 112x48 sobre a tela atual, sombra de 2 px, barra de título
 * preta. A resposta chega pelo callback depois que o modal fecha:
 *   -1 = B/cancelar; confirmação: 1 = sim (A); escolha: índice; quantidade: valor.
 */
typedef void (*ui_result_fn)(int answer, void *ctx);

void ui_alert(const char *title, const char *icon, const char *message, const char *text_a, ui_result_fn fn,
               void *ctx);
void ui_confirm(const char *title, const char *icon, const char *message, const char *text_no,
                  const char *text_yes, ui_result_fn fn, void *ctx);
void ui_choice(const char *title, const char *const *options, int count, ui_result_fn fn, void *ctx);
void ui_quantity_picker(const char *title, int minimum, int maximum, int initial, ui_result_fn fn, void *ctx);
/* Modal sem rodapé com texto rolável (detalhes de item, habilidade, arquivo). */
void ui_details(const char *title, const char *text);

/* Teclado virtual. Responde com o texto digitado ou NULL se cancelado. */
typedef void (*ui_text_fn)(const char *text, void *ctx);
void ui_keyboard(const char *title, const char *initial, int max_length, ui_text_fn fn, void *ctx);

#endif
