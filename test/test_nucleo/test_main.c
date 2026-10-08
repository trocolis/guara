#include <string.h>
#include <unity.h>

#include "../comum/storage_teste.h"
#include "combate.h"
#include "guara.h"
#include "regras.h"
#include "save.h"

static rng_t rng;

void setUp(void) { rng_semear(&rng, 1234); }
void tearDown(void) {}

#define EXIGIR_STORAGE() \
    if (!carregar_storage_teste()) TEST_IGNORE_MESSAGE("rode tools/storage-packer/pack_storage.py")

/* ---------------- Storage ---------------- */

static void test_storage_catalogo_e_vrom(void)
{
    EXIGIR_STORAGE();
    TEST_ASSERT_EQUAL_INT(2, catalogo_quantidade(CAT_ITENS));
    registro_t maca;
    TEST_ASSERT_TRUE(catalogo_ler(CAT_ITENS, 0, &maca));
    TEST_ASSERT_EQUAL_STRING("teste_comida", maca.slug);
    TEST_ASSERT_EQUAL_STRING("Comida de teste", maca.nome);
    TEST_ASSERT_EQUAL_INT(3, maca.n_efeitos);
    TEST_ASSERT_EQUAL_INT(VITAL_ALIMENTACAO, maca.efeitos[0].alvo);
    TEST_ASSERT_EQUAL_INT(15, maca.efeitos[0].valor);
    TEST_ASSERT_EQUAL_INT(1, catalogo_buscar_slug(CAT_ITENS, "teste_pocao"));

    gfx_bitmap_t b;
    TEST_ASSERT_TRUE(vrom_buscar(maca.icone, &b));
    TEST_ASSERT_EQUAL_INT(16, b.largura);
    TEST_ASSERT_TRUE(vrom_buscar("bitart_logo", &b));
    TEST_ASSERT_EQUAL_INT(112, b.largura);
    TEST_ASSERT_FALSE(vrom_buscar("nao_existe", &b));
    TEST_ASSERT_FALSE(vrom_buscar("main_menu", &b)); /* mockups ficam fora da VROM */
}

/* ---------------- Save ---------------- */

static void test_save_ida_e_volta(void)
{
    guara_t g, lido;
    guara_nascer(&g, "Tico", &rng, 1700000000);
    g.medidores[MED_RAIVA] = -1234;
    g.inventario[63] = 200;
    g.hab_possui = 0x8000000000000001ull;
    uint8_t bloco[SAVE_TAMANHO];
    uint32_t contador;
    save_serializar(&g, 77, bloco);
    TEST_ASSERT_TRUE(save_desserializar(bloco, &lido, &contador));
    TEST_ASSERT_EQUAL_UINT32(77, contador);
    TEST_ASSERT_EQUAL_MEMORY(&g, &lido, sizeof(g));

    bloco[100] ^= 1;
    TEST_ASSERT_FALSE(save_desserializar(bloco, &lido, &contador));
}

typedef struct {
    uint8_t mem[ANEL_SETOR * ANEL_SETORES];
    int apagamentos[ANEL_SETORES];
} flash_falsa_t;

static bool f_ler(void *ctx, uint32_t off, void *buf, size_t n)
{
    memcpy(buf, ((flash_falsa_t *)ctx)->mem + off, n);
    return true;
}

static bool f_escrever(void *ctx, uint32_t off, const void *buf, size_t n)
{
    /* NOR flash: só transforma 1 em 0 */
    for (size_t i = 0; i < n; i++) {
        ((flash_falsa_t *)ctx)->mem[off + i] &= ((const uint8_t *)buf)[i];
    }
    return true;
}

static bool f_apagar(void *ctx, uint32_t off)
{
    flash_falsa_t *f = ctx;
    memset(f->mem + off, 0xFF, ANEL_SETOR);
    f->apagamentos[off / ANEL_SETOR]++;
    return true;
}

