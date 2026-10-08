#ifndef GUARA_TIPOS_H
#define GUARA_TIPOS_H

/*
 * Enumerações compartilhadas entre o firmware e o storage-packer.
 * Manter em sincronia com as listas de tools/storage-packer/pack_storage.py.
 */

/* Alvos de efeito de itens e equipamentos: vitais (0..5) e atributos (6..11). */
typedef enum {
    VITAL_HP,
    VITAL_MP,
    VITAL_ENERGIA,
    VITAL_HUMOR,
    VITAL_ALIMENTACAO,
    VITAL_SAUDE,
    ATR_ATK,
    ATR_DEF,
    ATR_FOR,
    ATR_AGI,
    ATR_INT,
    ATR_SOR,
} alvo_efeito_t;

#define QTD_ATRIBUTOS 6
#define ATR_INDICE(a) ((int)(a) - (int)ATR_ATK)

typedef enum {
    NAT_FOGO,
    NAT_TERRA,
    NAT_AGUA,
    NAT_AR,
    NAT_NEUTRA,
    NAT_ELETRICIDADE,
    NAT_PLANTA,
    NAT_GELO,
    NAT_VAPOR,
    NAT_CRISTAL,
    NAT_SOMBRA,
    NAT_LUZ,
    NAT_PSIQUICA,
    NAT_LAVA,
    QTD_NATUREZAS,
} natureza_t;

/* As afinidades usam os 5 primeiros valores de natureza_t. */
#define QTD_AFINIDADES 5

/* Estados: bit N do campo `estados` do Guará. */
typedef enum {
    EST_DORMINDO = 0,
    EST_DESMAIADO = 1,
    EST_ENVENENADO = 2,
    EST_PARALISADO = 3,
    EST_QUEIMADO = 4,
    EST_CONGELADO = 5,
    EST_CONFUSO = 6,
    EST_AMALDICOADO = 7,
    EST_CANSADO = 8,
    EST_FAMINTO = 9,
    EST_FELIZ = 16,
    EST_TRISTE = 17,
    EST_ZANGADO = 18,
    EST_ENTEDIADO = 19,
    EST_TODOS = 255, /* só no catálogo: "cura todos os estados" */
} estado_t;

#define BIT_ESTADO(e) (1u << (e))
#define MASCARA_FISICOS 0x0000FFFFu
#define MASCARA_EMOCIONAIS 0xFFFF0000u

typedef enum { ITEM_COMIDA, ITEM_BEBIDA, ITEM_POCAO } tipo_item_t;

typedef enum { EQ_CABECA, EQ_CORPO, EQ_MAO_ATK, EQ_MAO_DEF, EQ_PES, EQ_ACESSORIO } tipo_equip_t;

/* Os 7 slots da tela de Equipamentos (dois de acessório). */
#define QTD_SLOTS_EQUIP 7

typedef enum { HAB_ATAQUE, HAB_CURA, HAB_EFEITO } tipo_hab_t;
typedef enum { ALVO_USUARIO, ALVO_OPONENTE } alvo_hab_t;

typedef enum { COR_PRETO = 0, COR_BRANCO = 1 } cor_guara_t;

/* Estágios de evolução (sprites baby, yong_a, yong_b, yong_c, adult_a). */
typedef enum { ESTAGIO_BEBE, ESTAGIO_JOVEM_A, ESTAGIO_JOVEM_B, ESTAGIO_JOVEM_C, ESTAGIO_ADULTO, QTD_ESTAGIOS } estagio_t;

/* Medidores ocultos (positivo = primeiro lado do nome). */
typedef enum { MED_VITORIAS, MED_CUIDADO, MED_RAIVA, MED_DIA, QTD_MEDIDORES } medidor_t;

#endif
