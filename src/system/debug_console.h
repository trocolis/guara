#ifndef GUARA_DEBUG_H
#define GUARA_DEBUG_H

/*
 * Console de depuração pela serial (UART0, 115200):
 *   w/s/a/d = Cima/Baixo/Esquerda/Direita da tela, j = A, k = B
 *   P = imprime o framebuffer ("FB:" + 2048 dígitos hex, formato de páginas)
 *   S = imprime o estado do Guará
 * Ver tools/console.py.
 */
void debug_console_init(void);

#endif
