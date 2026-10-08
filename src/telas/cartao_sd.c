#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "app.h"
#include "arquivos.h"
#include "audio.h"
#include "componentes.h"
#include "modal.h"
#include "sdcard.h"
#include "telas.h"

/*
 * Gerenciador do cartão SD (README > Cartão SD). A abre a pasta ou o modal de
 * comandos do arquivo; ←/→ abre os comandos também para pastas. B sobe um nível.
 */

#define MAX_ENTRADAS 96

static char pasta[160] = SD_RAIZ;
static lista_arquivos_t *itens;
static ui_lista_t lista;
static struct {
    char origem[192];
    bool mover;
    bool ativo;
} area;

static bool na_raiz(void)
{
    return strcmp(pasta, SD_RAIZ) == 0;
}

/* Linhas extras no topo: "Colar aqui" (se houver algo copiado) e ".." */
static int extras(void)
{
    return (area.ativo ? 1 : 0) + (na_raiz() ? 0 : 1);
}

static void recarregar(void)
{
    free(itens);
    itens = arquivos_listar(pasta, NULL, true, MAX_ENTRADAS);
    lista.total = (itens ? itens->quantidade : 0) + extras();
    ui_lista_ajustar(&lista, UI_LINHAS_VISIVEIS);
}

static const entrada_t *entrada_sel(void)
{
    int i = lista.sel - extras();
    return itens && i >= 0 && i < itens->quantidade ? &itens->itens[i] : NULL;
}

static void caminho_de(const entrada_t *e, char *buf, int tam)
{
    snprintf(buf, (size_t)tam, "%s/%s", pasta, e->nome);
}

static void item(int i, ui_item_t *it, void *ctx)
{
    (void)ctx;
    if (area.ativo && i == 0) {
        it->icone = "add";
        snprintf(it->texto, sizeof(it->texto), "%s aqui", area.mover ? "Mover" : "Colar");
        return;
    }
    if (!na_raiz() && i == (area.ativo ? 1 : 0)) {
        it->icone = "up";
        snprintf(it->texto, sizeof(it->texto), "..");
        return;
    }
    const entrada_t *e = &itens->itens[i - extras()];
    it->icone = arquivos_icone(e);
    snprintf(it->texto, sizeof(it->texto), "%s", e->nome);
    if (e->pasta) {
        snprintf(it->info, sizeof(it->info), "DIR");
    } else {
        arquivos_formatar_tamanho(e->tamanho, it->info, sizeof(it->info));
    }
}

static void subir(void)
{
    char *barra = strrchr(pasta, '/');
    if (barra && !na_raiz()) {
        *barra = '\0';
        lista = (ui_lista_t){0};
        recarregar();
    }
}

static bool copiar_arquivo(const char *de, const char *para)
{
    FILE *a = fopen(de, "rb"), *b = a ? fopen(para, "wb") : NULL;
    bool ok = a && b;
    char buf[1024];
    size_t n;
    while (ok && (n = fread(buf, 1, sizeof(buf), a)) > 0) {
        ok = fwrite(buf, 1, n, b) == n;
    }
    if (a) fclose(a);
    if (b) fclose(b);
    return ok;
}

static bool apagar_recursivo(const char *caminho)
{
    struct stat st;
    if (stat(caminho, &st) != 0) {
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        return remove(caminho) == 0;
    }
    DIR *d = opendir(caminho);
    struct dirent *de;
    char filho[192];
    while (d && (de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) {
            snprintf(filho, sizeof(filho), "%s/%s", caminho, de->d_name);
            apagar_recursivo(filho);
        }
    }
    if (d) {
        closedir(d);
    }
    return rmdir(caminho) == 0;
}

static void colar(void)
{
    const char *nome = strrchr(area.origem, '/');
    char destino[192];
    snprintf(destino, sizeof(destino), "%s%s", pasta, nome ? nome : "/arquivo");
    bool ok = area.mover ? rename(area.origem, destino) == 0 : copiar_arquivo(area.origem, destino);
    area.ativo = false;
    recarregar();
    if (!ok) {
        audio_erro();
        ui_alerta("CARTAO SD", "sd_card", "Não foi possível concluir.", "OK", NULL, NULL);
    }
}

static void renomeado(const char *novo, void *ctx)
{
    (void)ctx;
    const entrada_t *e = entrada_sel();
    if (!novo || !*novo || !e) {
        return;
    }
    char de[192], para[192];
    caminho_de(e, de, sizeof(de));
    snprintf(para, sizeof(para), "%s/%s", pasta, novo);
    if (rename(de, para) != 0) {
        audio_erro();
        ui_alerta("RENOMEAR", "file", "Falhou.", "OK", NULL, NULL);
    }
    recarregar();
}

static void excluir_2(int r, void *ctx)
{
    (void)ctx;
    const entrada_t *e = entrada_sel();
    if (r == 1 && e) {
        char c[192];
        caminho_de(e, c, sizeof(c));
        apagar_recursivo(c);
        recarregar();
    }
}

