#include <stdio.h>
#include <string.h>

#include "app.h"
#include "audio.h"
#include "combate.h"
#include "componentes.h"
#include "modal.h"
#include "regras.h"
#include "sprite.h"
#include "submenu.h"
#include "telas.h"

/* Arena: Treino contra um Guará gerado e Combate (online, futuro). */

#define X_JOGADOR 30  /* esquerda do usuário */
#define X_OPONENTE 98 /* direita do usuário: espelhado para os dois se olharem */
#define BASE_Y 50
#define ANIM_MS 900

static combate_t combate;
static lutador_t jogador, oponente;
static resultado_t ultimo;
static bool animando;
static uint32_t anim_inicio;
static bool em_modal;
static bool fim_mostrado;
static uint32_t fim_em;
static char aviso[24];

static void encerrar(void);
static const tela_t TELA_COMBATE;

/* ---------------- Ações do jogador ---------------- */

static void executar(acao_t acao, const registro_t *item)
{
    em_modal = false;
    combate_executar(&combate, 0, acao, item, &ultimo);
    if (acao.tipo == ACAO_ITEM && item && !ultimo.falhou) {
        g_guara.inventario[item->id]--;
        app_alterou();
    }
    animando = true;
    anim_inicio = app_ms();
}

static int itens_combate[QTD_ITENS], n_itens;
static void item_escolhido(int r, void *ctx)
{
    (void)ctx;
    registro_t item;
    if (r < 0 || r >= n_itens || !catalogo_ler(CAT_ITENS, itens_combate[r], &item)) {
        em_modal = false;
        return;
    }
    executar((acao_t){ACAO_ITEM, item.id}, &item);
}

static int hab_slots[QTD_HAB_ATIVAS], n_hab;
static void habilidade_escolhida(int r, void *ctx)
{
    (void)ctx;
    if (r < 0 || r >= n_hab) {
        em_modal = false;
        return;
    }
    executar((acao_t){ACAO_HABILIDADE, hab_slots[r]}, NULL);
}

static void desistir_resposta(int r, void *ctx)
{
    (void)ctx;
    if (r == 1) {
        executar((acao_t){ACAO_DESISTIR, 0}, NULL);
    } else {
        em_modal = false;
    }
}

static void acao_escolhida(int r, void *ctx)
{
    (void)ctx;
    if (r == 0) {
        executar((acao_t){ACAO_ATACAR, 0}, NULL);
    } else if (r == 1) {
        static char nomes[QTD_HAB_ATIVAS][24];
        const char *opcoes[QTD_HAB_ATIVAS];
        n_hab = 0;
        registro_t h;
        for (int i = 0; i < QTD_HAB_ATIVAS; i++) {
            if (jogador.habilidades[i] != SLOT_VAZIO && catalogo_ler(CAT_HABILIDADES, jogador.habilidades[i], &h)) {
                snprintf(nomes[n_hab], sizeof(nomes[n_hab]), "%s (%d)", h.nome, h.custo_mp);
                opcoes[n_hab] = nomes[n_hab];
                hab_slots[n_hab++] = i;
            }
        }
        if (n_hab == 0) {
            em_modal = false;
            snprintf(aviso, sizeof(aviso), "Sem habilidades");
            return;
        }
        ui_escolha("HABILIDADE", opcoes, n_hab, habilidade_escolhida, NULL);
    } else if (r == 2) {
        static char nomes[8][24];
        const char *opcoes[8];
        n_itens = 0;
        registro_t it;
        for (int id = 0; id < catalogo_quantidade(CAT_ITENS) && n_itens < 8; id++) {
            if (g_guara.inventario[id] && catalogo_ler(CAT_ITENS, id, &it) && it.tipo == ITEM_POCAO) {
                snprintf(nomes[n_itens], sizeof(nomes[n_itens]), "%s x%d", it.nome, g_guara.inventario[id]);
                opcoes[n_itens] = nomes[n_itens];
                itens_combate[n_itens++] = id;
            }
        }
        if (n_itens == 0) {
            em_modal = false;
            snprintf(aviso, sizeof(aviso), "Sem itens de cura");
            return;
        }
        ui_escolha("ITENS", opcoes, n_itens, item_escolhido, NULL);
    } else if (r == 3) {
        ui_confirmar("DESISTIR", "fight", "Desistir do combate?", "NAO", "SIM", desistir_resposta, NULL);
    } else {
        em_modal = false;
    }
}

