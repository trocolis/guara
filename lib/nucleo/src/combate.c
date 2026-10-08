#include "combate.h"

#include <stdio.h>
#include <string.h>

#include "regras.h"

static int limitar(int v, int min, int max)
{
    return v < min ? min : (v > max ? max : v);
}

static bool tem(const lutador_t *l, estado_t e)
{
    return (l->estados & BIT_ESTADO(e)) != 0;
}

int combate_tempo_atb(int agilidade)
{
    int t = ATB_BASE_MS - agilidade * ATB_POR_AGI_MS;
    return t < ATB_MIN_MS ? ATB_MIN_MS : t;
}

/* Água vence Fogo, Terra vence Água, Ar vence Terra, Fogo vence Ar. */
static unsigned vence(unsigned elementos)
{
    unsigned v = 0;
    if (elementos & (1u << NAT_AGUA)) v |= 1u << NAT_FOGO;
    if (elementos & (1u << NAT_TERRA)) v |= 1u << NAT_AGUA;
    if (elementos & (1u << NAT_AR)) v |= 1u << NAT_TERRA;
    if (elementos & (1u << NAT_FOGO)) v |= 1u << NAT_AR;
    return v;
}

int combate_multiplicador(natureza_t ataque, natureza_t defensor)
{
    if ((ataque == NAT_LUZ && defensor == NAT_SOMBRA) || (ataque == NAT_SOMBRA && defensor == NAT_LUZ)) {
        return MULT_VANTAGEM;
    }
    unsigned a = natureza_elementos(ataque), d = natureza_elementos(defensor);
    if (!a || !d) {
        return 100;
    }
    /* Fraqueza do defensor é neutralizada se ele também contém o elemento atacante. */
    bool vantagem = (vence(a & ~d) & d) != 0;
    bool desvantagem = (vence(d & ~a) & a) != 0;
    if (vantagem && !desvantagem) {
        return MULT_VANTAGEM;
    }
    if (desvantagem && !vantagem) {
        return MULT_DESVANTAGEM;
    }
    return 100;
}

void combate_lutador_de_guara(const guara_t *g, lutador_t *l)
{
    memset(l, 0, sizeof(*l));
    memcpy(l->nome, g->nome, sizeof(l->nome));
    l->natureza = g->natureza;
    l->cor = g->cor;
    l->cabeca = g->cabeca;
    l->estagio = g->estagio;
    l->nivel = g->nivel;
    l->hp = g->hp;
    l->hp_max = g->hp_max;
    l->mp = g->mp;
    l->mp_max = g->mp_max;
    for (int i = 0; i < QTD_ATRIBUTOS; i++) {
        l->atributos[i] = guara_atributo(g, (alvo_efeito_t)(ATR_ATK + i));
    }
    l->estados = g->estados;
    memcpy(l->habilidades, g->hab_ativas, sizeof(l->habilidades));
}

void combate_gerar_oponente(const guara_t *ref, rng_t *rng, lutador_t *l)
{
    static const char *const nomes[] = {"Tupa", "Jaci", "Iara", "Caipora", "Saci", "Boitata", "Curupira", "Guaraci"};
    memset(l, 0, sizeof(*l));
    snprintf(l->nome, sizeof(l->nome), "%s", nomes[rng_faixa(rng, 0, 7)]);
    l->natureza = (uint8_t)rng_faixa(rng, NAT_FOGO, NAT_AR);
    l->cor = (uint8_t)rng_faixa(rng, 0, 1);
    l->cabeca = (uint8_t)rng_faixa(rng, 1, 9);
    l->estagio = ref->estagio;
    l->nivel = ref->nivel;
    for (int i = 0; i < QTD_ATRIBUTOS; i++) {
        int base = guara_atributo(ref, (alvo_efeito_t)(ATR_ATK + i));
        l->atributos[i] = limitar(base * rng_faixa(rng, 90, 110) / 100, 1, 999);
    }
    l->hp_max = limitar(ref->hp_max * rng_faixa(rng, 90, 110) / 100, 1, 9999);
    l->mp_max = ref->mp_max;
    l->hp = l->hp_max;
    l->mp = l->mp_max;
    memset(l->habilidades, SLOT_VAZIO, sizeof(l->habilidades));
    /* Investida + até três habilidades compatíveis do catálogo */
    registro_t h;
    int n = 0;
    for (int id = 0; id < catalogo_quantidade(CAT_HABILIDADES) && n < QTD_HAB_ATIVAS; id++) {
        if (catalogo_ler(CAT_HABILIDADES, id, &h) && (h.natureza == NAT_NEUTRA || h.natureza == l->natureza)) {
            l->habilidades[n++] = (uint8_t)id;
        }
    }
}

