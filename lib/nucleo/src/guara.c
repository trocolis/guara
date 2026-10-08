#include "guara.h"

#include <string.h>

#include "regras.h"

static const uint8_t NIVEL_ESTAGIO[QTD_ESTAGIOS] = {1, 5, 15, 30, 50};

/* ------------------------------------------------------------------------- */
/* Tabelas de nomes e ícones                                                 */
/* ------------------------------------------------------------------------- */

static const char *const NOMES_NATUREZA[QTD_NATUREZAS] = {
    "Fogo", "Terra", "Água", "Ar", "Neutra", "Eletricidade", "Planta",
    "Gelo", "Vapor", "Cristal", "Sombra", "Luz", "Psíquica", "Lava",
};

static const char *const ICONES_NATUREZA[QTD_NATUREZAS] = {
    "fire", "earth", "water", "air", "neutral", "electricity", "plant",
    "ice", "steam", "crystal", "shadow", "light", "psychic", "lava",
};

const char *natureza_nome(natureza_t n) { return n < QTD_NATUREZAS ? NOMES_NATUREZA[n] : "?"; }
const char *natureza_icone(natureza_t n) { return n < QTD_NATUREZAS ? ICONES_NATUREZA[n] : "neutral"; }

const char *estado_nome(estado_t e)
{
    switch (e) {
    case EST_DORMINDO: return "Dormindo";
    case EST_DESMAIADO: return "Desmaiado";
    case EST_ENVENENADO: return "Envenenado";
    case EST_PARALISADO: return "Paralisado";
    case EST_QUEIMADO: return "Queimado";
    case EST_CONGELADO: return "Congelado";
    case EST_CONFUSO: return "Confuso";
    case EST_AMALDICOADO: return "Amaldiçoado";
    case EST_CANSADO: return "Cansado";
    case EST_FAMINTO: return "Faminto";
    case EST_FELIZ: return "Feliz";
    case EST_TRISTE: return "Triste";
    case EST_ZANGADO: return "Zangado";
    case EST_ENTEDIADO: return "Entediado";
    default: return "Normal";
    }
}

const char *estado_icone(estado_t e)
{
    switch (e) {
    case EST_DORMINDO: return "sleeping";
    case EST_DESMAIADO: return "fainted";
    case EST_ENVENENADO: return "poisoned";
    case EST_PARALISADO: return "paralyzed";
    case EST_QUEIMADO: return "burning";
    case EST_CONGELADO: return "frozen";
    case EST_CONFUSO: return "confused";
    case EST_AMALDICOADO: return "cursed";
    case EST_CANSADO: return "tired";
    case EST_FAMINTO: return "hungry";
    case EST_FELIZ: return "happy";
    case EST_TRISTE: return "sad";
    case EST_ZANGADO: return "angry";
    case EST_ENTEDIADO: return "bored";
    default: return NULL;
    }
}

#define E_FOGO (1u << NAT_FOGO)
#define E_TERRA (1u << NAT_TERRA)
#define E_AGUA (1u << NAT_AGUA)
#define E_AR (1u << NAT_AR)

unsigned natureza_elementos(natureza_t n)
{
    switch (n) {
    case NAT_FOGO: return E_FOGO;
    case NAT_TERRA: return E_TERRA;
    case NAT_AGUA: return E_AGUA;
    case NAT_AR: return E_AR;
    case NAT_LAVA: return E_FOGO | E_TERRA;
    case NAT_ELETRICIDADE: return E_FOGO | E_AR;
    case NAT_VAPOR: return E_FOGO | E_AGUA;
    case NAT_CRISTAL: return E_TERRA | E_AR;
    case NAT_PLANTA: return E_TERRA | E_AGUA;
    case NAT_GELO: return E_AR | E_AGUA;
    default: return 0;
    }
}

static natureza_t fusao(natureza_t a, natureza_t b)
{
    unsigned e = natureza_elementos(a) | natureza_elementos(b);
    for (int n = NAT_ELETRICIDADE; n < QTD_NATUREZAS; n++) {
        if (natureza_elementos((natureza_t)n) == e) {
            return (natureza_t)n;
        }
    }
    return a;
}

