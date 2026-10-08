# Catálogo

Nessa catálogo são definidos os itens, equipamento e habilidades, aqui é definido as carateristicas do objeto de catálogo.

## Itens

Os itens são objetos consumiveis, ao ser usado é decrementado a quantidade no inventário, a estrutura é:
- ID (inteiro não sinalizado)
- Icone (referência a VROM)
- Nome (texto de 20 caracteres)
- Tipo (comida, bebida e poção/remédio)
- Efeitos (Almenta oyu diminui um status: HP, MP, Energia, Humor, Alimentação) (Pode ter mais de 1)
- Sobre efeito (Diminui, aumenta ou cura um estado, exemplo: CONGELADO +5%, ENVENENADO -100% -> cura)
- Afinidade (Fogo, Terra, Água, Ar e Neutro)
- Descrição (texto de 40 caracteres)

### Comidas

1. Maçã
	- Icone: ![[apple.bmp]]
	- Efeito: Alimentação +15, HP +5, Energia +10
	- **Descrição:** Maca fresca, crocante e rica em fibras.

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