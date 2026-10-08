-- ==========================================================================
-- BOITATÁ — jogo de referência do console virtual do Guará
--
-- A serpente de fogo do folclore cresce a cada brasa que come. As telas de
-- menu, configuração, ranking, pausa e fim de jogo usam os componentes do
-- sistema (módulo `ui`), então seguem o mesmo visual do resto do aparelho.
--
-- Controles: direcional move a serpente; B pausa; A confirma nos menus.
-- Arquivos na pasta do jogo (via storage): boitata_user.sav (configuração),
-- boitata_rnk.sav (ranking) e boitata_<slot>.sav (jogos salvos).
-- ==========================================================================

-- Telas do jogo. Os modais do sistema (escolhas, alertas, teclado) abrem por
-- cima delas e devolvem a resposta em funções de retorno.
local SCREEN_MENU    = 1
local SCREEN_PLAY    = 2
local SCREEN_CONFIG  = 3
local SCREEN_RANKING = 4
local SCREEN_OVER    = 5 -- fim de jogo: tabuleiro congelado por trás do modal

local current_screen = SCREEN_MENU

-- Tabuleiro: 25 x 12 células de 5 px, com margem de 2 px
local GRID_W = 25
local GRID_H = 12
local CELL_SIZE = 5
local OFFSET_X = 2
local OFFSET_Y = 2

local SLOT_COUNT = 3
local RANKING_SIZE = 5
local NAME_LENGTH = 3

-- Chaves de save (storage grava <chave>.sav na pasta do jogo)
local CONFIG_KEY = "boitata_user"
local RANKING_KEY = "boitata_rnk"
local LEGACY_SAVE_KEY = "boitata" -- save único das primeiras versões, lido como slot 1

-- Configuração persistente. Os valores em texto são os mesmos gravados pelas
-- versões anteriores, para manter a compatibilidade dos saves.
local config = {
  speed_type = "progressiva", -- "progressiva" (acelera com os pontos) ou "fixa"
  speed_level = 2,            -- 1..4, usado na velocidade fixa
  arena_mode = "trancado",    -- "trancado" (bater na borda perde) ou "infinito"
  last_name = "BOI",
}

-- Estado da partida
local snake = {}
local dir_x, dir_y = 1, 0
local next_dir_x, next_dir_y = 1, 0
local food = {x = 10, y = 6}
local score = 0
local move_timer = 0
local move_delay = 5 -- quadros entre cada passo da serpente
local player_name = "BOI"
local current_slot = 1

-- Cursores das listas desenhadas com ui.list (começam em 1)
local menu_cursor = 1
local config_cursor = 1
local ranking_cursor = 1

local ranking = {
  {name = "BOI", score = 150},
  {name = "PET", score = 100},
  {name = "SNA", score = 70},
  {name = "LUA", score = 40},
  {name = "DEV", score = 20},
}

-- Cache com nome e pontuação de cada slot (evita ler o cartão a cada quadro)
local slot_cache = {}

-- --------------------------------------------------------------------------
-- Utilitários
-- --------------------------------------------------------------------------

-- Itera as linhas de um save, ou retorna nil se ele não existir
local function save_lines(key)
  local data = storage.load(key)
  if not data then return nil end
  return data:gmatch("[^\r\n]+")
end

-- Separa "chave=valor" (com espaços opcionais); retorna nil se a linha não for desse formato
local function parse_pair(line)
  local key, value = line:match("^%s*([^=]-)%s*=%s*(.-)%s*$")
  return key, value
end

-- Deixa o nome no formato do ranking: 3 caracteres maiúsculos, só letras, números e espaço
local function normalize_name(text)
  text = (text or ""):upper():gsub("[^A-Z0-9 ]", "")
  if #text == 0 then return nil end
  return (text .. "   "):sub(1, NAME_LENGTH)
end

-- --------------------------------------------------------------------------
-- Configuração e ranking
-- --------------------------------------------------------------------------

local function save_config()
  storage.save(CONFIG_KEY, "[CONFIG]\nspeed_type=" .. config.speed_type ..
                           "\nspeed_level=" .. tostring(config.speed_level) ..
                           "\narena_mode=" .. config.arena_mode ..
                           "\nlast_name=" .. config.last_name .. "\n")
end