/* ------------------------------------------------------------------------- */
/* Utilidades                                                                */
/* ------------------------------------------------------------------------- */

static int limitar(int v, int min, int max)
{
    return v < min ? min : (v > max ? max : v);
}

static void somar_vital(uint16_t *v, int delta)
{
    *v = (uint16_t)limitar(*v + delta, 0, VITAL_MAX);
}

static uint32_t raiz(uint32_t n)
{
    uint32_t r = 0;
    while ((r + 1) * (r + 1) <= n) {
        r++;
    }
    return r;
}

void guara_por_estado(guara_t *g, estado_t e, bool ativo)
{
    if (ativo) {
        g->estados |= BIT_ESTADO(e);
    } else {
        g->estados &= ~BIT_ESTADO(e);
    }
}

void guara_medidor(guara_t *g, medidor_t m, int delta)
{
    g->medidores[m] = (int16_t)limitar(g->medidores[m] + delta, -MEDIDOR_MAX, MEDIDOR_MAX);
}

estagio_t guara_estagio_do_nivel(uint8_t nivel)
{
    estagio_t e = ESTAGIO_BEBE;
    for (int i = 0; i < QTD_ESTAGIOS; i++) {
        if (nivel >= NIVEL_ESTAGIO[i]) {
            e = (estagio_t)i;
        }
    }
    return e;
}

static void recalcular_maximos(guara_t *g)
{
    g->hp_max = (uint16_t)(HP_BASE + HP_POR_NIVEL * g->nivel + g->atributos[ATR_INDICE(ATR_DEF)]);
    g->mp_max = (uint16_t)(MP_BASE + MP_POR_NIVEL * g->nivel + g->atributos[ATR_INDICE(ATR_INT)] / 2);
    if (g->hp > g->hp_max) {
        g->hp = g->hp_max;
    }
    if (g->mp > g->mp_max) {
        g->mp = g->mp_max;
    }
}

/* Atributos favorecidos por cada natureza primária. */
static void atributos_da_natureza(natureza_t n, alvo_efeito_t *a, alvo_efeito_t *b)
{
    switch (n) {
    case NAT_FOGO: *a = ATR_ATK; *b = ATR_FOR; break;
    case NAT_TERRA: *a = ATR_DEF; *b = ATR_FOR; break;
    case NAT_AGUA: *a = ATR_INT; *b = ATR_DEF; break;
    case NAT_AR: *a = ATR_AGI; *b = ATR_SOR; break;
    default: *a = ATR_INT; *b = ATR_SOR; break;
    }
}

/* ------------------------------------------------------------------------- */
/* Habilidades                                                               */
/* ------------------------------------------------------------------------- */

static bool habilidade_compativel(const guara_t *g, const registro_t *h)
{
    if (h->natureza == NAT_NEUTRA || h->natureza == g->natureza) {
        return true;
    }
    return (natureza_elementos((natureza_t)h->natureza) & natureza_elementos((natureza_t)g->natureza)) != 0;
}

static void ativar_habilidade(guara_t *g, int id)
{
    for (int i = 0; i < QTD_HAB_ATIVAS; i++) {
        if (g->hab_ativas[i] == id) {
            return;
        }
    }
    for (int i = 0; i < QTD_HAB_ATIVAS; i++) {
        if (g->hab_ativas[i] == SLOT_VAZIO) {
            g->hab_ativas[i] = (uint8_t)id;
            return;
        }
    }
}

