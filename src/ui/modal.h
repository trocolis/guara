#ifndef GUARA_MODAL_H
#define GUARA_MODAL_H

/*
 * Modais padrão: 112x48 sobre a tela atual, sombra de 2 px, barra de título
 * preta. A resposta chega pelo callback depois que o modal fecha:
 *   -1 = B/cancelar; confirmação: 1 = sim (A); escolha: índice; quantidade: valor.
 */
typedef void (*ui_resposta_fn)(int resposta, void *ctx);

void ui_alerta(const char *titulo, const char *icone, const char *mensagem, const char *texto_a, ui_resposta_fn fn,
               void *ctx);
void ui_confirmar(const char *titulo, const char *icone, const char *mensagem, const char *texto_nao,
                  const char *texto_sim, ui_resposta_fn fn, void *ctx);
void ui_escolha(const char *titulo, const char *const *opcoes, int quantidade, ui_resposta_fn fn, void *ctx);
void ui_quantidade(const char *titulo, int minimo, int maximo, int inicial, ui_resposta_fn fn, void *ctx);
/* Modal sem rodapé com texto rolável (detalhes de item, habilidade, arquivo). */
void ui_detalhes(const char *titulo, const char *texto);

/* Teclado virtual. Responde com o texto digitado ou NULL se cancelado. */
typedef void (*ui_texto_fn)(const char *texto, void *ctx);
void ui_teclado(const char *titulo, const char *inicial, int tamanho_max, ui_texto_fn fn, void *ctx);

#endif
