#include "alarmes.h"

#include <stdio.h>
#include <stdlib.h>

#include "esp_attr.h"
#include "relogio.h"
#include "sdcard.h"

static int lista[ALARMES_MAX];
static int quantidade;
static int ultimo_disparo = -1;

static RTC_DATA_ATTR uint32_t temp_fim;
static RTC_DATA_ATTR uint32_t temp_total;

static int comparar(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

static bool gravar(void)
{
    if (!sd_montado() || !sd_garantir_pasta(SD_RAIZ "/guara")) {
        return false;
    }
    FILE *f = fopen(ALARMES_ARQUIVO, "w");
    if (!f) {
        return false;
    }
    for (int i = 0; i < quantidade; i++) {
        fprintf(f, "%02d:%02d\n", lista[i] / 60, lista[i] % 60);
    }
    fclose(f);
    return true;
}

void alarmes_carregar(void)
{
    quantidade = 0;
    FILE *f = sd_montado() ? fopen(ALARMES_ARQUIVO, "r") : NULL;
    if (!f) {
        return;
    }
    int h, m;
    while (quantidade < ALARMES_MAX && fscanf(f, "%d:%d", &h, &m) == 2) {
        if (h >= 0 && h < 24 && m >= 0 && m < 60) {
            lista[quantidade++] = h * 60 + m;
        }
    }
    fclose(f);
    qsort(lista, (size_t)quantidade, sizeof(int), comparar);
}

int alarmes_quantidade(void) { return quantidade; }
int alarmes_minuto(int i) { return i >= 0 && i < quantidade ? lista[i] : 0; }

bool alarmes_adicionar(int minuto)
{
    if (quantidade >= ALARMES_MAX || !sd_montado()) {
        return false;
    }
    lista[quantidade++] = minuto;
    qsort(lista, (size_t)quantidade, sizeof(int), comparar);
    return gravar();
}

bool alarmes_editar(int i, int minuto)
{
    if (i < 0 || i >= quantidade) {
        return false;
    }
    lista[i] = minuto;
    qsort(lista, (size_t)quantidade, sizeof(int), comparar);
    return gravar();
}

bool alarmes_remover(int i)
{
    if (i < 0 || i >= quantidade) {
        return false;
    }
    for (int j = i; j < quantidade - 1; j++) {
        lista[j] = lista[j + 1];
    }
    quantidade--;
    return gravar();
}

bool alarmes_verificar(int minuto)
{
    if (minuto < 0 || minuto == ultimo_disparo) {
        return false;
    }
    for (int i = 0; i < quantidade; i++) {
        if (lista[i] == minuto) {
            ultimo_disparo = minuto;
            return true;
        }
    }
    if (ultimo_disparo != minuto) {
        ultimo_disparo = -1;
    }
    return false;
}

uint32_t alarmes_segundos_ate_proximo(void)
{
    struct tm tm;
    if (quantidade == 0 || !relogio_local(&tm)) {
        return 0;
    }
    int agora = tm.tm_hour * 60 + tm.tm_min;
    int melhor = 24 * 60;
    for (int i = 0; i < quantidade; i++) {
        int d = (lista[i] - agora + 1440) % 1440;
        if (d == 0) {
            d = 1440;
        }
        if (d < melhor) {
            melhor = d;
        }
    }
    return (uint32_t)(melhor * 60 - tm.tm_sec);
}

void temporizador_iniciar(uint32_t segundos)
{
    temp_total = segundos;
    temp_fim = relogio_agora() + segundos;
}

void temporizador_parar(void)
{
    temp_fim = 0;
}

bool temporizador_ativo(void)
{
    return temp_fim != 0;
}

uint32_t temporizador_restante(void)
{
    if (!temp_fim) {
        return 0;
    }
    uint32_t agora = relogio_agora();
    return agora >= temp_fim ? 0 : temp_fim - agora;
}

uint32_t temporizador_configurado(void)
{
    return temp_total;
}

bool temporizador_verificar(void)
{
    if (temp_fim && relogio_agora() >= temp_fim) {
        temp_fim = 0;
        return true;
    }
    return false;
}