/* Aprende a próxima habilidade compatível do catálogo. Retorna o id ou -1. */
static int aprender_habilidade(guara_t *g)
{
    registro_t h;
    for (int id = 0; id < catalogo_quantidade(CAT_HABILIDADES) && id < 64; id++) {
        if (g->hab_possui & (1ull << id)) {
            continue;
        }
        if (catalogo_ler(CAT_HABILIDADES, id, &h) && habilidade_compativel(g, &h)) {
            g->hab_possui |= 1ull << id;
            ativar_habilidade(g, id);
            return id;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------------- */
/* Nascimento                                                                */
/* ------------------------------------------------------------------------- */

void guara_nascer(guara_t *g, const char *nome, rng_t *rng, uint32_t agora)
{
    memset(g, 0, sizeof(*g));
    strncpy(g->nome, nome, TAM_NOME);
    g->uid = rng_proximo(rng);
    g->cabeca = (uint8_t)rng_faixa(rng, 1, 9);
    g->cor = (uint8_t)rng_faixa(rng, COR_PRETO, COR_BRANCO);
    g->natureza_nascimento = (uint8_t)rng_faixa(rng, NAT_FOGO, NAT_AR);
    g->natureza = g->natureza_nascimento;
    g->estagio = ESTAGIO_BEBE;
    g->nivel = 1;

    for (int i = 0; i < QTD_ATRIBUTOS; i++) {
        g->atributos[i] = ATR_INICIAL;
    }
    alvo_efeito_t a, b;
    atributos_da_natureza((natureza_t)g->natureza, &a, &b);
    g->atributos[ATR_INDICE(a)] += ATR_BONUS_NATUREZA;
    g->atributos[ATR_INDICE(b)] += ATR_BONUS_NATUREZA;
    recalcular_maximos(g);
    g->hp = g->hp_max;
    g->mp = g->mp_max;

    g->energia = VITAL_MAX;
    g->humor = CENTI(80);
    g->alimentacao = CENTI(70);
    g->saude = VITAL_MAX;

    memset(g->equipado, SLOT_VAZIO, sizeof(g->equipado));
    memset(g->hab_ativas, SLOT_VAZIO, sizeof(g->hab_ativas));

    /* Kit inicial de itens: a definir no CATALOG.md (README > Reiniciar) */
    /* Primeira habilidade compatível do catálogo e o primeiro ataque da natureza */
    aprender_habilidade(g);
    registro_t h;
    for (int id = 0; id < catalogo_quantidade(CAT_HABILIDADES) && id < 64; id++) {
        if (catalogo_ler(CAT_HABILIDADES, id, &h) && h.natureza == g->natureza && h.tipo == HAB_ATAQUE) {
            g->hab_possui |= 1ull << id;
            ativar_habilidade(g, id);
            break;
        }
    }

    g->timestamp = agora;
}

/* ------------------------------------------------------------------------- */
/* Ciclo de vida                                                             */
/* ------------------------------------------------------------------------- */

static void desmaiar(guara_t *g)
{
    g->hp = 0;
    g->estados &= ~(BIT_ESTADO(EST_DORMINDO) | BIT_ESTADO(EST_ENVENENADO) | BIT_ESTADO(EST_QUEIMADO) |
                    BIT_ESTADO(EST_CONGELADO) | BIT_ESTADO(EST_AMALDICOADO) | BIT_ESTADO(EST_PARALISADO) |
                    BIT_ESTADO(EST_CONFUSO));
    guara_por_estado(g, EST_DESMAIADO, true);
    g->desmaio_min = 0;
    guara_medidor(g, MED_CUIDADO, -500);
}

static void perder_hp(guara_t *g, int pct)
{
    int dano = g->hp_max * pct / 100;
    if (dano < 1) {
        dano = 1;
    }
    g->hp = (uint16_t)limitar(g->hp - dano, 0, g->hp_max);
}

static void atualizar_emocao(guara_t *g)
{
    g->estados &= ~MASCARA_EMOCIONAIS;
    if (guara_tem(g, EST_DESMAIADO) || guara_tem(g, EST_DORMINDO)) {
        return;
    }
    if (g->energia > LIM_FELIZ && g->humor > LIM_FELIZ && g->alimentacao > LIM_FELIZ && g->saude > LIM_FELIZ) {
        guara_por_estado(g, EST_FELIZ, true);
    } else if (g->humor < LIM_HUMOR_BAIXO) {
        int raiva = g->medidores[MED_RAIVA];
        if (raiva > MED_LIMIAR_EMOCAO) {
            guara_por_estado(g, EST_ZANGADO, true);
        } else if (raiva < -MED_LIMIAR_EMOCAO) {
            guara_por_estado(g, EST_TRISTE, true);
        } else {
            guara_por_estado(g, EST_ENTEDIADO, true);
        }
    } else if (g->ocioso_min > LIMITE_OCIOSO_MIN) {
        guara_por_estado(g, EST_ENTEDIADO, true);
    }
}

void guara_minuto(guara_t *g, const relogio_ctx_t *rel, bool protegido)
{
    g->idade_min++;

    if (guara_tem(g, EST_DESMAIADO)) {
        g->desmaio_min++;
        if (g->desmaio_min >= DESMAIO_RECUPERA_MIN) {
            guara_por_estado(g, EST_DESMAIADO, false);
            g->hp = (uint16_t)(g->hp_max / 10 ? g->hp_max / 10 : 1);
        }
        return;
    }

    bool dormindo = guara_tem(g, EST_DORMINDO);

    /* Multiplicador de desgaste (%): saúde alta desacelera, tristeza/tédio aceleram. */
    int mult = 150 - g->saude * 100 / VITAL_MAX;
    if (guara_tem(g, EST_TRISTE)) {
        mult += 20;
    }
    if (guara_tem(g, EST_ENTEDIADO)) {
        mult += 5;
    }

    somar_vital(&g->alimentacao, -(DESG_ALIMENTACAO * mult / 100) / (dormindo ? 2 : 1));
    if (dormindo) {
        somar_vital(&g->energia, RECUP_ENERGIA_SONO);
    } else {
        somar_vital(&g->energia, -(DESG_ENERGIA * mult / 100));
        g->ocioso_min = (uint16_t)limitar(g->ocioso_min + 1, 0, 60000);
    }

    if (g->energia < LIM_HUMOR_CAI || g->alimentacao < LIM_HUMOR_CAI) {
        somar_vital(&g->humor, -(DESG_HUMOR * mult / 100));
    } else if (g->energia > LIM_HUMOR_SOBE && g->alimentacao > LIM_HUMOR_SOBE) {
        somar_vital(&g->humor, RECUP_HUMOR);
    }
    if (!dormindo && g->ocioso_min > LIMITE_OCIOSO_MIN) {
        somar_vital(&g->humor, -DESG_HUMOR_OCIOSO);
    }

    if (g->energia < LIM_SAUDE_CAI || g->alimentacao < LIM_SAUDE_CAI) {
        somar_vital(&g->saude, -(DESG_SAUDE * mult / 100));
    } else if (g->alimentacao > LIM_SAUDE_SOBE && g->energia > LIM_SAUDE_SOBE && g->humor > LIM_SAUDE_SOBE) {
        somar_vital(&g->saude, RECUP_SAUDE * (guara_tem(g, EST_FELIZ) ? 120 : 100) / 100);
    }

    /* Estados físicos derivados dos vitais */
    bool faminto = g->alimentacao <= LIM_FOME;
    guara_por_estado(g, EST_FAMINTO, faminto);
    if (faminto) {
        perder_hp(g, DESG_HP_FOME_PCT);
    }
    static const estado_t corrosivos[] = {EST_ENVENENADO, EST_QUEIMADO, EST_CONGELADO};
    for (unsigned i = 0; i < sizeof(corrosivos) / sizeof(corrosivos[0]); i++) {
        if (guara_tem(g, corrosivos[i])) {
            perder_hp(g, DESG_HP_ESTADO_PCT);
            somar_vital(&g->saude, -DESG_SAUDE_ESTADO);
        }
    }

    /* Sono */
    if (dormindo) {
        bool hora_acordar = rel && rel->minuto_do_dia == rel->acordar_min && g->energia > LIM_ACORDAR;
        if (faminto || g->energia >= VITAL_MAX || hora_acordar) {
            guara_por_estado(g, EST_DORMINDO, false);
        }
    } else if (!faminto) {
        bool hora_dormir = rel && rel->minuto_do_dia >= 0 && rel->minuto_do_dia == rel->dormir_min;
        if (g->energia == 0 || hora_dormir) {
            guara_por_estado(g, EST_DORMINDO, true);
        }
    }
    dormindo = guara_tem(g, EST_DORMINDO);
    guara_por_estado(g, EST_CANSADO, !dormindo && g->energia < LIM_CANSADO);

    /* Travas da recuperação temporal */
    if (protegido) {
        int piso = g->hp_max * TRAVA_HP_PCT / 100;
        if (g->hp < piso) {
            g->hp = (uint16_t)(piso ? piso : 1);
        }
        uint16_t *vitais[] = {&g->energia, &g->humor, &g->alimentacao, &g->saude};
        for (int i = 0; i < 4; i++) {
            if (*vitais[i] < TRAVA_VITAL) {
                *vitais[i] = TRAVA_VITAL;
            }
        }
    }

    if (g->hp == 0) {
        desmaiar(g);
        return;
    }

    /* Cuidado × Negligência; o descaso também move para a Raiva */
    bool negligencia = faminto || guara_tem(g, EST_CANSADO) || g->humor < CENTI(20) || g->saude < CENTI(20);
    if (negligencia) {
        guara_medidor(g, MED_CUIDADO, -MED_PASSO_CUIDADO);
        guara_medidor(g, MED_RAIVA, MED_PASSO_RAIVA);
    } else if (g->alimentacao > CENTI(50) && g->energia > CENTI(50) && g->humor > CENTI(50)) {
        guara_medidor(g, MED_CUIDADO, MED_PASSO_CUIDADO);
    }

    atualizar_emocao(g);
}

uint32_t guara_recuperar_tempo(guara_t *g, uint32_t agora, int minuto_do_dia_agora, int dormir_min, int acordar_min)
{
    if (g->timestamp == 0 || agora <= g->timestamp) {
        g->timestamp = agora;
        return 0;
    }
    uint32_t delta = (agora - g->timestamp) / 60;
    uint32_t simular = delta > DELTA_MAX_MIN ? DELTA_MAX_MIN : delta;
    relogio_ctx_t rel = {.minuto_do_dia = -1, .dormir_min = dormir_min, .acordar_min = acordar_min};
    for (uint32_t i = 0; i < simular; i++) {
        if (minuto_do_dia_agora >= 0) {
            int32_t m = minuto_do_dia_agora - (int32_t)(simular - 1 - i);
            rel.minuto_do_dia = (int)(((m % 1440) + 1440) % 1440);
        }
        guara_minuto(g, &rel, true);
    }
    g->timestamp += delta * 60;
    return simular;
}

static bool na_janela(int minuto, int inicio, int duracao)
{
    int d = ((minuto - inicio) % 1440 + 1440) % 1440;
    return d < duracao;
}

void guara_interagir(guara_t *g, const relogio_ctx_t *rel)
{
    g->ocioso_min = 0;
    if (!rel || rel->minuto_do_dia < 0) {
        return;
    }
    if (na_janela(rel->minuto_do_dia, rel->acordar_min, 120)) {
        guara_medidor(g, MED_DIA, MED_PASSO_DIA);
    } else if (na_janela(rel->minuto_do_dia, rel->dormir_min - 120, 120)) {
        guara_medidor(g, MED_DIA, -MED_PASSO_DIA);
    } else if (g->medidores[MED_DIA] > 0) {
        guara_medidor(g, MED_DIA, -(g->medidores[MED_DIA] < MED_PASSO_DIA / 2 ? g->medidores[MED_DIA] : MED_PASSO_DIA / 2));
    } else if (g->medidores[MED_DIA] < 0) {
        guara_medidor(g, MED_DIA, -g->medidores[MED_DIA] < MED_PASSO_DIA / 2 ? -g->medidores[MED_DIA] : MED_PASSO_DIA / 2);
    }
}

/* ------------------------------------------------------------------------- */
/* Itens                                                                     */
/* ------------------------------------------------------------------------- */

bool guara_versao_desenvolvimento(const char *versao)
{
    return versao && versao[0] == '0' && versao[1] == '.';
}

static uint64_t mascara_catalogo(catalogo_t cat)
{
    int n = catalogo_quantidade(cat);
    return n >= 64 ? ~0ull : (1ull << n) - 1;
}

void guara_liberar_catalogo(guara_t *g)
{
    int n = catalogo_quantidade(CAT_ITENS);
    for (int id = 0; id < n && id < QTD_ITENS; id++) {
        g->inventario[id] = 255;
    }
    g->equip_possui |= mascara_catalogo(CAT_EQUIPAMENTOS);
    g->hab_possui |= mascara_catalogo(CAT_HABILIDADES);
}

static bool item_cura_hp(const registro_t *item)
{
    for (int i = 0; i < item->n_efeitos; i++) {
        if (item->efeitos[i].alvo == VITAL_HP && item->efeitos[i].valor > 0) {
            return true;
        }
    }
    return false;
}

uso_t guara_pode_usar_item(const guara_t *g, const registro_t *item)
{
    if (item->id >= QTD_ITENS) {
        return USO_INVALIDO;
    }
    if (g->inventario[item->id] == 0) {
        return USO_SEM_ESTOQUE;
    }
    if (guara_tem(g, EST_AMALDICOADO)) {
        return USO_BLOQUEADO_MALDICAO;
    }
    if (guara_tem(g, EST_DESMAIADO) && (g->desmaio_min < DESMAIO_BLOQUEIO_MIN || !item_cura_hp(item))) {
        return USO_BLOQUEADO_DESMAIADO;
    }
    return USO_OK;
}

static void aplicar_sobre_efeito(guara_t *g, uint8_t estado, int valor, rng_t *rng)
{
    if (estado == EST_TODOS) {
        if (valor <= -100) {
            g->estados &= ~(MASCARA_FISICOS & ~(BIT_ESTADO(EST_DORMINDO) | BIT_ESTADO(EST_DESMAIADO)));
        }
        return;
    }
    if (estado >= 32) {
        return;
    }
    if (valor <= -100 || (valor < 0 && rng_chance(rng, -valor))) {
        guara_por_estado(g, (estado_t)estado, false);
    } else if (valor > 0 && rng_chance(rng, valor)) {
        guara_por_estado(g, (estado_t)estado, true);
    }
}

uso_t guara_usar_item(guara_t *g, const registro_t *item, rng_t *rng)
{
    uso_t pode = guara_pode_usar_item(g, item);
    if (pode != USO_OK) {
        return pode;
    }
    g->inventario[item->id]--;

    for (int i = 0; i < item->n_efeitos; i++) {
        int v = item->efeitos[i].valor;
        switch (item->efeitos[i].alvo) {
        case VITAL_HP:
            g->hp = (uint16_t)limitar(g->hp + v, 0, g->hp_max);
            break;
        case VITAL_MP:
            g->mp = (uint16_t)limitar(g->mp + v, 0, g->mp_max);
            break;
        case VITAL_ENERGIA:
            somar_vital(&g->energia, CENTI(v));
            break;
        case VITAL_HUMOR:
            somar_vital(&g->humor, CENTI(v));
            break;
        case VITAL_ALIMENTACAO:
            somar_vital(&g->alimentacao, CENTI(v));
            break;
        case VITAL_SAUDE:
            somar_vital(&g->saude, CENTI(v));
            break;
        default:
            break;
        }
    }
    for (int i = 0; i < item->n_sobre; i++) {
        aplicar_sobre_efeito(g, item->sobre[i].estado, item->sobre[i].valor, rng);
    }

    if (guara_tem(g, EST_DESMAIADO) && g->hp > 0) {
        guara_por_estado(g, EST_DESMAIADO, false);
    }
    if (g->alimentacao > LIM_FOME) {
        guara_por_estado(g, EST_FAMINTO, false);
    }
    /* Acordado para comer: volta a dormir se a energia ainda não passou de 50% */
    if (guara_tem(g, EST_DORMINDO) && g->energia > LIM_ACORDAR) {
        guara_por_estado(g, EST_DORMINDO, false);
    }
    if (item->tipo == ITEM_COMIDA || item->tipo == ITEM_BEBIDA) {
        guara_afinidade(g, (natureza_t)item->natureza, AFIN_POR_ITEM);
        guara_medidor(g, MED_CUIDADO, MED_PASSO_CUIDADO * 50);
    }
    atualizar_emocao(g);
    return USO_OK;
}

/* ------------------------------------------------------------------------- */
/* Afinidades e evolução                                                     */
/* ------------------------------------------------------------------------- */

bool guara_afinidade_ativa(const guara_t *g)
{
    return g->natureza == g->natureza_nascimento && g->natureza <= NAT_AR;
}

void guara_afinidade(guara_t *g, natureza_t elemento, int centi)
{
    if (!guara_afinidade_ativa(g) || elemento >= QTD_AFINIDADES) {
        return;
    }
    if (elemento == g->natureza_nascimento) {
        elemento = NAT_NEUTRA; /* pontos da natureza de nascimento vão para o neutro */
    }
    somar_vital(&g->afinidades[elemento], centi);
    natureza_t oposto;
    switch (elemento) {
    case NAT_FOGO: oposto = NAT_AGUA; break;
    case NAT_AGUA: oposto = NAT_FOGO; break;
    case NAT_TERRA: oposto = NAT_AR; break;
    case NAT_AR: oposto = NAT_TERRA; break;
    default: return;
    }
    somar_vital(&g->afinidades[oposto], -centi);
}

static void evoluir_natureza(guara_t *g)
{
    if (!guara_afinidade_ativa(g)) {
        return;
    }
    int dominante = NAT_NEUTRA;
    for (int i = 0; i < QTD_AFINIDADES; i++) {
        if (g->afinidades[i] > g->afinidades[dominante]) {
            dominante = i;
        }
    }
    if (dominante != NAT_NEUTRA) {
        if (g->afinidades[dominante] >= EVO_AFIN_FUSAO && dominante != g->natureza_nascimento) {
            g->natureza = (uint8_t)fusao((natureza_t)g->natureza_nascimento, (natureza_t)dominante);
        }
        return;
    }
    int dia = g->medidores[MED_DIA];
    if (dia >= EVO_DIA_NOITE) {
        g->natureza = NAT_LUZ;
        return;
    }
    if (dia <= -EVO_DIA_NOITE) {
        g->natureza = NAT_SOMBRA;
        return;
    }
    int inteligencia = g->atributos[ATR_INDICE(ATR_INT)], outro = 0;
    for (int i = 0; i < QTD_ATRIBUTOS; i++) {
        if (i != ATR_INDICE(ATR_INT) && g->atributos[i] > outro) {
            outro = g->atributos[i];
        }
    }
    if (inteligencia * 10 >= outro * EVO_INT_FATOR_X10) {
        g->natureza = NAT_PSIQUICA;
    } else if (g->estagio == ESTAGIO_ADULTO) {
        g->natureza = NAT_NEUTRA;
    }
}

/* ------------------------------------------------------------------------- */
/* Experiência                                                               */
/* ------------------------------------------------------------------------- */

uint32_t guara_xp_proximo(uint8_t nivel)
{
    /* 20 * n^1,5 */
    return 20u * nivel * raiz(nivel * 100u) / 10u;
}

int guara_bonus_idade(const guara_t *g)
{
    uint32_t dias = g->idade_min / 1440;
    return dias < 3 ? 125 : (dias < 30 ? 100 : 90);
}

static void subir_nivel(guara_t *g, rng_t *rng)
{
    g->nivel++;
    alvo_efeito_t a, b;
    atributos_da_natureza((natureza_t)g->natureza_nascimento, &a, &b);
    for (int i = 0; i < QTD_ATRIBUTOS; i++) {
        int ganho = rng_faixa(rng, 0, 1);
        if (i == ATR_INDICE(a) || i == ATR_INDICE(b)) {
            ganho++;
        }
        g->atributos[i] = (uint16_t)limitar(g->atributos[i] + ganho, 0, 999);
    }
    uint16_t hp_antes = g->hp_max, mp_antes = g->mp_max;
    recalcular_maximos(g);
    g->hp = (uint16_t)limitar(g->hp + (g->hp_max - hp_antes), 0, g->hp_max);
    g->mp = (uint16_t)limitar(g->mp + (g->mp_max - mp_antes), 0, g->mp_max);

    estagio_t e = guara_estagio_do_nivel(g->nivel);
    if (e > g->estagio) {
        g->estagio = (uint8_t)e;
        if (e >= ESTAGIO_JOVEM_B) {
            evoluir_natureza(g);
        }
    }
    if (g->nivel % NIVEIS_POR_HABILIDADE == 0) {
        aprender_habilidade(g);
    }
}

int guara_ganhar_xp(guara_t *g, uint32_t pontos, rng_t *rng)
{
    if (g->nivel >= NIVEL_MAX) {
        return 0;
    }
    g->xp += pontos * (uint32_t)guara_bonus_idade(g) / 100u;
    int subiu = 0;
    while (g->nivel < NIVEL_MAX && g->xp >= guara_xp_proximo(g->nivel)) {
        g->xp -= guara_xp_proximo(g->nivel);
        subir_nivel(g, rng);
        subiu++;
    }
    if (g->nivel >= NIVEL_MAX) {
        g->xp = 0;
    }
    return subiu;
}

/* ------------------------------------------------------------------------- */
/* Atributos e equipamentos                                                  */
/* ------------------------------------------------------------------------- */

int guara_tipo_do_slot(int slot)
{
    static const uint8_t tipos[QTD_SLOTS_EQUIP] = {EQ_CABECA, EQ_CORPO, EQ_MAO_ATK, EQ_MAO_DEF, EQ_PES, EQ_ACESSORIO, EQ_ACESSORIO};
    return slot >= 0 && slot < QTD_SLOTS_EQUIP ? tipos[slot] : -1;
}

int guara_atributo(const guara_t *g, alvo_efeito_t atr)
{
    int v = g->atributos[ATR_INDICE(atr)];
    registro_t eq;
    for (int s = 0; s < QTD_SLOTS_EQUIP; s++) {
        if (g->equipado[s] == SLOT_VAZIO || !catalogo_ler(CAT_EQUIPAMENTOS, g->equipado[s], &eq)) {
            continue;
        }
        for (int i = 0; i < eq.n_efeitos; i++) {
            if (eq.efeitos[i].alvo == atr) {
                v += eq.efeitos[i].valor;
            }
        }
    }
    if (guara_tem(g, EST_CANSADO)) {
        v = v * 70 / 100;
    }
    return v < 1 ? 1 : v;
}

bool guara_equipar(guara_t *g, int slot, int equip_id)
{
    registro_t eq;
    if (slot < 0 || slot >= QTD_SLOTS_EQUIP || equip_id < 0 || equip_id >= 64 || !(g->equip_possui & (1ull << equip_id))) {
        return false;
    }
    if (catalogo_ler(CAT_EQUIPAMENTOS, equip_id, &eq) && eq.tipo != guara_tipo_do_slot(slot)) {
        return false;
    }
    for (int s = 0; s < QTD_SLOTS_EQUIP; s++) {
        if (g->equipado[s] == equip_id) {
            g->equipado[s] = SLOT_VAZIO;
        }
    }
    g->equipado[slot] = (uint8_t)equip_id;
    return true;
}

void guara_desequipar(guara_t *g, int slot)
{
    if (slot >= 0 && slot < QTD_SLOTS_EQUIP) {
        g->equipado[slot] = SLOT_VAZIO;
    }
}

/* ------------------------------------------------------------------------- */
/* Exibição                                                                  */
/* ------------------------------------------------------------------------- */

int guara_emocao(const guara_t *g)
{
    static const estado_t ordem[] = {EST_ZANGADO, EST_TRISTE, EST_ENTEDIADO, EST_FELIZ};
    for (unsigned i = 0; i < sizeof(ordem) / sizeof(ordem[0]); i++) {
        if (guara_tem(g, ordem[i])) {
            return ordem[i];
        }
    }
    return -1;
}

int guara_estado_principal(const guara_t *g)
{
    static const estado_t ordem[] = {EST_DESMAIADO, EST_DORMINDO, EST_FAMINTO, EST_ENVENENADO, EST_QUEIMADO,
                                     EST_CONGELADO, EST_PARALISADO, EST_CONFUSO, EST_AMALDICOADO, EST_CANSADO};
    for (unsigned i = 0; i < sizeof(ordem) / sizeof(ordem[0]); i++) {
        if (guara_tem(g, ordem[i])) {
            return ordem[i];
        }
    }
    return -1;
}