/* ---------------- Tela de combate ---------------- */

static void abrir(void *param)
{
    (void)param;
    combate_lutador_de_guara(&g_guara, &jogador);
    combate_gerar_oponente(&g_guara, &g_rng, &oponente);
    combate_iniciar(&combate, &jogador, &oponente, &g_rng);
    jogador = combate.lado[0];
    animando = em_modal = fim_mostrado = false;
    aviso[0] = '\0';
    app_bloquear_sono(true);
}

static void fechar(void)
{
    app_bloquear_sono(false);
}

static void tecla(const evento_tecla_t *ev)
{
    if (ev->tipo != TECLA_PRESSIONOU || animando) {
        return;
    }
    if (combate.terminado) {
        if (fim_mostrado && (ev->tecla == TECLA_A || ev->tecla == TECLA_B)) {
            encerrar();
        }
        return;
    }
    if (ev->tecla == TECLA_A && combate_pronto(&combate, 0)) {
        static const char *const opcoes[] = {"Atacar", "Habilidade", "Itens", "Desistir"};
        em_modal = true;
        aviso[0] = '\0';
        ui_escolha("ACAO", opcoes, 4, acao_escolhida, NULL);
    } else if (ev->tecla == TECLA_B) {
        em_modal = true;
        ui_confirmar("DESISTIR", "fight", "Desistir do combate?", "NAO", "SIM", desistir_resposta, NULL);
    }
}

static void ficha_fechada(int r, void *ctx)
{
    (void)r;
    (void)ctx;
    ui_voltar();
}

/* Aplica o resultado no Guará e mostra a ficha do vencedor. */
static void encerrar(void)
{
    const lutador_t *eu = &combate.lado[0];
    g_guara.hp = (uint16_t)(eu->hp > 0 ? eu->hp : 1);
    g_guara.mp = (uint16_t)eu->mp;
    uint32_t persistentes = BIT_ESTADO(EST_ENVENENADO) | BIT_ESTADO(EST_QUEIMADO) | BIT_ESTADO(EST_CONGELADO) |
                            BIT_ESTADO(EST_PARALISADO) | BIT_ESTADO(EST_CONFUSO) | BIT_ESTADO(EST_AMALDICOADO);
    g_guara.estados = (g_guara.estados & ~persistentes) | (eu->estados & persistentes);
    g_guara.energia = (uint16_t)(g_guara.energia > CENTI(5) ? g_guara.energia - CENTI(5) : 0);

    bool venceu = combate.vencedor == 0;
    guara_medidor(&g_guara, MED_VITORIAS, venceu ? MED_PASSO_VITORIA : -MED_PASSO_VITORIA);
    if (!venceu) {
        guara_medidor(&g_guara, MED_RAIVA, -MED_PASSO_TRISTEZA);
        app_alterou();
        ui_voltar();
        return;
    }
    guara_por_estado(&g_guara, EST_AMALDICOADO, false); /* cura com a vitória */
    uint32_t xp = combate_xp(&combate, 0);
    int drop = combate_drop(&combate, 0);
    char ficha[96];
    int n = snprintf(ficha, sizeof(ficha), "%s +%lu XP", g_guara.nome, (unsigned long)xp);
    if (drop >= 0 && drop < QTD_ITENS) {
        registro_t it;
        catalogo_ler(CAT_ITENS, drop, &it);
        if (g_guara.inventario[drop] < 255) {
            g_guara.inventario[drop]++;
        }
        snprintf(ficha + n, sizeof(ficha) - (size_t)n, " Ganhou: %s", it.nome);
    }
    int niveis = app_ganhar_xp(xp);
    if (niveis > 0) {
        n = (int)strlen(ficha);
        snprintf(ficha + n, sizeof(ficha) - (size_t)n, " NV %d!", g_guara.nivel);
    }
    ui_alerta("VITORIA", drop >= 0 ? NULL : "level", ficha, "OK", ficha_fechada, NULL);
}