void combate_iniciar(combate_t *c, const lutador_t *jogador, const lutador_t *oponente, rng_t *rng)
{
    memset(c, 0, sizeof(*c));
    c->lado[0] = *jogador;
    c->lado[1] = *oponente;
    c->vencedor = -1;
    c->rng = rng;
    for (int i = 0; i < 2; i++) {
        lutador_t *l = &c->lado[i];
        /* Estados de vida que não valem no combate */
        l->estados &= ~(BIT_ESTADO(EST_FAMINTO) | BIT_ESTADO(EST_DORMINDO));
        l->atb_total = combate_tempo_atb(l->atributos[ATR_INDICE(ATR_AGI)]);
        l->atb_ms = 0;
    }
}

unsigned combate_atualizar(combate_t *c, int dt_ms)
{
    unsigned prontos = 0;
    if (c->terminado) {
        return 0;
    }
    for (int i = 0; i < 2; i++) {
        lutador_t *l = &c->lado[i];
        if (l->atb_ms < l->atb_total) {
            l->atb_ms = limitar(l->atb_ms + dt_ms, 0, l->atb_total);
        }
        if (l->atb_ms >= l->atb_total) {
            prontos |= 1u << i;
        }
    }
    return prontos;
}

bool combate_pronto(const combate_t *c, int lado)
{
    return !c->terminado && c->lado[lado].atb_ms >= c->lado[lado].atb_total;
}

static int atributo(const lutador_t *l, alvo_efeito_t a)
{
    int v = l->atributos[ATR_INDICE(a)];
    if (tem(l, EST_ZANGADO)) {
        if (a == ATR_ATK) {
            v = v * 150 / 100;
        } else if (a == ATR_DEF) {
            v = v * 50 / 100;
        }
    }
    return v;
}

static void verificar_fim(combate_t *c)
{
    for (int i = 0; i < 2; i++) {
        if (c->lado[i].hp <= 0) {
            c->lado[i].hp = 0;
            c->terminado = true;
            c->vencedor = 1 - i;
            return;
        }
    }
}

static bool esquivou(combate_t *c, const lutador_t *atk, const lutador_t *def)
{
    int diff = atributo(def, ATR_AGI) - atributo(atk, ATR_AGI);
    if (diff <= 0) {
        return false;
    }
    int chance = limitar(diff * ESQUIVA_POR_AGI_X10, 0, ESQUIVA_MAX_X10);
    return rng_faixa(c->rng, 0, 999) < chance;
}

static bool critico(combate_t *c, const lutador_t *atk)
{
    int chance = limitar(CRITICO_BASE_X10 + atributo(atk, ATR_SOR) * CRITICO_POR_SOR_X10, 0, CRITICO_MAX_X10);
    return rng_faixa(c->rng, 0, 999) < chance;
}

static int dano_fisico(const lutador_t *atk, const lutador_t *def)
{
    int d = (atributo(atk, ATR_ATK) * 12 + atributo(atk, ATR_FOR) * 8 - atributo(def, ATR_DEF) * 7) / 10;
    return d < 1 ? 1 : d;
}

static int dano_habilidade(const lutador_t *atk, const lutador_t *def, int poder)
{
    int d = (poder * 10 + atributo(atk, ATR_INT) * 8 + atributo(atk, ATR_ATK) * 4 - atributo(def, ATR_DEF) * 5) / 10;
    return d < 1 ? 1 : d;
}

