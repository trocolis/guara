# Changelog

Versionamento `v#.#.#` (release.feature.patch) conforme README > Implementação > Versionamento.
Cada escopo executado gera um commit e uma entrada aqui.

## v0.9.2 — 2026-10-08

### Combate
- A janela de reação não mostra mais o ícone do A: o atacante recua para tomar impulso e avança de volta durante a janela, e o jogador prevê o tempo pelo movimento.
- Sem os textos "Certeiro!" e "Defesa!".
- Crítico com `!` no final do dano (`-10!`).
- O segundo impacto do ataque duplo fica 8 px para dentro do sprite (antes 4 px).

## v0.9.1 — 2026-10-08

### Combate
- Ataque duplo: o atacante dá o pulo para a frente duas vezes e o segundo impacto aparece 4 px mais para dentro do sprite do alvo.

## v0.9.0 — 2026-10-08

### Combate
- Animação de impacto quando um golpe causa dano: 7 quadros (`hit_1` a `hit_7`, convertidos de `bitmaps/animations/hit/*.png`) a 30 FPS, na borda do alvo voltada para o atacante, desenhados em XOR (preto transparente, cor invertida onde cruza o sprite). Repete no ataque duplo.

## v0.8.0 — 2026-10-08

### Catálogo
- 64 itens no catálogo, convertidos do catálogo do Petnator para o `CATALOG.md` e o `catalog/items.csv`: 16 frutas e vegetais, 8 bebidas, 24 lanches, carnes e sobremesas, e 16 poções, tônicos e remédios. A Maçã mantém a entrada escrita no `CATALOG.md`. A afinidade, não definida na origem, ficou Neutra em todos.
- Ícones dos itens renomeados para o nome do item em inglês (`fruta_banana` → `banana`, `pocao_vida` → `potion`…). Exceções por conflito com ícones do sistema: Hambúrguer usa `burger` (o `hamburger` é o ícone de Alimentação) e Água usa `drinking_water` (o `water` é o da natureza Água).
- `CATALOG.md`: abreviação HU para humor (a legenda repetia HP), estados BRN (Queimado) e ALL (cura de todos os estados), descrição com até 48 caracteres.
- Packer: descrição com até 48 caracteres e efeitos entre −9999 e 9999 (as poções maiores restauram até 2500 de HP).
- Modais de escolha (e `ui.choice` no Lua) com até 16 opções, para todos os itens de cura aparecerem no combate.

## v0.7.1 — 2026-10-08

### Correções
- Deep sleep: a placa acordava sozinha (o light sleep automático deixava o timer e a UART como fontes de despertar) ou não acordava. Agora as fontes são limpas antes de dormir e o ULP confere os quatro botões (o `ext0` do Cima não pode ser usado junto com o ULP).
- Display apagado depois de acordar: o deep sleep podia cortar uma transmissão I2C no meio e o SH1106 ficava segurando o SDA. O display só é desligado depois da transmissão em andamento, e o boot destrava o barramento se o SDA estiver preso.
- O ULP para depois de acordar o chip.
- Console: comando `Z` (passo `sleep` no `console.py`) para entrar em deep sleep na hora.

## v0.7.0 — 2026-10-08

### Energia
- Modo Ocioso após 30 s sem botões: brilho mínimo, telas a no máximo 5 FPS, CPU entre 80 e 160 MHz com light sleep automático e botões lidos a cada 20 ms. O primeiro toque só restaura o modo Ativo. Jogos, combate, cronômetro e alarme tocando impedem o modo Ocioso; o buzzer bloqueia o light sleep enquanto toca.
- Os botões A e B também acordam o aparelho do deep sleep, pelo coprocessador ULP (o `ext1` do ESP32 não acorda com "qualquer pino em nível baixo").
- Console de depuração acorda o chip do light sleep pela UART; o `console.py` manda um preâmbulo antes de cada comando.

## v0.6.1 — 2026-10-08

### Ciclo de vida
- A recuperação de HP fica em pausa enquanto o Guará dorme; o sono só repõe energia.

## v0.6.0 — 2026-10-08

### Ciclo de vida
- Recuperação de HP: com alimentação, saúde e energia acima de 50%, o HP sobe 1% do máximo por minuto e gasta 0,20%/min a mais de energia, até a energia chegar a 10%. Com o HP cheio, não gasta energia. O estado fica no bit interno 10 da máscara de estados (sem ícone).