static void excluir_1(int r, void *ctx)
{
    (void)ctx;
    if (r == 1) {
        ui_confirmar("EXCLUIR", "kill", "Não dá para desfazer. Confirma?", "NAO", "EXCLUIR", excluir_2, NULL);
    }
}

static void abrir_arquivo(const entrada_t *e)
{
    char c[192];
    caminho_de(e, c, sizeof(c));
    if (arquivos_tem_extensao(e->nome, ".bmp")) {
        telas_ver_imagem(c);
    } else if (arquivos_tem_extensao(e->nome, ".txt")) {
        telas_ler_texto(c);
    } else if (arquivos_tem_extensao(e->nome, ".lua")) {
        telas_executar_jogo(c);
    } else if (arquivos_tem_extensao(e->nome, ".mid") || arquivos_tem_extensao(e->nome, ".midi")) {
        telas_tocar_musica(c);
    } else {
        ui_alerta("ABRIR", arquivos_icone(e), "Tipo de arquivo não suportado.", "OK", NULL, NULL);
    }
}

static void comando(int r, void *ctx)
{
    (void)ctx;
    const entrada_t *e = entrada_sel();
    if (!e) {
        return;
    }
    char c[192];
    caminho_de(e, c, sizeof(c));
    switch (r) {
    case 0:
        if (e->pasta) {
            snprintf(pasta + strlen(pasta), sizeof(pasta) - strlen(pasta), "/%s", e->nome);
            lista = (ui_lista_t){0};
            recarregar();
        } else {
            abrir_arquivo(e);
        }
        break;
    case 1:
    case 2:
        snprintf(area.origem, sizeof(area.origem), "%s", c);
        area.mover = r == 2;
        area.ativo = true;
        recarregar();
        break;
    case 3:
        ui_teclado("Novo nome", e->nome, 60, renomeado, NULL);
        break;
    case 4:
        ui_confirmar("EXCLUIR", arquivos_icone(e), e->nome, "NAO", "SIM", excluir_1, NULL);
        break;
    case 5: {
        char texto[200], tam[12];
        arquivos_formatar_tamanho(e->tamanho, tam, sizeof(tam));
        snprintf(texto, sizeof(texto), "%s\nTipo: %s\nTamanho: %lu bytes", e->nome, e->pasta ? "pasta" : arquivos_icone(e),
                 (unsigned long)e->tamanho);
        ui_detalhes("DETALHES", texto);
        break;
    }
    default:
        break;
    }
}

static void comandos(const entrada_t *e)
{
    static const char *const opcoes[] = {"Abrir", "Copiar", "Mover", "Renomear", "Excluir", "Detalhes"};
    ui_escolha(e->nome, opcoes, 6, comando, NULL);
}

static void tecla(const evento_tecla_t *ev)
{
    if (ui_lista_tecla(&lista, ev, UI_LINHAS_VISIVEIS) || ev->tipo != TECLA_PRESSIONOU) {
        return;
    }
    if (!sd_montado()) {
        if (ev->tecla == TECLA_A) {
            sd_montar();
            recarregar();
        } else if (ev->tecla == TECLA_B) {
            ui_voltar();
        }
        return;
    }
    const entrada_t *e = entrada_sel();
    switch (ev->tecla) {
    case TECLA_B:
        if (na_raiz()) {
            ui_voltar();
        } else {
            subir();
        }
        break;
    case TECLA_A:
        if (area.ativo && lista.sel == 0) {
            colar();
        } else if (!e) {
            subir();
        } else if (e->pasta) {
            comando(0, NULL);
        } else {
            comandos(e);
        }
        break;
    case TECLA_ESQ:
    case TECLA_DIR:
        if (e) {
            comandos(e);
        }
        break;
    default:
        break;
    }
}

static void desenhar(gfx_t *g, uint32_t agora)
{
    ui_titulo(g, pasta, agora);
    if (!sd_montado()) {
        gfx_texto_centro(g, 0, GFX_LARGURA, 26, "Cartão não montado", GFX_BRANCO);
        ui_rodape(g, "VOLTAR", "MONTAR", agora);
        return;
    }
    if (lista.total == 0) {
        gfx_texto_centro(g, 0, GFX_LARGURA, 32, "Pasta vazia", GFX_BRANCO);
        return;
    }
    ui_lista_desenhar(g, &lista, UI_TITULO_ALTURA, UI_LINHAS_VISIVEIS, item, NULL, agora);
}

static void abrir(void *param)
{
    (void)param;
    if (!sd_montado()) {
        sd_montar();
    }
    snprintf(pasta, sizeof(pasta), "%s", SD_RAIZ);
    lista = (ui_lista_t){0};
    recarregar();
}

static void retomar(void)
{
    recarregar();
}

static void fechar(void)
{
    free(itens);
    itens = NULL;
    area.ativo = false;
}

const tela_t TELA_CARTAO_SD = {
    .nome = "cartao_sd",
    .abrir = abrir,
    .retomar = retomar,
    .fechar = fechar,
    .tecla = tecla,
    .desenhar = desenhar,
};
