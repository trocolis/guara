# Plano de Implementação — Guará

Plano em fases para sair da especificação (`README.md`) até o firmware completo. Cada fase tem um entregável verificável e lista os itens do **Backlog de correções** (fim deste arquivo) que precisam estar resolvidos antes ou durante ela. As correções do README são feitas aos poucos, fase a fase, e marcadas aqui.

Legenda: `[ ]` pendente · `[x]` feito · ✅ decidido/adotado · ⏳ pendente

**Convenção de lados:** "direita"/"esquerda" são sempre do ponto de vista do aparelho (a direita do aparelho é a esquerda do usuário). Coordenadas de pixel são de tela, com X=0 à esquerda do usuário.

## Status geral (2026-10-08)

| Fase | Status |
|---|---|
| 0 — Decisões | ✅ exceto D7 e D10 (hardware) |
| 1 — Esqueleto | ✅ |
| 2 — HAL | ✅ display, botões, buzzer, bateria, SD, relógio, deep sleep. Medir FPS e consumo no hardware |
| 3 — Storage | ✅ packer v2, VROM, catálogo, save em anel, NVS, `upload_storage` |
| 4 — UI | ✅ componentes, modais, teclado, pilha de telas |
| 5 — Núcleo do pet | ✅ com valores provisórios (docs/GAME_DESIGN.md) |
| 6 — Menus | ✅ |
| 7 — Combate | ✅ treino; combate online pendente (Bluetooth) |
| 8 — Mídia | ✅ músicas MIDI e biblioteca |
| 9 — Lua | ✅ VM 5.4, APIs do README, exemplos em examples/sdcard (apple_catch e boitata) |
| 10 — Conectividade | ⏳ Wi-Fi sob demanda, NTP automático e OTA pelo SD prontos; Bluetooth e GitHub pendentes |

Gravado e verificado no NodeMCU do protótipo: boot, storage, cartão SD (10 MHz), nascimento, tela principal, carrossel e lista de músicas. Sem o DS3231 ligado, a hora fica `--:--` até um ajuste manual ou NTP.

---

## Fase 0 — Decisões de base

Decisões caras de mudar depois. Nenhum código de firmware antes delas.

| ID | Decisão | Recomendação |
|---|---|---|
| D1 | Fonte de tempo (H1) | ✅ **Decidido:** RTC externo DS3231 no I2C já existente (endereço 0x68, não colide com o SH1106 em 0x3C). Firmware abstrai a fonte de tempo (`clock`): DS3231 se presente, senão RTC interno + NTP. |
| D2 | Framework | ✅ **Decidido:** ESP-IDF 6.1 puro via PlatformIO (`framework = espidf`, `espressif32@7.1.3` fixada). Drivers próprios e simples (SH1106 é um framebuffer de 1 KB), sem o peso do Arduino na partição de 1472 KB. |
| D3 | Versão do Lua | ✅ **Adotado (padrão):** Lua 5.4 embutido como componente. |
| D4 | Onde ficam os saves (H4) | ✅ **Decidido:** Partição própria `save` (8 KB, subtipo `0x9A`). A `storage` vira somente leitura e pode ser regravada sem perder o Guará. |
| D5 | Onde ficam as configurações (H5) | ✅ **Adotado (padrão):** NVS: Wi-Fi, mudo, timeout de sleep, hora de dormir, fuso. |
| D6 | Detecção de USB/carga (H3) | ✅ **Decidido:** pino CHRG do TP4056 → GPIO35 (só entrada, precisa de pull-up externo, pois o GPIO35 não tem pull-up interno). |
| D7 | Consumo em deep sleep (H2, H7) | ⏳ **Pendente:** Aceitar o consumo real e corrigir o README, ou adicionar corte de energia dos periféricos (MOSFET) e divisor chaveado. Confirmar se o S09 é buck ou buck-boost. |
| D8 | Formato gráfico na VROM | ✅ **Adotado (padrão):** O packer converte os BMPs para 1 bpp compactado em linhas (row-major, sem cabeçalho BMP). O firmware desenha direto no framebuffer, sem converter nada em runtime. |
| D9 | Transparência 1-bit | ✅ **Adotado (padrão):** Modos de desenho `COPY`, `OR` (só pixels brancos), `AND` (pixels pretos apagam) e `XOR`. A composição cabeça+corpo+olhos e a mordida usam esses modos, com máscara opcional por sprite (`<nome>_mask.bmp`). |
| D10 | Despertar por alarme do DS3231 | ⏳ **Pendente:** O timer de deep sleep do ESP32 usa o oscilador RC interno e pode atrasar despertadores longos em minutos. **(R)** Ligar o pino INT/SQW do DS3231 ao GPIO4 (pino RTC livre, com pull-up) e acordar pelo alarme do próprio DS3231. |