static void passo(uint32_t agora)
{
    static uint32_t anterior;
    uint32_t dt = anterior ? agora - anterior : 0;
    anterior = agora;
    if (dt > 100) {
        dt = 100;
    }
    if (animando) {
        if (agora - anim_inicio >= ANIM_MS) {
            animando = false;
            if (combate.terminado) {
                fim_em = agora;
                if (combate.vencedor == 0) {
                    audio_vitoria();
                } else {
                    audio_derrota();
                }
            }
        }
        return;
    }
    if (combate.terminado) {
        if (!fim_mostrado && agora - fim_em > 2500) {
            fim_mostrado = true;
        }
        return;
    }
    if (em_modal || ui_topo() != &TELA_COMBATE) {
        return;
    }
    unsigned prontos = combate_atualizar(&combate, (int)dt);
    if (prontos & 2u) {
        combate_executar(&combate, 1, combate_ia(&combate, 1), NULL, &ultimo);
        animando = true;
        anim_inicio = agora;
        audio_tom(ultimo.dano ? 220 : 660, 60);
    }
}

static const char *olho_lutador(int lado, uint32_t agora)
{
    if (combate.terminado && !animando) {
        return combate.vencedor == lado ? OLHO_FELIZ : OLHO_DESMAIADO;
    }
    uint32_t t = agora - anim_inicio;
    if (animando && t > 150 && t < 700 && ultimo.alvo == lado && (ultimo.dano > 0 || ultimo.dano_estado > 0) &&
        !ultimo.esquivou) {
        return OLHO_ATORDOADO;
    }
    if (animando && ultimo.cura > 0 && ultimo.alvo == lado) {
        return OLHO_FELIZ;
    }
    return OLHO_RAIVA;
}

static void desenhar_lutador(gfx_t *g, int lado, uint32_t agora)
{
    const lutador_t *l = &combate.lado[lado];
    int x = lado == 0 ? X_JOGADOR : X_OPONENTE;
    int dir = lado == 0 ? 1 : -1; /* direção do oponente */
    uint32_t t = agora - anim_inicio;
    bool visivel = true;
    if (animando && ultimo.ator == lado && !ultimo.falhou && ultimo.tipo == ACAO_ATACAR && t < 300) {
        x += dir * (t < 150 ? (int)t / 25 : (int)(300 - t) / 25); /* pulinho na direção do alvo */
    }
    if (animando && ultimo.alvo == lado && ultimo.dano > 0 && t > 150 && t < 600) {
        x -= dir * 2; /* recuo */
        visivel = (t / 60) % 2 == 0;
    }
    bool derrotado = combate.terminado && combate.vencedor != lado && !animando;
    int quadro = derrotado ? 1 : sprite_quadro(agora);
    if (visivel) {
        sprite_guara(g, l->cor, l->cabeca, l->estagio, olho_lutador(lado, agora), x, BASE_Y, quadro,
                     lado == 1 ? GFX_ESPELHAR : 0);
    }
}