## v0.5.2 — 2026-10-08

### Correções
- Combate: o menu de ações abria com um toque atrasado na janela de reação. Agora, depois de cada golpe, A só abre o menu após 600 ms.

## v0.5.1 — 2026-10-08

### Combate
- A ação escolhida fica travada até ser executada: apertar A não abre o modal de ações antes do próximo ciclo da barra.

## v0.5.0 — 2026-10-08

### Combate
- Barra de MP (28 x 5 px) abaixo da barra de HP de cada lutador; os nomes saíram da tela de luta.
- Abertura "Fulano  VS  Siclano" no início do combate (A ou B pula).
- Barra de ação com 32 px, precedida pelo ícone do botão A piscando. A ação pode ser escolhida antes de a barra encher e só executa com ela cheia; depois da escolha o ícone para de piscar.
- Janela de reação no fim do preparo de cada golpe: quem ataca e aperta A ganha +20% de crítico e 30% de chance de ataque duplo; quem é atacado e aperta A recebe metade do dano e o atacante tem +15% de chance de errar. O oponente acerta a janela com 25% de chance.

 — 2026-10-08

### Animação
- Mordidas mais lentas: 450 ms por quadro (antes 300 ms).
- A cada mordida, o Guará fica com o olhar feliz por meio quadro e volta ao normal.

## v0.4.0 — 2026-10-08

### Catálogo
- Primeiro item do catálogo, convertido do `CATALOG.md`: Maçã (`apple`), comida com Alimentação +15, HP +5 e Energia +10. A afinidade, não definida no `CATALOG.md`, ficou como Neutra.

## v0.3.2 — 2026-10-08

### Interface
- Modal com 112 x 52 px (Y=6..57) para mostrar até 3 linhas de mensagem em alertas e confirmações; os detalhes mostram 4 linhas.
- Rodapé do modal 2 px mais abaixo (sprites B e A em Y=47).

### Código
- Campo do evento de tecla renomeado de `on_key` para `key` e o do carrossel para `open_menu` (sobras da refatoração da v0.2.1).

## v0.3.1 — 2026-10-08

### Interface
- Barra de título e de abas com 11 px (Y=0..10).
- Lista dos sub-menus a partir de Y=12 (1 px livre abaixo da barra), com caixas de seleção de 16 px e 2 px de espaço entre os itens (Y=12..63).
- Barra de título do modal branca com texto preto e 11 px, como a das telas; barra do teclado virtual também com 11 px.
- Vale para todos os sub-menus, listas e para `ui.list`/`ui.tabs` dos jogos Lua.

### Exemplos
- Boitatá usa os componentes do sistema:
  - menu, configurações e ranking com `ui.title` e `ui.list`;
  - nome do jogador pelo `ui.keyboard`;
  - slots, pausa e fim de jogo com `ui.choice` e `ui.alert`.
  
  Os saves continuam no mesmo formato. O código ganhou comentários em português.
- Boitatá movido de `sdcard/games/` para `examples/sdcard/games/boitata/`: passa a ser o jogo de referência do projeto, evoluído junto com o console.

## v0.3.0 — 2026-10-08

### API Lua: gráficos e componentes do sistema
- `oled.icon` aceita opções `{mode, scale, mirror}` (modos `or`, `copy`, `mask`, `xor` e `invert`); novo `oled.icon_size`.
- `guara.draw` desenha o Guará do jogador (olhos, escala 2x, espelho, quadro), e `guara.get_status` informa cor, cabeça e estágio.
- Novo módulo `catalog`: `item`, `equipment`, `skill` (por id ou slug) e `count`.
- Novo módulo `ui`:
  - componentes de desenho: `title`, `tabs`, `list`, `footer`, `progress`, `scrollbar`, `marquee`, `paragraph`, `text_center`;
  - componentes interativos: `alert`, `confirm`, `choice`, `quantity`, `details`, `keyboard`.
- Com um modal ou o teclado aberto, o jogo fica em pausa e a tecla que fecha o modal não é repassada a ele.
- O exemplo Pega-Maçã usa a pausa com `ui.choice` e a tela final com `guara.draw`.

## v0.2.1 — 2026-10-08

