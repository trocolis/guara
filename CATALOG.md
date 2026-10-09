# Catálogo

Nessa catálogo são definidos os itens, equipamento e habilidades, aqui é definido as caraterísticas do objeto de catálogo.

**Pontos**
- HP -> Pontos de vida
- MP -> Pontos de magia
- EP -> Pontos de energia
- HP -> pontos de humor
- FP -> Pontos de Alimentação

**Estados Fisicos**
- SLP - Dormindo
- PSN - Envenenado
- PRL - Paralizado
- FRZ - Congelado
- CNF - Confuso
- CRS - Amaldiçoado
- FND - Cançado
- HGR - Faminto

**Estados Emocionais**
- HPY - Feliz
- SAD - Triste
- ANG - Zangado
- BRD - Entediado

**Naturezas**
- FRE - Fogo
- EAT - Terra
- WTR - Água
- AIR - Ar
- NTR - Neutro

## Itens

Os itens são objetos consumíveis, ao ser usado é decrementado a quantidade no inventário, a estrutura é:
- ID (inteiro não sinalizado)
- Ícone (referência a VROM)
- Nome (texto de 20 caracteres)
- Tipo (comida, bebida e poção/remédio)
- Efeitos Aumenta ou diminui  a pontuação (HP+10, EP+30 ) 
- Estado: Probabilidade de cura em %, (FRZ+5, PSN-100)
- Afinidade: Aumenta 1 ponto de afinidade: (FRE, EAT)
- Descrição (texto de 40 caracteres)

### Comidas

1. Maçã
	- Ícone: ![](./bitmaps/icons/apple.bmp)
	- Efeito: FP+15, HP+5, EP+10
	- Estado: HGR-25%
	- **Descrição:** Maca fresca, crocante e rica em fibras.
2. Banana
3.  Abacate
	- Ícone: ![[avocado.bmp]]
	- Efeito: Alimentação +32, Humor +10, Energia
4. 

> TODO: Criar um catálogo de 64 itens.

## Equipamentos

Os equipamentos serveme para aprimora o reduzir atributos, não são consúmíves, podeden ser equipado no slot apropriado eme Equpamento, u slote de um tipo, pode comportat apenas um por vez, sua estrura é:
- ID (inteiro não sinalizado)
- Icone (referência a VROM)
- Nome (texto de 20 caracteres)
- Tipo (cabeça, corpo, mão ATK, mão DEF, pés, acessório 1 e acessório 2)
- Efeitos (Almenta ou diminui um atributio: ATK, DEF, FOR, AGI, INT, SOR) (Pode ter mais de 1)
- Sobre efeito (Diminui ou aumenta a probabilidade de a um estado: Exemplo: ENVENENADO -20%)
- Afinidade (Fogo, Terra, Água, Ar e Neutro)
- Descrição (texto de 40 caracteres)

> TODO: Criar um catálogo de 64 itens.
## Habilidades

Aqui são descritas as habilidades possíve de se encinar a um gurá, elas são gerenciadas no menu Habilidades, sua estrutura é:
- ID (inteiro não sinalizado)
- Icone (referência a VROM)
- Nome (texto de 20 caracteres)
- Tipo (Ataque, cura ou efeito)
- Alvo (Quem a habilidade afeta, usuário ou oponente)
- Atributos (dano, cura)
- Sobre efeito (Diminui ou aumenta a probabilidade de  um estado: Exemplo: ENVENENADO +20%)
- Natureza (Determina qual natureza é compatível: Fogo, Terra, Água, Ar e Neutro)
- Descrição (texto de 40 caracteres)

> TODO: Criar um catálogo de 64 itens.