Tabela de partições proposta (4 MB):

| Nome | Tipo | Subtipo | Offset | Tamanho |
|---|---|---|---|---|
| nvs | data | nvs | 0x9000 | 20 KB |
| otadata | data | ota | 0xE000 | 8 KB |
| ota_0 | app | ota_0 | 0x10000 | 1472 KB |
| ota_1 | app | ota_1 | 0x180000 | 1472 KB |
| storage | data | 0x99 | 0x2F0000 | 1024 KB |
| save | data | 0x9A | 0x3F0000 | 8 KB |

**Entregável:** decisões registradas aqui e no README; README corrigido nas seções de hardware, memória e armazenamento.

**Status:** README atualizado (componentes, pinout com DS3231 e CHRG→GPIO35, energia, tabela de partições com `save`, camadas, ambiente PlatformIO). Faltam D7 e D10.

---

## Fase 1 — Esqueleto do firmware

- `platformio.ini` com dois ambientes:
  - `esp32`: placa `nodemcu-32s`, ESP-IDF, monitor a 115200.
  - `native`: testes de lógica pura no PC com Unity.
- `partitions.csv` conforme a Fase 0.
- Estrutura de módulos (layout padrão do PlatformIO):

```
platformio.ini, partitions.csv, sdkconfig.defaults, CMakeLists.txt
src/                  # componente principal ESP-IDF: main.c, hal/, ui/, screens/, lua/ (dependem da IDF)
lib/                  # C puro, sem IDF, compilado nos dois ambientes:
  crc32/              #   CRC-32 compatível com zlib (já existe)
  gfx/                #   framebuffer, primitivas, fontes, sprites (Fase 4)
  savefmt/            #   serialização do Save State e leitura de VROM/catálogo (Fase 3)
  game/               #   ciclo de vida, estados, combate, evolução (Fases 5 e 7)
test/test_native/     # testes Unity do ambiente native
```

- **Regra de ouro:** nada em `lib/` inclui headers da IDF, para rodar nos testes `native`. O que precisa de hardware fica em `src/`.

**Pronto quando:** `pio run -e esp32` compila e grava um firmware que loga no serial, e `pio test -e native` roda um teste vazio.

**Status:** ✅ compila sem warnings (RAM 4,2%, Flash 9,5% da partição OTA). Os testes `native` passam (CRC32). Falta conferir no aparelho: `pio run -e esp32 -t upload -t monitor` deve logar a versão e as partições `storage` e `save`. Na primeira gravação, use `pio run -e esp32 -t erase` antes, pois a tabela de partições mudou.

---

## Fase 2 — HAL (drivers)

| Módulo | Escopo | Pronto quando |
|---|---|---|
| display | SH1106 em I2C a 400 kHz ou mais, framebuffer de 1 KB, `flush()` completo e parcial | Padrão de teste na tela, medição de FPS no log (meta ≥ 30) |
| botões | 6 GPIOs com pull-up, debounce, eventos press/release/repeat, fila FreeRTOS | Log de eventos sem repique |
| buzzer | LEDC canal 0 no GPIO25, `tone(freq, ms)`, fila de notas em task própria, mudo global | Escala tocando sem travar a UI |
| bateria | ADC1_CH6 calibrado (`esp_adc_cali`), média móvel, curva LiPo → %, detecção de USB (D6) | % estável no log |
| sd | VSPI + FATFS montado em `/sdcard`, montagem a quente | Listagem da raiz no log |
| clock | Abstração de tempo (D1), NTP, fuso | Hora correta após o boot com Wi-Fi |
| energia | Light sleep, deep sleep com despertar por GPIO32 (ext0) e GPIO33 (ext1), mais timer | Dorme e acorda pelos botões e por timer |