static void test_anel_rotaciona_e_nivela(void)
{
    static flash_falsa_t flash;
    memset(&flash, 0, sizeof(flash)); /* lixo: nenhum save válido */
    flash_ops_t ops = {&flash, f_ler, f_escrever, f_apagar};
    anel_t anel;
    guara_t g, lido;
    guara_nascer(&g, "Anel", &rng, 1);

    anel_iniciar(&anel, &ops);
    TEST_ASSERT_FALSE(anel_carregar(&anel, &lido));
    for (int i = 0; i < 40; i++) {
        g.xp = (uint32_t)i;
        TEST_ASSERT_TRUE(anel_salvar(&anel, &g));
    }
    /* 40 gravações: 5 voltas por setor */
    TEST_ASSERT_EQUAL_INT(3, flash.apagamentos[0]);
    TEST_ASSERT_EQUAL_INT(2, flash.apagamentos[1]);

    anel_t outro;
    anel_iniciar(&outro, &ops);
    TEST_ASSERT_TRUE(anel_carregar(&outro, &lido));
    TEST_ASSERT_EQUAL_UINT32(39, lido.xp);
    TEST_ASSERT_EQUAL_UINT32(40, outro.contador);
}

/* ---------------- Ciclo de vida ---------------- */

static void test_nascimento(void)
{
    guara_t g;
    guara_nascer(&g, "NomeMuitoComprido", &rng, 1);
    TEST_ASSERT_EQUAL_STRING("NomeMuitoC", g.nome);
    TEST_ASSERT_EQUAL_INT(1, g.nivel);
    TEST_ASSERT_TRUE(g.natureza <= NAT_AR);
    TEST_ASSERT_TRUE(g.cabeca >= 1 && g.cabeca <= 9);
    TEST_ASSERT_EQUAL_INT(g.hp_max, g.hp);
}

static void test_habilidades_iniciais(void)
{
    EXIGIR_STORAGE();
    guara_t g;
    guara_nascer(&g, "Hab", &rng, 1);
    TEST_ASSERT_TRUE(g.hab_possui & 1u); /* primeira compatível (neutra) */
    TEST_ASSERT_EQUAL_INT(0, g.hab_ativas[0]);
    TEST_ASSERT_EQUAL_INT(g.natureza == NAT_FOGO, (int)((g.hab_possui >> 1) & 1u));
    for (int i = 0; i < QTD_ITENS; i++) {
        TEST_ASSERT_EQUAL_INT(0, g.inventario[i]); /* sem kit inicial definido */
    }
}

static void test_fome_e_desmaio(void)
{
    guara_t g;
    guara_nascer(&g, "Fome", &rng, 1);
    relogio_ctx_t rel = {-1, 20 * 60, 8 * 60};
    g.alimentacao = CENTI(10);
    guara_minuto(&g, &rel, false);
    TEST_ASSERT_TRUE(guara_tem(&g, EST_FAMINTO));
    TEST_ASSERT_TRUE(g.hp < g.hp_max);
    for (int i = 0; i < 200 && !guara_tem(&g, EST_DESMAIADO); i++) {
        guara_minuto(&g, &rel, false);
    }
    TEST_ASSERT_TRUE(guara_tem(&g, EST_DESMAIADO));
    TEST_ASSERT_EQUAL_INT(0, g.hp);
}

static void test_recuperacao_tem_travas(void)
{
    guara_t g;
    guara_nascer(&g, "Longe", &rng, 1000);
    uint32_t sim = guara_recuperar_tempo(&g, 1000 + 3 * 24 * 3600, 12 * 60, 20 * 60, 8 * 60);
    TEST_ASSERT_EQUAL_UINT32(3 * 24 * 60, sim);
    TEST_ASSERT_FALSE(guara_tem(&g, EST_DESMAIADO));
    TEST_ASSERT_TRUE(g.hp >= g.hp_max * TRAVA_HP_PCT / 100);
    TEST_ASSERT_TRUE(g.alimentacao >= TRAVA_VITAL);
    TEST_ASSERT_EQUAL_UINT32(1000 + 3 * 24 * 3600, g.timestamp);
}

static void test_dorme_na_hora_e_acorda(void)
{
    guara_t g;
    guara_nascer(&g, "Sono", &rng, 1);
    g.alimentacao = VITAL_MAX;
    relogio_ctx_t rel = {20 * 60, 20 * 60, 8 * 60};
    guara_minuto(&g, &rel, false);
    TEST_ASSERT_TRUE(guara_tem(&g, EST_DORMINDO));
    g.energia = CENTI(60);
    rel.minuto_do_dia = 8 * 60;
    guara_minuto(&g, &rel, false);
    TEST_ASSERT_FALSE(guara_tem(&g, EST_DORMINDO));
}

