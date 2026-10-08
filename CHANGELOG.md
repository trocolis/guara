# Changelog

Versionamento `v#.#.#` (release.feature.patch) conforme README > Implementação > Versionamento.
Cada escopo executado gera um commit e uma entrada aqui.

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