Backlog: H1, H3, H8.

---

## Fase 3 — Storage e ferramentas

**Packer v2 (`tools/storage-packer`)**
- Gera somente a partição `storage` e não toca em saves (H4).
- Erra, em vez de avisar, quando há basename duplicado, arquivo que não é 1 bpp ou nome com mais de 31 caracteres (A1, A2).
- VROM no formato nativo (D8), com índice ordenado por nome para busca binária no firmware.
- Gera `vrom_ids.h` com constantes por nome, para referências no código verificadas em compilação.
- Catálogo: fonte legível (`catalog/*.yaml` ou `.csv`) → binário `GCAT` com itens, equipamentos e habilidades. O schema inclui `slug` (I13) e o custo de MP das habilidades (L-combate).
- Ignora `bitmaps/exemple/` (mockups) e as fontes `.png`.
- Testes em Python dos formatos.

**Firmware**
- `vrom`: `esp_partition_mmap` da partição `storage`, busca por nome/ID, validação de CRC no boot.
- `catalog`: leitura dos registros pelo mmap.
- `save`: domínios serializados, CRC32, ring de 16 slots na partição `save`, espelho em `RTC_DATA_ATTR`, *dirty flag* e flush por eventos. Testado no `native` com uma flash simulada.
- `settings`: NVS.

**Pronto quando:** o boot carrega a VROM, desenha um ícone pelo nome, salva e restaura um save após um reset, e os testes `native` passam.

Backlog: H4, H5, I1–I5, A1–A4, L-dados.

---

## Fase 4 — Gráficos e kit de UI

- Fontes:
  - padrão 5x7 em células de 6px (20 caracteres por linha), com acentos do português (L-fonte);
  - grande, para relógio e timers.
- Primitivas: pixel, linha, retângulo (vazado, cheio, arredondado), círculo, texto, sprite com os modos de D9.
- Componentes conforme "Padrões do sistema":
  - barra de título com marquee;
  - abas;
  - barra de rolagem;
  - sub-menu de 3 linhas (resolver I10 da sobreposição do ícone com a seleção);
  - rodapé B/A;
  - modais (alerta, confirmação e escolha);
  - teclado virtual;
  - barra de progresso.
- Pilha de telas (`push`/`pop`) que preserva a seleção ao voltar com B.

**Pronto quando:** uma tela de demonstração com todos os componentes, conferida pixel a pixel contra as coordenadas do README.

Backlog: I9, I10, L-fonte.

---

## Fase 5 — Núcleo do pet

**Pré-requisito:** `GAME_DESIGN.md` com os números de progressão e desgaste (L-progressão).

- `game/` (C puro, testado no `native`):
  - modelo do Guará;
  - desgaste por tick e por Δt, com travas;
  - máquina de estados físicos e emocionais;
  - medidores ocultos;
  - XP e level up;
  - afinidades;
  - evolução de estágio e de natureza.
- Composição do sprite: cabeça + corpo (3 frames a 3 FPS) + olhos por emoção, preto e branco.
- Telas:
  - boot (logo + progresso + NTP);
  - nascimento (nome no teclado virtual, sorteio de natureza, cor e cabeça, UID, kit inicial);
  - tela principal (barra superior, Guará, balão de estado, área de itens com a animação de mordida).
- Menu principal em carrossel.
- Gestão de energia ligada ao uso: timeout de inatividade, deep sleep, emergência de bateria baixa.

Backlog: I6, I7, I8, I11, I12, L-progressão, L-sistema (navegação da tela principal).

---

## Fase 6 — Menus principais

Ordem sugerida, das telas mais simples para as mais dependentes:

1. Ajustes: Som, Controle, Energia, Armazenamento, Sistema (Status), Reiniciar.
2. Guará: abas GUARA, ESTADO, NIVEL, NATUREZA.
3. Inventário: abas, usar, detalhes, descartar.
4. Equipamentos: manequim e slots.
5. Habilidades.
6. Relógio: despertador, cronômetro, temporizador (no RTC RAM, I14), hora de dormir, ajuste manual, NTP.
7. Cartão SD: navegação, ações, detalhes.
8. Imagens.