static void test_afinidade_opostos_e_nascimento(void)
{
    guara_t g;
    guara_nascer(&g, "Afin", &rng, 1);
    g.natureza = g.natureza_nascimento = NAT_FOGO;
    guara_afinidade(&g, NAT_AGUA, CENTI(30));
    guara_afinidade(&g, NAT_FOGO, CENTI(10)); /* vai para o neutro */
    TEST_ASSERT_EQUAL_INT(CENTI(30), g.afinidades[NAT_AGUA]);
    TEST_ASSERT_EQUAL_INT(0, g.afinidades[NAT_FOGO]);
    TEST_ASSERT_EQUAL_INT(CENTI(10), g.afinidades[NAT_NEUTRA]);
    guara_afinidade(&g, NAT_TERRA, CENTI(5));
    guara_afinidade(&g, NAT_AR, CENTI(2));
    TEST_ASSERT_EQUAL_INT(CENTI(3), g.afinidades[NAT_TERRA]);
}

static void test_evolucao_vapor(void)
{
    guara_t g;
    guara_nascer(&g, "Evo", &rng, 1);
    g.natureza = g.natureza_nascimento = NAT_FOGO;
    guara_afinidade(&g, NAT_AGUA, CENTI(60));
    uint32_t total = 0;
    for (int n = 1; n < 15; n++) {
        total += guara_xp_proximo((uint8_t)n);
    }
    g.idade_min = 10 * 1440; /* sem bônus de idade */
    guara_ganhar_xp(&g, total, &rng);
    TEST_ASSERT_EQUAL_INT(15, g.nivel);
    TEST_ASSERT_EQUAL_INT(ESTAGIO_JOVEM_B, g.estagio);
    TEST_ASSERT_EQUAL_INT(NAT_VAPOR, g.natureza);
    TEST_ASSERT_FALSE(guara_afinidade_ativa(&g));
}

static void test_item_cura_e_bloqueios(void)
{
    EXIGIR_STORAGE();
    guara_t g;
    registro_t pocao, maca;
    guara_nascer(&g, "Item", &rng, 1);
    catalogo_ler(CAT_ITENS, catalogo_buscar_slug(CAT_ITENS, "teste_pocao"), &pocao);
    catalogo_ler(CAT_ITENS, 0, &maca);
    g.inventario[pocao.id] = 1;
    g.inventario[maca.id] = 1;
    g.hp = 1;
    TEST_ASSERT_EQUAL_INT(USO_OK, guara_usar_item(&g, &pocao, &rng));
    TEST_ASSERT_EQUAL_INT(g.hp_max < 31 ? g.hp_max : 31, g.hp);
    TEST_ASSERT_EQUAL_INT(USO_SEM_ESTOQUE, guara_usar_item(&g, &pocao, &rng));

    guara_por_estado(&g, EST_AMALDICOADO, true);
    TEST_ASSERT_EQUAL_INT(USO_BLOQUEADO_MALDICAO, guara_pode_usar_item(&g, &maca));
    guara_por_estado(&g, EST_AMALDICOADO, false);
    guara_por_estado(&g, EST_DESMAIADO, true);
    g.desmaio_min = 5;
    TEST_ASSERT_EQUAL_INT(USO_BLOQUEADO_DESMAIADO, guara_pode_usar_item(&g, &maca));
}

static void test_regra_versao_desenvolvimento(void)
{
    EXIGIR_STORAGE();
    TEST_ASSERT_TRUE(guara_versao_desenvolvimento("0.1.0"));
    TEST_ASSERT_TRUE(guara_versao_desenvolvimento("0.12.3"));
    TEST_ASSERT_FALSE(guara_versao_desenvolvimento("1.0.0"));
    TEST_ASSERT_FALSE(guara_versao_desenvolvimento("10.0.0"));
    guara_t g;
    guara_nascer(&g, "Dev", &rng, 1);
    guara_liberar_catalogo(&g);
    TEST_ASSERT_EQUAL_INT(255, g.inventario[0]);
    TEST_ASSERT_EQUAL_INT(255, g.inventario[1]);
    TEST_ASSERT_EQUAL_INT(0, g.inventario[2]); /* fora do catálogo de teste */
    TEST_ASSERT_EQUAL_HEX64(0x1ull, g.equip_possui);
    TEST_ASSERT_EQUAL_HEX64(0x3ull, g.hab_possui);
}

