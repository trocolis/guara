# Game Design — valores e decisões provisórias

O README define as regras do Guará, mas deixa vários números e comportamentos em aberto. Este arquivo registra o que o firmware implementa nesses pontos, para revisão. Os valores numéricos estão em `lib/core/src/rules.h`. Quando algo for decidido de outro jeito, atualize os dois lugares e marque o item no backlog de `docs/PLAN.md`.

Legenda: **PROVISÓRIO** = valor escolhido na implementação, não vem do README.

## Unidades

- Energia, Humor, Alimentação e Saúde são guardados em centésimos de ponto percentual (`10000` = 100%) para permitir desgaste lento.
- HP e MP são pontos absolutos.
- O ciclo de vida avança **um passo por minuto real** (`guara_tick_minute`).

## Desgaste por minuto (PROVISÓRIO)

| Vital | Regra | Valor |
|---|---|---|
| Alimentação | sempre (metade dormindo) | −0,15%/min (≈ 11 h de 100% a 0%) |
| Energia | acordado | −0,10%/min (≈ 16 h) |
| Energia | dormindo | +0,50%/min (≈ 3 h) |
| Humor | energia ou alimentação < 50% | −0,20%/min |
| Humor | energia e alimentação > 40% | +0,10%/min |
| Humor | sem interação há mais de 2 h | −0,10%/min |
| Saúde | energia ou alimentação < 20% | −0,10%/min |
| Saúde | alimentação, energia e humor > 50% | +0,10%/min (Feliz: +20%) |
| HP | Faminto | −1% do HP máximo/min |
| HP e Saúde | Envenenado, Queimado, Congelado | −2% do HP máximo/min e −1%/min de Saúde |

- **Multiplicador de desgaste:** `150% − Saúde` (Saúde 100% desgasta a 50%, Saúde 0% a 150%), +20% se Triste e +5% se Entediado. Isso dá efeito à Saúde (**I17**).
- **I7 resolvido:** os "2% por segundo" do README viraram **2% por minuto**. Por segundo, o Guará desmaiaria em 50 s, o que contradiz o desgaste gradual.

## Estados

- **Faminto:** alimentação ≤ 10% (**I6**: só a alimentação; a energia baixa causa *Cansado*).
- **Cansado:** energia < 20% e acordado. É o "estado de sono" do README (**I8**).
- **Dormindo:**
  - Começa quando a energia chega a 0% sem fome, ou no minuto da hora de dormir.
  - Termina com a energia em 100%, na hora de acordar (se a energia passar de 50%) ou se ficar com fome.
  - Comer acorda o Guará, mas ele volta a dormir se a energia não passar de 50%.
- **Desmaiado:**
  - Ocorre com HP 0. Bloqueia tudo por 10 min (README); depois disso, aceita itens que curam HP.
  - **PROVISÓRIO:** acorda sozinho após 60 min com 10% do HP.
  - Desmaiar cura veneno, queimadura, gelo, paralisia, confusão e maldição.
- **Amaldiçoado:** cura com vitória, desmaio ou ao tocar uma música (README).
- **Emoções** (só uma por vez):
  - **Feliz:** os 4 vitais acima de 80%.
  - **Humor < 30%:** a emoção vem do medidor Tristeza × Raiva. Até ±2000 fica *Entediado* ("chateado" no README; **I8**), acima de +2000 fica *Zangado* e abaixo de −2000 fica *Triste*.
  - **Entediado** também aparece após 2 h sem interação.

### Exibição (I11, I12)

- **Balão:**
  - Estados físicos aparecem sempre.
  - Emoções negativas aparecem por 5–30 s a cada 20–30 min.
  - Feliz aparece junto com o olhar feliz: 5–10 s a cada 20–30 min, se todos os vitais estiverem acima de 80%.
- **Olhos:**

  | Situação | Olho |
  |---|---|
  | Desmaiado | `fainted` |
  | Dormindo | `closed` |
  | Durante a animação de comer, logo após cada mordida (225 ms) | `happy` |
  | Logo após comer ou usar item | `wink` |
  | Veneno, queimadura ou gelo | `scared` |
  | Fome, cansaço, confusão ou paralisia | `dazed` |
  | Zangado | `anger` |
  | Triste ou entediado | `bored` |
  | Piscada a cada 3–6 s | `closed` |