---

## Fase 7 — Combate (Arena: Treino)

**Pré-requisito:** fórmulas completas em `GAME_DESIGN.md` (L-combate).

- Motor de combate em `game/combat`:
  - ATB;
  - dano, crítico, esquiva;
  - elementos (tabela completa das 14 naturezas);
  - estados;
  - IA do oponente.
  
  Tudo testado no `native`.
- Tela de combate: sprites espelhados, barras de HP e ATB, modal de ações, animações, vitória e derrota, ficha de recompensa.

---

## Fase 8 — Mídia

- Músicas: parser de MIDI (SMF tipos 0 e 1) reduzido a uma voz (L-midi), player em background, tela de lista e tocador.
- Biblioteca: leitor de `.txt` e de livros (`book.ini`, `book.txt`, `{{img.bmp}}`).

---

## Fase 9 — Console virtual Lua

- VM Lua 5.4 em sandbox: sem `io` e `os`, alocador com limite de memória, GC agressivo.
- Bindings: `oled`, `btn`, `audio`, `guara`, `storage` (isolado em `GAME_DIR`).
- Launcher: varredura de `/sdcard/games/`, `games.idx`, splash, descarregamento do Wi-Fi antes do jogo, aplicação de `[GUARA_EFFECTS]`.
- Jogo de exemplo no repositório.

---

## Fase 10 — Conectividade

- Wi-Fi: busca, conexão, redes salvas.
- Atualização OTA de firmware pelo SD, depois pelo GitHub. Atualização do `storage.bin` pelo SD.
- Bluetooth: pareamento, troca de itens (Compartilhar), combate PvP. Exige especificar o protocolo antes.

---

## Backlog de correções do README

### Hardware e arquitetura
- [x] **H1** Sem cristal de 32 kHz (GPIO32/33 são botões): deriva do RTC → D1
- [ ] **H2** ~10 µA em deep sleep irreal com a montagem atual → D7
- [x] **H3** Sem pino de detecção de USB/carga → D6
- [x] **H4** Gravar `storage.bin` apaga os saves → D4
- [x] **H5** "Configurações do Sistema" sem local definido → D5
- [x] **H6** Framework e versão do Lua indefinidos → D2, D3
- [ ] **H7** S09 buck × faixa de tensão da LiPo; TP4056 sem load sharing → D7
- [x] **H8** Frequência do I2C precisa garantir 30 FPS → 400 kHz + envio só das páginas alteradas (FPS real ainda não medido no display)