/* ---------------- Combate ---------------- */

static void test_formulas_combate(void)
{
    TEST_ASSERT_EQUAL_INT(5750, combate_tempo_atb(5));
    TEST_ASSERT_EQUAL_INT(1500, combate_tempo_atb(90));
    TEST_ASSERT_EQUAL_INT(MULT_VANTAGEM, combate_multiplicador(NAT_AGUA, NAT_FOGO));
    TEST_ASSERT_EQUAL_INT(MULT_DESVANTAGEM, combate_multiplicador(NAT_FOGO, NAT_AGUA));
    TEST_ASSERT_EQUAL_INT(MULT_VANTAGEM, combate_multiplicador(NAT_FOGO, NAT_AR));
    /* Vapor (Fogo + Água) não sofre a fraqueza do Fogo contra Água */
    TEST_ASSERT_EQUAL_INT(100, combate_multiplicador(NAT_AGUA, NAT_VAPOR));
    TEST_ASSERT_EQUAL_INT(100, combate_multiplicador(NAT_NEUTRA, NAT_FOGO));
    TEST_ASSERT_EQUAL_INT(MULT_VANTAGEM, combate_multiplicador(NAT_LUZ, NAT_SOMBRA));
}

static void test_combate_termina(void)
{
    guara_t g;
    guara_nascer(&g, "Luta", &rng, 1);
    lutador_t eu, op;
    combate_lutador_de_guara(&g, &eu);
    combate_gerar_oponente(&g, &rng, &op);
    combate_t c;
    combate_iniciar(&c, &eu, &op, &rng);
    resultado_t r;
    for (int passo = 0; passo < 10000 && !c.terminado; passo++) {
        unsigned prontos = combate_atualizar(&c, 100);
        for (int lado = 0; lado < 2; lado++) {
            if (prontos & (1u << lado)) {
                combate_executar(&c, lado, combate_ia(&c, lado), NULL, &r);
            }
        }
    }
    TEST_ASSERT_TRUE(c.terminado);
    TEST_ASSERT_TRUE(c.vencedor == 0 || c.vencedor == 1);
    TEST_ASSERT_EQUAL_INT(0, c.lado[1 - c.vencedor].hp);
}

static void test_desistir(void)
{
    guara_t g;
    guara_nascer(&g, "Fuga", &rng, 1);
    lutador_t eu, op;
    combate_lutador_de_guara(&g, &eu);
    combate_gerar_oponente(&g, &rng, &op);
    combate_t c;
    resultado_t r;
    combate_iniciar(&c, &eu, &op, &rng);
    combate_executar(&c, 0, (acao_t){ACAO_DESISTIR, 0}, NULL, &r);
    TEST_ASSERT_TRUE(c.terminado);
    TEST_ASSERT_EQUAL_INT(1, c.vencedor);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_storage_catalogo_e_vrom);
    RUN_TEST(test_save_ida_e_volta);
    RUN_TEST(test_anel_rotaciona_e_nivela);
    RUN_TEST(test_nascimento);
    RUN_TEST(test_habilidades_iniciais);
    RUN_TEST(test_fome_e_desmaio);
    RUN_TEST(test_recuperacao_tem_travas);
    RUN_TEST(test_dorme_na_hora_e_acorda);
    RUN_TEST(test_afinidade_opostos_e_nascimento);
    RUN_TEST(test_evolucao_vapor);
    RUN_TEST(test_item_cura_e_bloqueios);
    RUN_TEST(test_regra_versao_desenvolvimento);
    RUN_TEST(test_formulas_combate);
    RUN_TEST(test_combate_termina);
    RUN_TEST(test_desistir);
    return UNITY_END();
}