### Refatoração (README > Implementação > Código)
- Identificadores, funções, arquivos e pastas renomeados para o inglês: `src/system`, `src/screens`, `lib/core`, `examples/`, `docs/PLAN.md` e todos os módulos. Sem mudança de comportamento: os testes e o build com `-Wshadow` não acusam diferenças.
- Comentários em português: cabeçalho em todos os arquivos e um comentário antes de cada função.
- Ferramentas Python (`pack_storage.py`, `build_font.py`, `console.py`, pré-scripts do PlatformIO) reescritas com nomes em inglês.
- API Lua: `sair()` virou `exit()`, e os campos de `guara.get_status()`, os nomes de `guara.add_stat()` e os elementos de `guara.add_affinity()` passaram para o inglês. Os jogos do cartão foram atualizados.
- Catálogo: arquivos `items.csv`, `equipment.csv` e `skills.csv`, com colunas e valores em inglês (`FOOD`, `FIRE`, `SATIETY`…). O formato binário não mudou.
- Despertadores gravados em `/sdcard/guara/alarms.txt`; o `alarmes.txt` antigo é migrado automaticamente.
- `console.py`: o modo `roteiro` virou `script` (passos `wait`, `keys`, `screen`, `state`, `log`).

## v0.2.0 — 2026-10-08

### Wi-Fi sob demanda
- Com o Wi-Fi em ON, o rádio só liga durante tarefas de rede e desliga após 30 s ocioso. Há reconexão rápida pelo BSSID e canal salvos na NVS.
- O NTP roda em segundo plano a cada 24 h ou quando a hora é inválida, e o boot não espera mais a rede.
- O Wi-Fi passou a se comportar igual após acordar do deep sleep.

### Regra de desenvolvimento
- Versões `0.x.x` liberam todo o catálogo no nascimento e a cada boot: todos os itens com 255 unidades e todos os equipamentos e habilidades.

### Catálogo
- Removido o conteúdo provisório: `catalog/*.csv` fica vazio até o `CATALOG.md` ser preenchido, e o kit inicial foi retirado.

### Sprites
- Guará escalonado em 2x na tela principal; nova animação de olhos no quadro 3.
- Direcional com Direita e Esquerda corrigidos.

### Jogos do cartão SD
- `cobra`, `forca`, `velha` e `boitata` portados para a API Lua nativa.

## v0.1.0 — 2026-10-08

Primeira versão funcional do firmware e do storage.

### Firmware
- ESP-IDF 6.1 puro via PlatformIO; tabela de partições com `ota_0`/`ota_1`, `storage` (somente leitura) e `save` dedicada.
- Drivers: SH1106 (I2C, envio só das páginas alteradas), botões com debounce e auto-repetição, buzzer (LEDC) com efeitos e música em segundo plano, bateria (ADC + CHRG do TP4056), cartão SD (SPI a 10/4 MHz), relógio (DS3231 quando presente + NTP), deep sleep com despertar por Cima/Baixo e por timer.
- Persistência em 3 pilares: espelho na RTC RAM, gravação por eventos e anel de 16 slots na partição `save`; configurações na NVS.
- Kit de UI do README: barra de título, abas, sub-menu de 3 linhas, rolagem, rodapé B/A, marquee, modais (alerta, confirmação, escolha, quantidade, detalhes) e teclado virtual.
- Telas: boot, nascimento, tela principal (Guará animado, balão de estados, uso de itens), menu em carrossel, Guará (4 abas), Inventário, Equipamentos, Habilidades, Arena (treino), Biblioteca, Jogos (VM Lua 5.4), Músicas (MIDI), Imagens, Relógio (despertador, cronômetro, temporizador, hora de dormir, ajuste, NTP), Cartão SD e Ajustes (Wi-Fi, Bluetooth, Energia, Armazenamento, Controle, Som, Sistema com OTA pelo SD, Reiniciar).
- Console de depuração pela serial (`tools/console.py`).

### Storage
- Formato v2: VROM com bitmaps em formato nativo e índice ordenado, catálogo binário `GCAT` gerado de `catalog/*.csv` (vazio até o conteúdo do `CATALOG.md` ser definido).
- Ícones `ui/` renomeados para `status_*` (nomes únicos na VROM); bitmaps de 16 bpp convertidos para 1 bpp.

### Pendente
- Bluetooth (trocas e combate entre Guarás), atualização pelo GitHub, decisões D7 e D10 de hardware.