### Inconsistências
- [x] **I1** Área de save: "Setores 2 e 3" (README) × últimos 8 KB (packer)
- [x] **I2** `HeaderDomain` de 12 bytes não comporta os campos listados → HeaderDomain com 16 bytes
- [x] **I3** `InventoryDomain` de 128 bytes × catálogo de 64 itens; texto diz "não referenciar itens zerados" → InventoryDomain com 64 bytes, indexado pelo ID do item
- [x] **I4** `VitalsDomain` com 1 byte por valor limita HP/MP a 255 → vitais em u16
- [x] **I5** Soma dos domínios (233) × "~240 bytes" → tabela do README refeita a partir do código (190 bytes úteis)
- [x] **I6** Gatilho de fome: alimentação ≤10% × energia ou alimento a 10% → fome só pela alimentação ≤ 10% (docs/GAME_DESIGN.md)
- [x] **I7** Queda de 2%/s nos estados físicos × desgaste gradual e travas → 2% por minuto
- [x] **I8** Nomes de estados: Sono/Cansado, Chateado/Entediado, olhos Assustado/Atordoado sem estado → Sono = Cansado, Chateado = Entediado, olhos Assustado/Atordoado mapeados
- [x] **I9** ~~Carrossel invertido~~: não é inconsistência. Os lados são do ponto de vista do aparelho (convenção registrada no README e conferida nos mockups).
- [x] **I10** Linha de 16px com ícone de 16px: a seleção sobrepõe o ícone → v0.3.1: barra de 11 px, lista a partir de Y=12 com caixas de 16 px e 2 px entre os itens; o contorno da seleção encosta no ícone em cima e embaixo (aceito)
- [x] **I11** Três definições de exibição do "Feliz" → regra única em docs/GAME_DESIGN.md
- [x] **I12** Sequência de frames "1, 2, 2" e margem dos olhos "frames 1 e 2 … frame 2" → sequência literal 1, 2, 2; olhos fixos pelo quadro 1
- [x] **I13** `give_item("maca")` usa identificador textual inexistente no catálogo → coluna `slug` no catálogo
- [x] **I14** Temporizador "só na RAM" não sobrevive ao deep sleep → temporizador na RTC RAM e despertar por timer
- [x] **I15** Vantagens (Fogo<Água<Terra<Ar) × opostos de afinidade (fogo×água, terra×ar); fusões Vapor/Cristal dependem da regra do Neutro, que não está explícita → regra do Neutro explícita em docs/GAME_DESIGN.md
- [x] **I16** `add_affinity` cita "mente"/"pontos mentais", que não existem → `mind` soma INT
- [x] **I17** Saúde sem efeito em itens e fórmulas → Saúde ajusta o ritmo de desgaste
- [x] **I18** "Pés" (CATALOG) × "Calçados" (README) → a tela mostra "Calçados"; o tipo no catálogo é PES
- [ ] **I19** Preset `sprite` 32x32 × sprites reais de 16–18px compostos
- [ ] **I20** ~~`[[CHANGELOG]]` inexistente~~ (CHANGELOG.md criado); referências `![[x.bmp]]` ainda não renderizam no GitHub

### Lacunas
- [x] **L-progressão** Curva de XP, crescimento de atributos e HP/MP, níveis dos 5 estágios (yong_a/b/c: estágio ou ramo?), limiares de evolução especial, taxas de desgaste, hora de dormir, desmaios repetidos → valores provisórios em docs/GAME_DESIGN.md (revisar)
- [x] **L-combate** Dano de habilidade, custo de MP (falta no schema), ordem dos multiplicadores, IA, aplicação e cura de estados, consequências do treino, vantagens das naturezas secundárias e especiais → valores provisórios em docs/GAME_DESIGN.md (revisar)
- [ ] **L-dados** Habilidades possuídas, estágio e UID implementados; ícone referenciado por nome → falta o **kit inicial** e o conteúdo do catálogo (autor, em `CATALOG.md`)
- [x] **L-fonte** Fontes e suporte a acentos → fonte 5x8 com acentos em tools/font-builder
- [ ] **L-sistema** ~~Botão que abre o menu principal~~ (A); configuração de fuso (fixo em UTC-3, falta menu); ~~sandbox Lua~~; `games.idx` (a lista é lida da pasta a cada abertura); ~~atualização do storage em campo~~ (SD)
- [x] **L-midi** Redução de MIDI a uma voz; Cima/Baixo no player troca faixa ou volume? → nota mais aguda ativa; ↑/↓ troca de faixa e ←/→ ajusta o volume

### Conectividade
- [x] **C1** Wi-Fi sob demanda (README > Conectividade: Wi-Fi sob Demanda): rádio ligado só durante tarefas de rede, boot sem esperar a rede, NTP a cada 24 h, desligamento após 30 s ocioso. Corrige também o Wi-Fi não religar ao acordar do deep sleep.

### Assets
- [x] **A1** Basenames duplicados entre `ui/` e `icons/`: alarm, bluetooth, mute, timer → renomeados para status_*
- [x] **A2** Em 16 bpp: `bitart_logo.bmp`, `icons/equipment.bmp`, `icons/github.bmp`, `icons/mute.bmp` → convertidos para 1 bpp
- [x] **A3** `bitmaps/exemple/` (mockups) dentro da árvore da VROM → o packer ignora bitmaps/exemple/
- [ ] **A5** Mockup `main_menu.bmp` mostra 10 pontos no indicador, mas o README tem 12 menus
- [ ] **A4** Ícones faltando: Wi-Fi desligado, bateria carregando/USB; `status_*_8x8` sem uso documentado
