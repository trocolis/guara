-- Pega-Maçã: mova a cesta com esquerda/direita e pegue as maçãs.
-- B abre a pausa (modal do sistema); A reinicia depois do fim de jogo.
-- Demonstra a API: oled, btn, audio, storage, guara.draw e os componentes ui.

local basket_x      -- posição horizontal da cesta (pode ser fracionária)
local apples        -- maçãs caindo: {x, y, speed}
local score         -- maçãs pegas
local lives         -- vidas restantes (perde uma por maçã que cai no chão)
local spawn_timer   -- tempo desde a última maçã criada (segundos)
local game_over     -- verdadeiro depois que as vidas acabam
local best_score    -- recorde salvo no cartão SD

local BASKET_WIDTH = 20
local BASKET_Y = 56
local GROUND_Y = 54  -- altura em que a maçã encosta na cesta

-- Grava o recorde na pasta do jogo (storage cria <chave>.sav)
local function save_best_score()
  if score > best_score then
    best_score = score
    storage.save("best_score", tostring(best_score))
  end
end

function init()
  basket_x = 56
  apples = {}
  score = 0
  lives = 3
  spawn_timer = 0
  game_over = false
  best_score = tonumber(storage.load("best_score") or "0") or 0
end

-- Fim de jogo: salva o recorde e informa o resultado ao Guará
local function finish_game()
  game_over = true
  save_best_score()
  guara.report_result(score >= 10)
  if score > 0 then
    guara.add_exp(score)
  end
  audio.beep(200, 300)
end

-- Pausa com o modal de escolha do sistema: o jogo para enquanto ele está aberto
local function open_pause_menu()
  ui.choice("PAUSA", {"Continuar", "Sair"}, function(option)
    if option == 2 then
      exit()
    end
  end)
end

function update(dt)
  if btn.pressed("b") then
    open_pause_menu()
    return
  end
  if game_over then
    if btn.pressed("a") then init() end
    return
  end

  -- Movimento da cesta (70 px/s), limitado à tela
  if btn.left() then basket_x = basket_x - 70 * dt end
  if btn.right() then basket_x = basket_x + 70 * dt end
  basket_x = math.max(0, math.min(WIDTH - BASKET_WIDTH, basket_x))

  -- Cria maçãs cada vez mais rápido conforme a pontuação sobe
  spawn_timer = spawn_timer + dt
  local spawn_interval = math.max(0.4, 1.0 - score * 0.02)
  if spawn_timer >= spawn_interval then
    spawn_timer = 0
    table.insert(apples, {x = math.random(0, WIDTH - 16), y = 10, speed = 20 + score})
  end

  -- Percorre de trás para frente para poder remover durante o laço
  for i = #apples, 1, -1 do
    local apple = apples[i]
    apple.y = apple.y + apple.speed * dt
    local caught = apple.y + 12 >= GROUND_Y and apple.x + 12 > basket_x and apple.x < basket_x + BASKET_WIDTH
    if caught then
      table.remove(apples, i)
      score = score + 1
      audio.beep(1500, 20)
      -- A cada 10 maçãs, uma maçã de verdade vai para o inventário
      if score % 10 == 0 then
        guara.give_item("apple", 1)
        audio.note(84, 120)
      end
    elseif apple.y > HEIGHT then
      table.remove(apples, i)
      lives = lives - 1
      audio.beep(300, 80)
      if lives <= 0 then finish_game() end
    end
  end
end

function draw()
  -- Placar no topo: pontos à esquerda e as vidas numa barra de progresso
  oled.text(0, 0, "Pontos " .. score)
  ui.progress(WIDTH - 30, 1, 30, 6, lives, 3)
  oled.line(0, 9, WIDTH - 1, 9)

  for _, apple in ipairs(apples) do
    oled.icon("apple", math.floor(apple.x), math.floor(apple.y))
  end

  -- Cesta: retângulo com uma faixa interna
  local x = math.floor(basket_x)
  oled.rect(x, BASKET_Y, BASKET_WIDTH, 7)
  oled.line(x + 2, BASKET_Y + 2, x + BASKET_WIDTH - 3, BASKET_Y + 2)

  if game_over then
    -- Tela de fim: o Guará do jogador comemora (ou fica chateado) em 2x
    oled.clear()
    ui.title("FIM DE JOGO")
    guara.draw(32, 60, {scale = 2, eye = score >= 10 and "happy" or "bored"})
    ui.text_center(64, 64, 22, "Pontos " .. score)
    ui.text_center(64, 64, 34, "Recorde " .. best_score)
    ui.footer("PAUSA", "DE NOVO")
  end
end