local function load_config()
  local lines = save_lines(CONFIG_KEY)
  if not lines then return end
  for line in lines do
    local key, value = parse_pair(line)
    if key == "speed_type" then config.speed_type = value
    elseif key == "speed_level" then config.speed_level = tonumber(value) or 2
    elseif key == "arena_mode" then config.arena_mode = value
    elseif key == "last_name" then config.last_name = normalize_name(value) or config.last_name end
  end
end

local function save_ranking()
  local lines = {}
  for i = 1, math.min(#ranking, RANKING_SIZE) do
    table.insert(lines, ranking[i].name .. "=" .. tostring(ranking[i].score))
  end
  storage.save(RANKING_KEY, table.concat(lines, "\n") .. "\n")
end

local function load_ranking()
  local lines = save_lines(RANKING_KEY)
  if not lines then return end
  local loaded = {}
  for line in lines do
    local name, points = line:match("^([^=]+)=(%d+)$")
    if name then
      table.insert(loaded, {name = normalize_name(name) or "???", score = tonumber(points)})
    end
  end
  if #loaded > 0 then ranking = loaded end
end

-- Insere a pontuação atual no ranking (mantém só as 5 melhores)
local function submit_score()
  table.insert(ranking, {name = player_name, score = score})
  table.sort(ranking, function(a, b) return a.score > b.score end)
  while #ranking > RANKING_SIZE do table.remove(ranking) end
  save_ranking()
end

-- --------------------------------------------------------------------------
-- Slots de jogo salvo
-- --------------------------------------------------------------------------

local function slot_key(slot)
  return "boitata_" .. slot
end

-- Linhas do save de um slot (o slot 1 também aceita o save único antigo)
local function slot_lines(slot)
  return save_lines(slot_key(slot)) or (slot == 1 and save_lines(LEGACY_SAVE_KEY)) or nil
end

-- Lê só o nome e a pontuação de um slot, para as listas de escolha
local function read_slot_summary(slot)
  local lines = slot_lines(slot)
  if not lines then return {exists = false} end
  local summary = {exists = true, name = "BOI", score = 0}
  for line in lines do
    local key, value = parse_pair(line)
    if key == "name" then summary.name = normalize_name(value) or "BOI"
    elseif key == "score" then summary.score = tonumber(value) or 0 end
  end
  return summary
end

local function refresh_slot_cache()
  for slot = 1, SLOT_COUNT do
    slot_cache[slot] = read_slot_summary(slot)
  end
end

local function any_save_exists()
  for slot = 1, SLOT_COUNT do
    if slot_cache[slot].exists then return true end
  end
  return false
end

-- Texto de um slot para o ui.choice (cabe nas 24 colunas do modal)
local function slot_label(slot)
  local summary = slot_cache[slot]
  if not summary.exists then
    return string.format("Slot %d: vazio", slot)
  end
  return string.format("Slot %d: %s %d pts", slot, summary.name, summary.score)
end

local function save_game(slot)
  local body = {}
  for _, segment in ipairs(snake) do
    table.insert(body, segment.x .. "," .. segment.y)
  end
  local ok = storage.save(slot_key(slot),
    "name=" .. player_name ..
    "\nscore=" .. tostring(score) ..
    "\ndir_x=" .. tostring(dir_x) ..
    "\ndir_y=" .. tostring(dir_y) ..
    "\nfood_x=" .. tostring(food.x) ..
    "\nfood_y=" .. tostring(food.y) ..
    "\nspeed_type=" .. config.speed_type ..
    "\nspeed_level=" .. tostring(config.speed_level) ..
    "\narena_mode=" .. config.arena_mode ..
    "\nsnake=" .. table.concat(body, ";") .. "\n")
  if ok then
    slot_cache[slot] = {exists = true, name = player_name, score = score}
  end
  return ok
end

-- Velocidade: na fixa vem do nível; na progressiva acelera a cada 50 pontos
local function current_delay()
  if config.speed_type == "fixa" then
    local delays = {6, 4, 3, 2}
    return delays[config.speed_level] or 4
  end
  return math.max(2, 6 - score // 50)
end

-- Carrega um slot; retorna false se ele não existir ou estiver corrompido
local function load_game(slot)
  local lines = slot_lines(slot)
  if not lines then return false end
  local loaded_snake = {}
  for line in lines do
    local key, value = parse_pair(line)
    if key == "name" then player_name = normalize_name(value) or player_name
    elseif key == "score" then score = tonumber(value) or 0
    elseif key == "dir_x" then dir_x = tonumber(value) or 1
    elseif key == "dir_y" then dir_y = tonumber(value) or 0
    elseif key == "food_x" then food.x = tonumber(value) or 10
    elseif key == "food_y" then food.y = tonumber(value) or 6
    elseif key == "speed_type" then config.speed_type = value
    elseif key == "speed_level" then config.speed_level = tonumber(value) or 2
    elseif key == "arena_mode" then config.arena_mode = value
    elseif key == "snake" then
      for x, y in value:gmatch("(%d+),(%d+)") do
        table.insert(loaded_snake, {x = tonumber(x), y = tonumber(y)})
      end
    end
  end
  if #loaded_snake < 3 then return false end
  snake = loaded_snake
  if dir_x == 0 and dir_y == 0 then
    dir_x = snake[1].x >= snake[2].x and 1 or -1
  end
  next_dir_x, next_dir_y = dir_x, dir_y
  move_timer = 0
  move_delay = current_delay()
  current_slot = slot
  return true
end

-- --------------------------------------------------------------------------
-- Partida
-- --------------------------------------------------------------------------

-- Sorteia uma posição livre para a brasa (desiste após 100 tentativas)
local function spawn_food()
  for _ = 1, 100 do
    food.x = math.random(GRID_W) - 1
    food.y = math.random(GRID_H) - 1
    local free = true
    for _, segment in ipairs(snake) do
      if segment.x == food.x and segment.y == food.y then free = false; break end
    end
    if free then return end
  end
end

local function start_new_game()
  snake = {{x = 10, y = 6}, {x = 9, y = 6}, {x = 8, y = 6}}
  dir_x, dir_y = 1, 0
  next_dir_x, next_dir_y = 1, 0
  score, move_timer = 0, 0
  move_delay = current_delay()
  spawn_food()
  current_screen = SCREEN_PLAY
  audio.beep(523, 30)
end

local open_game_over -- definida mais abaixo (usada por step_snake)

-- Um passo da serpente: move, verifica colisões e come a brasa
local function step_snake()
  dir_x, dir_y = next_dir_x, next_dir_y
  local new_x = snake[1].x + dir_x
  local new_y = snake[1].y + dir_y

  if config.arena_mode == "trancado" then
    if new_x < 0 or new_x >= GRID_W or new_y < 0 or new_y >= GRID_H then
      open_game_over()
      return
    end
  else
    new_x = new_x % GRID_W -- arena infinita: atravessa a borda
    new_y = new_y % GRID_H
  end

  -- A ponta da cauda sai do lugar neste passo, exceto quando a serpente come
  local eating = new_x == food.x and new_y == food.y
  local last_checked = eating and #snake or #snake - 1
  for i = 2, last_checked do
    if snake[i].x == new_x and snake[i].y == new_y then
      open_game_over()
      return
    end
  end

  table.insert(snake, 1, {x = new_x, y = new_y})
  if eating then
    score = score + 10
    audio.beep(880, 35)
    move_delay = current_delay()
    spawn_food()
  else
    table.remove(snake)
  end
end

-- --------------------------------------------------------------------------
-- Fluxos com os modais do sistema
-- --------------------------------------------------------------------------

local function back_to_menu()
  current_screen = SCREEN_MENU
  refresh_slot_cache()
end

-- Escolha de slot para começar um jogo novo (pode sobrescrever um save)
local function choose_new_game_slot()
  local options = {}
  for slot = 1, SLOT_COUNT do options[slot] = slot_label(slot) end
  ui.choice("SALVAR EM QUAL SLOT?", options, function(slot)
    if slot then
      current_slot = slot
      start_new_game()
    end
  end)
end

-- Novo jogo: pede o nome no teclado do sistema e depois o slot
local function ask_player_name()
  ui.keyboard("Seu nome (3 letras)", config.last_name, NAME_LENGTH, function(text)
    local name = normalize_name(text)
    if not name then return end -- B no teclado: volta ao menu
    player_name = name
    config.last_name = name
    save_config()
    choose_new_game_slot()
  end)
end

-- Continuar: lista os slots e carrega o escolhido
local function choose_slot_to_load()
  local options = {}
  for slot = 1, SLOT_COUNT do options[slot] = slot_label(slot) end
  ui.choice("CARREGAR JOGO", options, function(slot)
    if not slot then return end
    if slot_cache[slot].exists and load_game(slot) then
      current_screen = SCREEN_PLAY
      audio.beep(659, 40)
    else
      ui.alert("CARREGAR", "Este slot está vazio.", "save")
    end
  end)
end

-- Pausa (B durante a partida): o jogo fica parado enquanto o modal está aberto
local function open_pause()
  ui.choice("PAUSA", {"Continuar", "Salvar", "Sair para o menu"}, function(option)
    if option == 2 then
      if save_game(current_slot) then
        ui.alert("PAUSA", "Salvo no slot " .. current_slot .. "!", "save")
      else
        ui.alert("PAUSA", "Erro ao salvar.", "save")
      end
    elseif option == 3 then
      back_to_menu()
    end
  end)
end

-- Fim de jogo: o tabuleiro fica congelado por trás do modal de opções
open_game_over = function()
  audio.beep(180, 100)
  current_screen = SCREEN_OVER
  local has_save = slot_cache[current_slot] and slot_cache[current_slot].exists
  local options = has_save and {"Voltar ao save", "Novo jogo", "Menu principal"}
                            or {"Novo jogo", "Menu principal"}
  ui.choice(string.format("FIM: %d PONTOS", score), options, function(option)
    local choice = option and options[option] or "Menu principal"
    if choice == "Voltar ao save" then
      if not load_game(current_slot) then
        ui.alert("FIM DE JOGO", "Erro ao recarregar o save.", "save")
        submit_score()
        back_to_menu()
        return
      end
      current_screen = SCREEN_PLAY
      return
    end
    submit_score()
    if choice == "Novo jogo" then
      start_new_game()
    else
      back_to_menu()
    end
  end)
end

-- --------------------------------------------------------------------------
-- Menus desenhados com ui.list
-- --------------------------------------------------------------------------

-- Itens do menu principal, com ícones da VROM e o aviso de "sem save" à direita
local function menu_items()
  return {
    {text = "Novo jogo", icon = "add"},
    {text = "Continuar", icon = "save", info = any_save_exists() and "" or "--"},
    {text = "Configurar", icon = "gear"},
    {text = "Ranking", icon = "level"},
    {text = "Sair", icon = "kill"},
  }
end

-- Itens da configuração, com o valor atual na coluna da direita (até 4 letras)
local function config_items()
  local levels = {"LENT", "NORM", "RAPI", "TURB"}
  return {
    {text = "Velocidade", icon = "agility", info = config.speed_type == "fixa" and "FIXA" or "PROG"},
    {text = "Nível", icon = "level", info = config.speed_type == "fixa" and (levels[config.speed_level] or "NORM") or "--"},
    {text = "Arena", icon = "arena", info = config.arena_mode == "trancado" and "TRAN" or "INF"},
    {text = "Voltar", icon = "up"},
  }
end

-- Itens do ranking: posição e nome, pontuação à direita
local function ranking_items()
  local items = {}
  for i = 1, RANKING_SIZE do
    local entry = ranking[i]
    items[i] = entry and {text = i .. ". " .. entry.name, info = tostring(entry.score)}
                      or {text = i .. ". ---", info = "0"}
  end
  return items
end

-- Move um cursor de lista com Cima/Baixo, voltando ao início/fim
local function move_cursor(cursor, count)
  if btn.pressed("up") then
    audio.beep(440, 15)
    return (cursor - 2) % count + 1
  elseif btn.pressed("down") then
    audio.beep(440, 15)
    return cursor % count + 1
  end
  return cursor
end

local function update_menu()
  menu_cursor = move_cursor(menu_cursor, 5)
  if not btn.pressed("a") then return end
  audio.beep(523, 20)
  if menu_cursor == 1 then
    ask_player_name()
  elseif menu_cursor == 2 then
    if any_save_exists() then
      choose_slot_to_load()
    else
      ui.alert("CONTINUAR", "Nenhum jogo salvo ainda.", "save")
    end
  elseif menu_cursor == 3 then
    config_cursor = 1
    current_screen = SCREEN_CONFIG
  elseif menu_cursor == 4 then
    ranking_cursor = 1
    current_screen = SCREEN_RANKING
  else
    exit()
  end
end

-- Configuração: ←/→/A mudam o valor da linha selecionada; B ou "Voltar" saem
local function update_config()
  config_cursor = move_cursor(config_cursor, 4)
  local change = btn.pressed("left") and -1 or ((btn.pressed("right") or btn.pressed("a")) and 1 or 0)
  if btn.pressed("b") or (config_cursor == 4 and btn.pressed("a")) then
    save_config()
    current_screen = SCREEN_MENU
    audio.beep(330, 20)
    return
  end
  if change == 0 then return end
  if config_cursor == 1 then
    config.speed_type = config.speed_type == "progressiva" and "fixa" or "progressiva"
  elseif config_cursor == 2 and config.speed_type == "fixa" then
    config.speed_level = (config.speed_level - 1 + change) % 4 + 1
  elseif config_cursor == 3 then
    config.arena_mode = config.arena_mode == "trancado" and "infinito" or "trancado"
  else
    return
  end
  save_config()
  audio.beep(523, 20)
end

local function update_ranking()
  ranking_cursor = move_cursor(ranking_cursor, RANKING_SIZE)
  if btn.pressed("a") or btn.pressed("b") then
    current_screen = SCREEN_MENU
    audio.beep(440, 20)
  end
end

local function update_play()
  if btn.pressed("b") then
    audio.beep(523, 25)
    open_pause()
    return
  end
  -- A serpente não pode dar meia-volta sobre o próprio corpo
  if btn.pressed("up") and dir_y == 0 then next_dir_x, next_dir_y = 0, -1
  elseif btn.pressed("down") and dir_y == 0 then next_dir_x, next_dir_y = 0, 1
  elseif btn.pressed("left") and dir_x == 0 then next_dir_x, next_dir_y = -1, 0
  elseif btn.pressed("right") and dir_x == 0 then next_dir_x, next_dir_y = 1, 0
  end
  move_timer = move_timer + 1
  if move_timer >= move_delay then
    move_timer = 0
    step_snake()
  end
end

-- --------------------------------------------------------------------------
-- Desenho
-- --------------------------------------------------------------------------

-- Brasa: um losango de 4x4 px
local function draw_food(x, y)
  oled.fill(x + 1, y, 2, 4)
  oled.fill(x, y + 1, 4, 2)
end

-- Tabuleiro: borda (só na arena trancada), brasa e serpente com a cabeça cheia
local function draw_board()
  if config.arena_mode == "trancado" then
    oled.rect(0, 0, 128, 63)
  end
  draw_food(OFFSET_X + food.x * CELL_SIZE, OFFSET_Y + food.y * CELL_SIZE)
  for i, segment in ipairs(snake) do
    local x, y = OFFSET_X + segment.x * CELL_SIZE, OFFSET_Y + segment.y * CELL_SIZE
    if i == 1 then
      oled.fill(x, y, 4, 4)
    else
      oled.rect(x, y, 4, 4)
    end
  end
end

-- --------------------------------------------------------------------------
-- Ciclo do console virtual
-- --------------------------------------------------------------------------

function init()
  load_config()
  load_ranking()
  refresh_slot_cache()
  player_name = config.last_name
  current_screen = SCREEN_MENU
end

-- Chamado a 30 FPS; não é chamado enquanto um modal do sistema está aberto
function update()
  if current_screen == SCREEN_MENU then
    update_menu()
  elseif current_screen == SCREEN_PLAY then
    update_play()
  elseif current_screen == SCREEN_CONFIG then
    update_config()
  elseif current_screen == SCREEN_RANKING then
    update_ranking()
  end
  -- SCREEN_OVER: as opções estão no modal de fim de jogo
end

function draw()
  oled.clear()
  if current_screen == SCREEN_MENU then
    ui.title("BOITATA")
    ui.list(menu_items(), menu_cursor)
  elseif current_screen == SCREEN_CONFIG then
    ui.title("CONFIGURACOES")
    ui.list(config_items(), config_cursor)
  elseif current_screen == SCREEN_RANKING then
    ui.title("RANKING")
    ui.list(ranking_items(), ranking_cursor)
  else
    draw_board() -- jogando, ou congelado por trás dos modais de pausa/fim
  end
end