static void aplicar_estado(combate_t *c, lutador_t *alvo, uint8_t estado, int chance, resultado_t *r)
{
    if (estado < 32 && chance > 0 && rng_chance(c->rng, chance)) {
        alvo->estados |= BIT_ESTADO(estado);
        r->estado_aplicado = estado;
    }
}

void combate_executar(combate_t *c, int lado, acao_t acao, const registro_t *item, resultado_t *r)
{
    memset(r, 0, sizeof(*r));
    r->ator = lado;
    r->alvo = 1 - lado;
    r->tipo = acao.tipo;
    r->estado_aplicado = -1;
    r->multiplicador = 100;
    if (c->terminado) {
        return;
    }
    lutador_t *eu = &c->lado[lado], *outro = &c->lado[1 - lado];
    eu->atb_ms = 0;

    if (acao.tipo == ACAO_DESISTIR) {
        c->terminado = true;
        c->vencedor = 1 - lado;
        return;
    }

    /* Dano dos estados corrosivos no início do turno */
    static const estado_t corrosivos[] = {EST_ENVENENADO, EST_QUEIMADO, EST_CONGELADO};
    for (unsigned i = 0; i < 3; i++) {
        if (tem(eu, corrosivos[i])) {
            int d = eu->hp_max * DESG_HP_ESTADO_TURNO_PCT / 100;
            r->dano_estado += d < 1 ? 1 : d;
        }
    }
    eu->hp -= r->dano_estado;
    verificar_fim(c);
    if (c->terminado) {
        return;
    }

    if (tem(eu, EST_PARALISADO) || tem(eu, EST_CONGELADO)) {
        r->falhou = true;
        return;
    }

    if (acao.tipo == ACAO_ITEM) {
        if (!item || tem(eu, EST_AMALDICOADO)) {
            r->falhou = true;
            return;
        }
        snprintf(r->icone, sizeof(r->icone), "%s", item->icone);
        r->alvo = lado;
        for (int i = 0; i < item->n_efeitos; i++) {
            if (item->efeitos[i].alvo == VITAL_HP) {
                int antes = eu->hp;
                eu->hp = limitar(eu->hp + item->efeitos[i].valor, 0, eu->hp_max);
                r->cura += eu->hp - antes;
            } else if (item->efeitos[i].alvo == VITAL_MP) {
                eu->mp = limitar(eu->mp + item->efeitos[i].valor, 0, eu->mp_max);
            }
        }
        for (int i = 0; i < item->n_sobre; i++) {
            if (item->sobre[i].estado == EST_TODOS && item->sobre[i].valor <= -100) {
                eu->estados &= BIT_ESTADO(EST_ZANGADO);
            } else if (item->sobre[i].estado < 32 && item->sobre[i].valor < 0 && rng_chance(c->rng, -item->sobre[i].valor)) {
                eu->estados &= ~BIT_ESTADO(item->sobre[i].estado);
            }
        }
        return;
    }

    /* Confusão: 50% de errar; ao errar, 50% de atingir a si mesmo */
    if (tem(eu, EST_CONFUSO) && rng_chance(c->rng, 50)) {
        r->errou = true;
        if (rng_chance(c->rng, 50)) {
            r->auto_golpe = true;
            r->alvo = lado;
            r->dano = dano_fisico(eu, eu);
            eu->hp -= r->dano;
            verificar_fim(c);
        }
        return;
    }

    if (acao.tipo == ACAO_ATACAR) {
        snprintf(r->icone, sizeof(r->icone), "attack");
        if (esquivou(c, eu, outro)) {
            r->esquivou = true;
            return;
        }
        r->dano = dano_fisico(eu, outro);
        if (critico(c, eu)) {
            r->critico = true;
            r->dano = r->dano * 15 / 10;
        }
        outro->hp -= r->dano;
        verificar_fim(c);
        return;
    }

    /* Habilidade */
    registro_t h;
    if (tem(eu, EST_AMALDICOADO) || acao.indice < 0 || acao.indice >= QTD_HAB_ATIVAS ||
        eu->habilidades[acao.indice] == SLOT_VAZIO || !catalogo_ler(CAT_HABILIDADES, eu->habilidades[acao.indice], &h)) {
        r->falhou = true;
        return;
    }
    int custo = limitar(h.custo_mp - atributo(eu, ATR_INT) / 20, 1, 255);
    if (eu->mp < custo) {
        r->falhou = true;
        return;
    }
    eu->mp -= custo;
    snprintf(r->icone, sizeof(r->icone), "%s", h.icone);

    if (h.tipo == HAB_CURA) {
        lutador_t *alvo = h.alvo == ALVO_USUARIO ? eu : outro;
        r->alvo = h.alvo == ALVO_USUARIO ? lado : 1 - lado;
        int antes = alvo->hp;
        alvo->hp = limitar(alvo->hp + h.poder + atributo(eu, ATR_INT) / 2, 0, alvo->hp_max);
        r->cura = alvo->hp - antes;
        return;
    }

    lutador_t *alvo = h.alvo == ALVO_USUARIO ? eu : outro;
    r->alvo = h.alvo == ALVO_USUARIO ? lado : 1 - lado;
    if (alvo != eu && esquivou(c, eu, alvo)) {
        r->esquivou = true;
        return;
    }
    if (h.tipo == HAB_ATAQUE) {
        r->multiplicador = combate_multiplicador((natureza_t)h.natureza, (natureza_t)alvo->natureza);
        r->dano = dano_habilidade(eu, alvo, h.poder) * r->multiplicador / 100;
        if (r->dano < 1) {
            r->dano = 1;
        }
        if (critico(c, eu)) {
            r->critico = true;
            r->dano = r->dano * 15 / 10;
        }
        alvo->hp -= r->dano;
    }
    for (int i = 0; i < h.n_sobre; i++) {
        aplicar_estado(c, alvo, h.sobre[i].estado, h.sobre[i].valor, r);
    }
    verificar_fim(c);
}

