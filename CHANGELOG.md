# Changelog

Versionamento semântico conforme README > Versionamento e Política de Releases.

## 0.1.0 — 2026-10-08

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

### Regra de desenvolvimento
- Versões `0.x.x` liberam todo o catálogo no nascimento e a cada boot: todos os itens com 255 unidades e todos os equipamentos e habilidades.

### Wi-Fi sob demanda
- Com o Wi-Fi em ON, o rádio só liga durante tarefas de rede e desliga após 30 s ocioso. Há reconexão rápida pelo BSSID e canal salvos na NVS.
- O NTP roda em segundo plano a cada 24 h ou quando a hora é inválida, e o boot não espera mais a rede.
- O Wi-Fi passou a se comportar igual após acordar do deep sleep.

### Pendente
- Bluetooth (trocas e combate entre Guarás), atualização pelo GitHub, decisões D7 e D10 de hardware.
