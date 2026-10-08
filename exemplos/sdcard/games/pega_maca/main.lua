-- Pega-Maçã: mova a cesta com esquerda/direita e pegue as maçãs.
-- A reinicia depois do fim de jogo; segure B por 2 s para sair.

local cesta_x, macas, pontos, vidas, relogio, fim, recorde

local function salvar_recorde()
  if pontos > recorde then
    recorde = pontos
    storage.save("recorde", tostring(recorde))
  end
end

function init()
  cesta_x = 56
  macas = {}
  pontos = 0
  vidas = 3
  relogio = 0
  fim = false
  recorde = tonumber(storage.load("recorde") or "0") or 0
end

local function terminar()
  fim = true
  salvar_recorde()
  guara.report_result(pontos >= 10)
  if pontos > 0 then
    guara.add_exp(pontos)
  end
  audio.beep(200, 300)
end

function update(dt)
  if fim then
    if btn.pressed("a") then init() end
    return
  end
  if btn.left() then cesta_x = cesta_x - 70 * dt end
  if btn.right() then cesta_x = cesta_x + 70 * dt end
  cesta_x = math.max(0, math.min(WIDTH - 20, cesta_x))

  relogio = relogio + dt
  local intervalo = math.max(0.4, 1.0 - pontos * 0.02)
  if relogio >= intervalo then
    relogio = 0
    table.insert(macas, {x = math.random(0, WIDTH - 16), y = 10, v = 20 + pontos})
  end

  for i = #macas, 1, -1 do
    local m = macas[i]
    m.y = m.y + m.v * dt
    if m.y + 12 >= 54 and m.x + 12 > cesta_x and m.x < cesta_x + 20 then
      table.remove(macas, i)
      pontos = pontos + 1
      audio.beep(1500, 20)
      if pontos % 10 == 0 then
        guara.give_item("maca", 1)
        audio.note(84, 120)
      end
    elseif m.y > HEIGHT then
      table.remove(macas, i)
      vidas = vidas - 1
      audio.beep(300, 80)
      if vidas <= 0 then terminar() end
    end
  end
end

function draw()
  oled.text(0, 0, "Pontos " .. pontos)
  oled.text(WIDTH - oled.textwidth("Vidas " .. vidas), 0, "Vidas " .. vidas)
  oled.line(0, 9, WIDTH - 1, 9)
  for _, m in ipairs(macas) do
    oled.icon("apple", math.floor(m.x), math.floor(m.y))
  end
  local x = math.floor(cesta_x)
  oled.rect(x, 56, 20, 7)
  oled.line(x + 2, 58, x + 17, 58)
  if fim then
    oled.fill(14, 20, 100, 28, 0)
    oled.rect(14, 20, 100, 28)
    local t1, t2 = "FIM DE JOGO", "Recorde " .. recorde
    oled.text(64 - oled.textwidth(t1) // 2, 25, t1)
    oled.text(64 - oled.textwidth(t2) // 2, 36, t2)
  end
end