- **Animação do corpo:** quadros 1, 2 e 3 a 3 FPS; os quadros 2 e 3 usam o bitmap `_2` do corpo (a sequência "1, 2, 2" do README). Os olhos ficam 1 px abaixo do topo do quadro 1 nos quadros 1 e 2 e sobem 1 px no quadro 3.

## Recuperação temporal (Δt)

- Simula no máximo **7 dias** (PROVISÓRIO).
- **Travas:** durante o Δt, o HP não cai abaixo de 20% e os vitais não caem abaixo de 5%.

## Medidores ocultos (faixa ±10000)

| Medidor | Movimento |
|---|---|
| Vitórias × Derrotas | ±300 por combate ou resultado de minijogo |
| Cuidado × Negligência | +2/min com tudo acima de 50%; −2/min com fome, cansaço, humor < 20% ou saúde < 20%; −500 ao desmaiar; +100 ao comer |
| Tristeza × Raiva | negligência move +2/min para Raiva; cada derrota move −300 para Tristeza |
| Dia × Noite | ±50 por interação na janela de 2 h após acordar ou antes de dormir; fora delas volta 25 para o equilíbrio |

## Afinidades

- **Ganho por comida ou bebida:** +1% na afinidade do item (PROVISÓRIO).
- **Opostos:** ganhar pontos num elemento tira a mesma quantidade do oposto (fogo × água, terra × ar).
- **Natureza de nascimento:** os pontos do elemento de nascimento vão para o Neutro (README). Por isso Vapor (Fogo + Água) e Cristal (Terra + Ar) são alcançáveis (**I15**).
- **Desativação:** o sistema de afinidade desliga ao adquirir uma natureza secundária.

## Progressão (PROVISÓRIO)

| Item | Regra |
|---|---|
| XP para o próximo nível | `20 × n^1,5` (nível 1: 20; nível 10: 632; nível 50: 7071) |
| Bônus de idade no XP | 125% até 3 dias, 100% até 30 dias, 90% depois |
| HP máximo | `30 + 8 × nível + DEF` |
| MP máximo | `10 + 3 × nível + INT/2` |
| Atributos iniciais | 5 cada; +2 nos dois da natureza (Fogo: ATK/FOR, Terra: DEF/FOR, Água: INT/DEF, Ar: AGI/SOR) |
| Por nível | +0..1 em cada atributo, +1 extra nos dois da natureza |
| Estágios | bebê 1, jovem A 5, jovem B 15, jovem C 30, adulto 50 (`yong_a/b/c` tratados como estágios sequenciais) |
| Habilidades | a cada 5 níveis aprende a próxima habilidade compatível do catálogo (mesma natureza, elemento em comum ou Neutra) e ativa se houver vaga |

### Evolução da natureza

Ocorre ao entrar nos estágios jovem B, jovem C e adulto.

- **Fusão:** acontece se a maior afinidade for um elemento diferente do de nascimento e tiver ≥ 40%. O resultado é a fusão nascimento + elemento.
- **Maior afinidade Neutra:**
  - Dia × Noite ≥ +6000 → **Luz**;
  - Dia × Noite ≤ −6000 → **Sombra**;
  - INT ≥ 1,5 × o maior outro atributo → **Psíquica**;
  - nenhum dos casos acima, já no estágio adulto → **Neutra**.

O módulo Lua `guara.add_affinity("mind", n)` soma pontos em INT, porque os "pontos mentais" não existem em outro lugar (**I16**).

## Combate

As fórmulas de ATB, dano físico, crítico, esquiva e multiplicador elemental seguem o README. Complementos PROVISÓRIOS:

- **Ordem de aplicação:** esquiva → dano base → elemento (só habilidades) → crítico (×1,5).
- **Dano de habilidade:** `max(1, poder + INT×0,8 + ATK×0,4 − DEF×0,5)`.
- **Custo de MP:** `max(1, custo − INT/20)`. O custo está no catálogo (coluna `mp_cost`).
- **Cura:** `poder + INT/2`.
- **Zangado:** ATK ×1,5 e DEF ×0,5 (README).
- **Cansado:** −30% em todos os atributos (README), fora e dentro do combate.
- **Estados em combate:**
  - Paralisado e Congelado perdem a vez.
  - Confuso tem 50% de errar; ao errar, 50% de atingir a si mesmo.
  - Veneno, queimadura e gelo tiram 5% do HP máximo por turno.
  - Amaldiçoado não usa habilidades nem itens.
- **Elementos:**
  - Vantagem +25% e desvantagem −25% pelo ciclo Fogo < Água < Terra < Ar < Fogo.
  - Naturezas compostas usam os dois elementos, e a fraqueza é neutralizada quando o defensor também contém o elemento atacante (exemplo do Vapor no README).
  - Luz e Sombra têm vantagem mútua.
  - Neutra e Psíquica são neutras contra tudo.
- **Reação no tempo certo (janela de A):**
  - Golpes com janela: ataque físico e habilidades (exceto cura) contra o outro lado.
  - O golpe tem 600 ms de preparo; a janela são os últimos 250 ms. Uma tentativa por golpe: apertar antes da janela perde a reação.
  - **Golpe certeiro (quem ataca):** +20% de chance de crítico (somado depois do limite de 50%) e, só no ataque físico, 30% de chance de ataque duplo (o segundo golpe sorteia o próprio crítico).
  - **Defesa (quem é atacado):** dano ×0,5 (mínimo 1) e +15% de chance de o atacante errar, sorteada depois da esquiva.
  - O oponente acerta a janela com 25% de chance, tanto para atacar quanto para se defender.
  - Ordem de aplicação: esquiva → erro da defesa → dano base → elemento → crítico → defesa.
- **Escolha antecipada:** a ação pode ser escolhida com a barra ATB incompleta e só executa quando ela enche. Depois de escolhida, não muda até o próximo ciclo (A não abre o modal); desistir vale na hora. O tempo para enquanto o modal de ações está aberto.
- **IA do oponente:** cura com HP < 30% (se tiver habilidade e MP); senão, 40% de chance de usar uma habilidade de ataque; senão, ataca.
- **Treino:**
  - Oponente com o mesmo nível e atributos ±10%.
  - O HP e os estados do combate voltam para o Guará, com HP mínimo de 1.
  - Cada luta custa 5% de energia.
  - **Vitória:** 10 + 5 × nível do oponente em XP; drop com chance de 10% + SOR/2 de um item aleatório.
  - **Derrota:** move o medidor para Tristeza.

## Itens

- **Sobre efeito:** −100 cura o estado; valor negativo é a chance de curar; valor positivo é a chance de causar. `ALL-100` cura todos os estados físicos, exceto dormir e desmaio.
- **Kit inicial:** a definir junto com o catálogo. Hoje o Guará nasce sem itens nem equipamentos; ganha só a primeira habilidade compatível do catálogo e o primeiro ataque da sua natureza, se existirem.
- **Catálogo:** o conteúdo (itens, equipamentos, habilidades) é definido pelo autor em `CATALOG.md`. Os arquivos `catalog/*.csv`, lidos pelo storage-packer, estão vazios até lá. Os testes usam um catálogo próprio em `test/fixtures/catalog/`.

## Controles não definidos no README

| Onde | Comando |
|---|---|
| Tela principal | **A** abre o menu |
| Habilidades | **A** mostra os detalhes; **←/→** ativa ou desativa (máximo 4 ativas) |
| Cartão SD | **A** abre a pasta ou os comandos do arquivo; **←/→** abre os comandos também para pastas; **B** sobe um nível |
| Copiar/Mover (Cartão SD) | aparece "Colar aqui" no topo da lista de destino |
| Músicas (tocador) | **↑/↓** troca de faixa, **←/→** ajusta o volume (3 níveis) |
| Jogos | segure **B** por 2 s (ou chame `exit()` no Lua) para sair |
| Visualizador de imagens | direcional rola imagens maiores que a tela |
| Teste de controle | segure **B** por 1 s para sair |