acao_t combate_ia(combate_t *c, int lado)
{
    lutador_t *eu = &c->lado[lado];
    registro_t h;
    acao_t acao = {ACAO_ATACAR, 0};
    int cura = -1, ataques[QTD_HAB_ATIVAS], n_ataques = 0;
    for (int i = 0; i < QTD_HAB_ATIVAS; i++) {
        if (eu->habilidades[i] == SLOT_VAZIO || !catalogo_ler(CAT_HABILIDADES, eu->habilidades[i], &h)) {
            continue;
        }
        int custo = limitar(h.custo_mp - eu->atributos[ATR_INDICE(ATR_INT)] / 20, 1, 255);
        if (custo > eu->mp) {
            continue;
        }
        if (h.tipo == HAB_CURA) {
            cura = i;
        } else {
            ataques[n_ataques++] = i;
        }
    }
    if (cura >= 0 && eu->hp * 100 < eu->hp_max * 30) {
        acao.tipo = ACAO_HABILIDADE;
        acao.indice = cura;
    } else if (n_ataques && rng_chance(c->rng, 40)) {
        acao.tipo = ACAO_HABILIDADE;
        acao.indice = ataques[rng_faixa(c->rng, 0, n_ataques - 1)];
    }
    return acao;
}

uint32_t combate_xp(const combate_t *c, int lado_vencedor)
{
    return 10u + 5u * c->lado[1 - lado_vencedor].nivel;
}

int combate_drop(const combate_t *c, int lado_vencedor)
{
    int chance = DROP_BASE_PCT + c->lado[lado_vencedor].atributos[ATR_INDICE(ATR_SOR)] / 2;
    int n = catalogo_quantidade(CAT_ITENS);
    if (n == 0 || !rng_chance(c->rng, chance)) {
        return -1;
    }
    return rng_faixa(c->rng, 0, n - 1);
}