static void desenhar(gfx_t *g, uint32_t agora)
{
    passo(agora);
    const lutador_t *eu = &combate.lado[0], *op = &combate.lado[1];

    /* HP no topo de cada lado */
    ui_progresso(g, 2, 2, 56, 6, eu->hp, eu->hp_max);
    ui_progresso(g, 70, 2, 56, 6, op->hp, op->hp_max);
    gfx_texto(g, 2, 10, eu->nome, GFX_BRANCO);
    int w = gfx_largura_texto(op->nome);
    gfx_texto(g, 126 - w, 10, op->nome, GFX_BRANCO);

    desenhar_lutador(g, 0, agora);
    desenhar_lutador(g, 1, agora);

    /* Barra ATB do jogador: menor e mais fina, no canto inferior */
    ui_progresso(g, 2, 59, 50, 3, eu->atb_ms, eu->atb_total);
    char mp[12];
    snprintf(mp, sizeof(mp), "MP %d", eu->mp);
    gfx_texto(g, 56, 56, mp, GFX_BRANCO);
    if (!combate.terminado && combate_pronto(&combate, 0) && !animando && (agora / 300) % 2) {
        ui_icone_modo(g, "button_a", 116, 53, GFX_COPIA, 0);
    }

    uint32_t t = agora - anim_inicio;
    if (animando) {
        /* Ícone da habilidade/item no centro, piscando cada vez mais rápido */
        if (ultimo.icone[0] && ultimo.tipo != ACAO_ATACAR && t < 800) {
            uint32_t periodo = 200 - t * 160 / 800;
            if ((t / periodo) % 2 == 0) {
                ui_icone_modo(g, ultimo.icone, 56, 24, GFX_COPIA, 0);
            }
        }
        char msg[16] = "";
        if (ultimo.falhou) {
            snprintf(msg, sizeof(msg), "Falhou!");
        } else if (ultimo.esquivou) {
            snprintf(msg, sizeof(msg), "Esquivou!");
        } else if (ultimo.errou && !ultimo.auto_golpe) {
            snprintf(msg, sizeof(msg), "Errou!");
        } else if (ultimo.dano > 0) {
            snprintf(msg, sizeof(msg), "%s-%d", ultimo.critico ? "!" : "", ultimo.dano);
        } else if (ultimo.cura > 0) {
            snprintf(msg, sizeof(msg), "+%d", ultimo.cura);
        }
        if (msg[0] && t > 150) {
            int xa = ultimo.alvo == 0 ? X_JOGADOR : X_OPONENTE;
            int dy = (int)(t - 150) / 60;
            gfx_texto_centro(g, xa - 20, 40, 22 - dy, msg, GFX_INVERTER);
        }
    } else if (combate.terminado) {
        gfx_texto_centro(g, 0, GFX_LARGURA, 22, combate.vencedor == 0 ? "VITORIA!" : "DERROTA", GFX_BRANCO);
    } else if (aviso[0]) {
        gfx_texto_centro(g, 0, GFX_LARGURA, 22, aviso, GFX_BRANCO);
    }
}

static const tela_t TELA_COMBATE = {
    .nome = "combate",
    .fps = 30,
    .abrir = abrir,
    .fechar = fechar,
    .tecla = tecla,
    .desenhar = desenhar,
};

/* ---------------- Menu da Arena ---------------- */

static void treino(void)
{
    if (guara_tem(&g_guara, EST_DESMAIADO) || guara_tem(&g_guara, EST_DORMINDO)) {
        ui_alerta("TREINO", "sleeping", "O Guará não pode lutar agora.", "OK", NULL, NULL);
        return;
    }
    if (g_guara.hp == 0 || g_guara.hp * 5 < g_guara.hp_max) {
        ui_alerta("TREINO", "hp", "HP muito baixo para treinar.", "OK", NULL, NULL);
        return;
    }
    ui_abrir(&TELA_COMBATE, NULL);
}

static void combate_online(void)
{
    /* README: "o combate com outro guará será implementado no futuro" */
    ui_alerta("COMBATE", "fight", "Em breve: combate entre Guarás.", "OK", NULL, NULL);
}

static const submenu_item_t ITENS_ARENA[] = {
    {"training", "Treino", NULL, treino},
    {"fight", "Combate", NULL, combate_online},
};
static const submenu_t MENU_ARENA = {"ARENA", ITENS_ARENA, 2};

void arena_abrir_menu(void)
{
    submenu_abrir(&MENU_ARENA);
}
