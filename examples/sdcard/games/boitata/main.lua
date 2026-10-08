-- BOITATA - VIRTUAL CONSOLE EDITION (REFINED)
local STATE_MENU        = 1
local STATE_PLAY        = 2
local STATE_PAUSE       = 3
local STATE_CONFIG      = 4
local STATE_GAMEOVER    = 5
local STATE_RANKING     = 6
local STATE_NAME_ENTRY  = 7
local STATE_SLOT_SELECT = 8

local current_state = STATE_MENU

local GRID_W = 25
local GRID_H = 12
local CELL_SZ = 5
local OFFSET_X = 2
local OFFSET_Y = 2

local cfg = {
    speed_type = "progressiva",
    speed_level = 2,
    arena_mode = "trancado",
    last_name = "BOI"
}

local snake = {}
local dir_x, dir_y = 1, 0
local next_dir_x, next_dir_y = 1, 0
local food = {x = 10, y = 6}
local score = 0
local move_timer = 0
local current_speed_delay = 5
local current_player_name = "BOI"
local current_slot = 1
local slot_mode = "new"

local menu_cursor = 1
local menu_opts = {"Novo Jogo", "Continuar", "Configurar", "Ranking", "Sair"}

local pause_cursor = 1
local pause_opts = {"Continuar", "Salvar", "Sair"}
local pause_msg = ""
local pause_timer = 0

local gameover_cursor = 1
local slot_cursor = 1
local config_cursor = 1

local ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789"
local ALPHABET_LEN = #ALPHABET
local name_chars = {2, 15, 9} -- B, O, I
local name_cursor = 1

local ranking = {
    {name = "BOI", score = 150},
    {name = "PET", score = 100},
    {name = "SNA", score = 70},
    {name = "LUA", score = 40},
    {name = "DEV", score = 20}
}

local status_msg = ""
local status_timer = 0

local function center_x(text, container_w, container_x)
    container_w = container_w or 128
    container_x = container_x or 0
    local x = container_x + math.floor((container_w - (#text * 6)) / 2)
    return x < 0 and 0 or x
end

-- Arquivos na pasta do jogo via storage.save/load (<chave>.sav)
local CFG_KEY = "boitata_user"
local RNK_KEY = "boitata_rnk"

local function get_save_key(slot)
    return string.format("boitata_%d", slot or 1)
end

-- Itera as linhas de um save, ou retorna nil se ele não existir
local function save_lines(key)
    local data = storage.load(key)
    if not data then return nil end
    return data:gmatch("[^\r\n]+")
end

local function char_at(idx)
    return ALPHABET:sub(idx, idx)
end

local function set_name_from_str(str)
    str = (str or "BOI"):upper()
    for i = 1, 3 do
        local c = str:sub(i, i)
        name_chars[i] = (c ~= "") and (ALPHABET:find(c, 1, true) or 1) or 27
    end
end

local function get_name_from_chars()
    return char_at(name_chars[1]) .. char_at(name_chars[2]) .. char_at(name_chars[3])
end

local function save_configs()
    storage.save(CFG_KEY, "[CONFIG]\nspeed_type=" .. cfg.speed_type ..
                          "\nspeed_level=" .. tostring(cfg.speed_level) ..
                          "\narena_mode=" .. cfg.arena_mode ..
                          "\nlast_name=" .. (cfg.last_name or "BOI") .. "\n")
end

local function load_configs()
    local lines = save_lines(CFG_KEY)
    if not lines then return end
    for line in lines do
        local k, v = line:match("^([^=]+)%s*=%s*(.+)$")
        if k and v then
            k = k:match("^%s*(.-)%s*$"); v = v:match("^%s*(.-)%s*$")
            if k == "speed_type" then cfg.speed_type = v
            elseif k == "speed_level" then cfg.speed_level = tonumber(v) or 2
            elseif k == "arena_mode" then cfg.arena_mode = v
            elseif k == "last_name" and #v > 0 then cfg.last_name = v:sub(1, 3):upper() end
        end
    end
    set_name_from_str(cfg.last_name)
end

local function save_ranking()
    local out = {}
    for i = 1, math.min(#ranking, 5) do
        table.insert(out, ranking[i].name .. "=" .. tostring(ranking[i].score))
    end
    storage.save(RNK_KEY, table.concat(out, "\n") .. "\n")
end

local function load_ranking()
    local lines = save_lines(RNK_KEY)
    if not lines then return end
    local loaded = {}
    for line in lines do
        local n, s = line:match("^([^=]+)=(%d+)$")
        if n and s then table.insert(loaded, {name = n:sub(1, 3):upper(), score = tonumber(s)}) end
    end
    if #loaded > 0 then ranking = loaded end
end

local slot_cached_meta = {}

local function read_slot_meta(slot)
    local lines = save_lines(get_save_key(slot))
    if not lines and slot == 1 then lines = save_lines("boitata") end
    if not lines then return {exists = false, name = "---", score = 0} end
    local meta = {exists = true, name = "BOI", score = 0}
    for line in lines do
        local k, v = line:match("^([^=]+)%s*=%s*(.+)$")
        if k and v then
            k = k:match("^%s*(.-)%s*$"); v = v:match("^%s*(.-)%s*$")
            if k == "name" then meta.name = v:sub(1, 3):upper()
            elseif k == "score" then meta.score = tonumber(v) or 0 end
        end
    end
    return meta
end

local function refresh_slot_cache()
    for s = 1, 3 do
        slot_cached_meta[s] = read_slot_meta(s)
    end
end

local function get_slot_meta(slot)
    slot = slot or 1
    if not slot_cached_meta[slot] then
        slot_cached_meta[slot] = read_slot_meta(slot)
    end
    return slot_cached_meta[slot]
end

local function any_save_exists()
    for s = 1, 3 do
        if get_slot_meta(s).exists then return true end
    end
    return false
end

local function save_game_state(slot)
    slot = slot or current_slot or 1
    local b = {}
    for _, seg in ipairs(snake) do table.insert(b, seg.x .. "," .. seg.y) end
    local ok = storage.save(get_save_key(slot), "name=" .. (current_player_name or "BOI") ..
            "\nscore=" .. tostring(score) ..
            "\ndir_x=" .. tostring(dir_x) ..
            "\ndir_y=" .. tostring(dir_y) ..
            "\nfood_x=" .. tostring(food.x) ..
            "\nfood_y=" .. tostring(food.y) ..
            "\nspeed_type=" .. cfg.speed_type ..
            "\nspeed_level=" .. tostring(cfg.speed_level) ..
            "\narena_mode=" .. cfg.arena_mode ..
            "\nsnake=" .. table.concat(b, ";") .. "\n")
    if not ok then return false end
    slot_cached_meta[slot] = {
        exists = true,
        name = (current_player_name or "BOI"):sub(1, 3):upper(),
        score = score or 0
    }
    return true
end

local function load_game_state(slot)
    slot = slot or current_slot or 1
    local lines = save_lines(get_save_key(slot))
    if not lines and slot == 1 then lines = save_lines("boitata") end
    if not lines then return false end

    local loaded_snake = {}
    for line in lines do
        local k, v = line:match("^([^=]+)%s*=%s*(.+)$")
        if k and v then
            k = k:match("^%s*(.-)%s*$"); v = v:match("^%s*(.-)%s*$")
            if k == "name" then current_player_name = v:sub(1, 3):upper()
            elseif k == "score" then score = tonumber(v) or 0
            elseif k == "dir_x" then dir_x = tonumber(v) or 1
            elseif k == "dir_y" then dir_y = tonumber(v) or 0
            elseif k == "food_x" then food.x = tonumber(v) or 10
            elseif k == "food_y" then food.y = tonumber(v) or 6
            elseif k == "speed_type" then cfg.speed_type = v
            elseif k == "speed_level" then cfg.speed_level = tonumber(v) or 2
            elseif k == "arena_mode" then cfg.arena_mode = v
            elseif k == "snake" then
                for p in v:gmatch("([^;]+)") do
                    local sx, sy = p:match("^(%d+),(%d+)$")
                    if sx and sy then table.insert(loaded_snake, {x = tonumber(sx), y = tonumber(sy)}) end
                end
            end
        end
    end

    if #loaded_snake >= 3 then
        snake = loaded_snake
        if dir_x == 0 and dir_y == 0 then
            dir_x = (snake[1].x >= snake[2].x) and 1 or -1
            dir_y = 0
        end
        next_dir_x, next_dir_y = dir_x, dir_y
        move_timer = 0
        current_slot = slot
        set_name_from_str(current_player_name)
        return true
    end
    return false
end

local function get_current_delay()
    if cfg.speed_type == "fixa" then
        local delays = {6, 4, 3, 2}
        return delays[cfg.speed_level] or 4
    end
    local d = 6 - math.floor(score / 50)
    return d < 2 and 2 or d
end

local function spawn_food()
    for _ = 1, 100 do
        food.x = math.random(GRID_W) - 1
        food.y = math.random(GRID_H) - 1
        local ok = true
        for _, s in ipairs(snake) do
            if s.x == food.x and s.y == food.y then ok = false; break end
        end
        if ok then break end
    end
end

local function start_new_game()
    snake = { {x = 10, y = 6}, {x = 9, y = 6}, {x = 8, y = 6} }
    dir_x, dir_y = 1, 0
    next_dir_x, next_dir_y = 1, 0
    score, move_timer = 0, 0
    current_speed_delay = get_current_delay()
    spawn_food()
    current_state = STATE_PLAY
    audio.beep(523, 30)
end

local function check_and_submit_ranking()
    local p = current_player_name or "BOI"
    table.insert(ranking, {name = (p ~= "") and p or "BOI", score = score})
    table.sort(ranking, function(a, b) return a.score > b.score end)
    while #ranking > 5 do table.remove(ranking) end
    save_ranking()
end

local function draw_food(fx, fy)
    oled.pixel(fx + 1, fy, 1); oled.pixel(fx + 2, fy, 1)
    oled.pixel(fx, fy + 1, 1); oled.pixel(fx + 1, fy + 1, 1); oled.pixel(fx + 2, fy + 1, 1); oled.pixel(fx + 3, fy + 1, 1)
    oled.pixel(fx, fy + 2, 1); oled.pixel(fx + 1, fy + 2, 1); oled.pixel(fx + 2, fy + 2, 1); oled.pixel(fx + 3, fy + 2, 1)
    oled.pixel(fx + 1, fy + 3, 1); oled.pixel(fx + 2, fy + 3, 1)
end

function init()
    load_configs()
    load_ranking()
    refresh_slot_cache()
    current_state = STATE_MENU
end

function update()
    if status_timer > 0 then status_timer = status_timer - 1 end
    if pause_timer > 0 then
        pause_timer = pause_timer - 1
        if pause_timer == 0 then pause_msg = "" end
    end

    if current_state == STATE_MENU then
        if btn.pressed("up") then
            menu_cursor = (menu_cursor - 2 + #menu_opts) % #menu_opts + 1; audio.beep(440, 15)
        elseif btn.pressed("down") then
            menu_cursor = menu_cursor % #menu_opts + 1; audio.beep(440, 15)
        end

        if btn.pressed("a") then
            if menu_cursor == 1 then
                set_name_from_str(cfg.last_name or "BOI")
                name_cursor = 1
                current_state = STATE_NAME_ENTRY
                audio.beep(523, 20)
            elseif menu_cursor == 2 then
                if any_save_exists() then
                    slot_cursor = 1; slot_mode = "load"
                    current_state = STATE_SLOT_SELECT
                    audio.beep(523, 20)
                else
                    status_msg = "Nenhum save encontrado!"
                    status_timer = 45; audio.beep(220, 50)
                end
            elseif menu_cursor == 3 then
                config_cursor = 1; current_state = STATE_CONFIG; audio.beep(523, 20)
            elseif menu_cursor == 4 then
                current_state = STATE_RANKING; audio.beep(523, 20)
            elseif menu_cursor == 5 then
                audio.beep(330, 40); exit()
            end
        end

    elseif current_state == STATE_NAME_ENTRY then
        if btn.pressed("down") then
            name_chars[name_cursor] = name_chars[name_cursor] - 1
            if name_chars[name_cursor] < 1 then name_chars[name_cursor] = ALPHABET_LEN end
            audio.beep(523, 15)
        elseif btn.pressed("up") then
            name_chars[name_cursor] = name_chars[name_cursor] + 1
            if name_chars[name_cursor] > ALPHABET_LEN then name_chars[name_cursor] = 1 end
            audio.beep(523, 15)
        end

        if btn.pressed("left") and name_cursor > 1 then
            name_cursor = name_cursor - 1; audio.beep(440, 15)
        elseif btn.pressed("right") and name_cursor < 3 then
            name_cursor = name_cursor + 1; audio.beep(440, 15)
        end

        if btn.pressed("a") then
            if name_cursor < 3 then
                name_cursor = name_cursor + 1; audio.beep(523, 20)
            else
                current_player_name = get_name_from_chars()
                cfg.last_name = current_player_name
                save_configs(); audio.beep(659, 30)
                slot_cursor = 1; slot_mode = "new"
                current_state = STATE_SLOT_SELECT
            end
        elseif btn.pressed("b") then
            if name_cursor > 1 then
                name_cursor = name_cursor - 1; audio.beep(440, 15)
            else
                current_state = STATE_MENU; audio.beep(330, 20)
            end
        end

    elseif current_state == STATE_SLOT_SELECT then
        if btn.pressed("up") then
            slot_cursor = (slot_cursor - 2 + 3) % 3 + 1; audio.beep(440, 15)
        elseif btn.pressed("down") then
            slot_cursor = slot_cursor % 3 + 1; audio.beep(440, 15)
        end

        if btn.pressed("a") then
            current_slot = slot_cursor
            if slot_mode == "new" then
                audio.beep(659, 40); start_new_game()
            else
                local meta = get_slot_meta(slot_cursor)
                if meta.exists and load_game_state(slot_cursor) then
                    current_speed_delay = get_current_delay()
                    current_state = STATE_PLAY; audio.beep(659, 40)
                else
                    status_msg = "Slot vazio!"; status_timer = 40; audio.beep(220, 50)
                end
            end
        elseif btn.pressed("b") then
            current_state = (slot_mode == "new") and STATE_NAME_ENTRY or STATE_MENU
            if slot_mode == "new" then name_cursor = 3 end
            audio.beep(330, 20)
        end

    elseif current_state == STATE_PLAY then
        if btn.pressed("b") then
            pause_cursor = 1; pause_msg = ""; pause_timer = 0
            current_state = STATE_PAUSE; audio.beep(523, 25)
            return
        end

        if btn.pressed("up") and dir_y == 0 then next_dir_x = 0; next_dir_y = -1
        elseif btn.pressed("down") and dir_y == 0 then next_dir_x = 0; next_dir_y = 1
        elseif btn.pressed("left") and dir_x == 0 then next_dir_x = -1; next_dir_y = 0
        elseif btn.pressed("right") and dir_x == 0 then next_dir_x = 1; next_dir_y = 0
        end

        move_timer = move_timer + 1
        if move_timer >= current_speed_delay then
            move_timer = 0
            dir_x, dir_y = next_dir_x, next_dir_y

            local head = snake[1]
            local new_x = head.x + dir_x
            local new_y = head.y + dir_y

            if cfg.arena_mode == "trancado" then
                if new_x < 0 or new_x >= GRID_W or new_y < 0 or new_y >= GRID_H then
                    audio.beep(180, 100); gameover_cursor = 1; current_state = STATE_GAMEOVER; return
                end
            else
                if new_x < 0 then new_x = GRID_W - 1 elseif new_x >= GRID_W then new_x = 0 end
                if new_y < 0 then new_y = GRID_H - 1 elseif new_y >= GRID_H then new_y = 0 end
            end

            local check_end = (new_x == food.x and new_y == food.y) and #snake or (#snake - 1)
            for i = 2, check_end do
                if snake[i].x == new_x and snake[i].y == new_y then
                    audio.beep(180, 100); gameover_cursor = 1; current_state = STATE_GAMEOVER; return
                end
            end

            table.insert(snake, 1, {x = new_x, y = new_y})
            if new_x == food.x and new_y == food.y then
                score = score + 10; audio.beep(880, 35)
                current_speed_delay = get_current_delay()
                spawn_food()
            else
                table.remove(snake)
            end
        end

    elseif current_state == STATE_PAUSE then
        if btn.pressed("up") then
            pause_cursor = (pause_cursor - 2 + #pause_opts) % #pause_opts + 1; audio.beep(440, 15)
        elseif btn.pressed("down") then
            pause_cursor = pause_cursor % #pause_opts + 1; audio.beep(440, 15)
        end

        if btn.pressed("a") then
            if pause_cursor == 1 then
                current_state = STATE_PLAY; audio.beep(523, 20)
            elseif pause_cursor == 2 then
                if save_game_state(current_slot) then
                    pause_msg = "Salvo no Slot " .. current_slot .. "!"
                    pause_timer = 50; audio.beep(659, 40)
                else
                    pause_msg = "Erro ao salvar!"
                    pause_timer = 50; audio.beep(220, 60)
                end
            elseif pause_cursor == 3 then
                current_state = STATE_MENU; audio.beep(330, 30)
            end
        elseif btn.pressed("b") then
            current_state = STATE_PLAY; audio.beep(523, 20)
        end

    elseif current_state == STATE_CONFIG then
        if btn.pressed("up") then
            config_cursor = (config_cursor - 2 + 4) % 4 + 1; audio.beep(440, 15)
        elseif btn.pressed("down") then
            config_cursor = config_cursor % 4 + 1; audio.beep(440, 15)
        end

        if config_cursor == 1 and (btn.pressed("left") or btn.pressed("right") or btn.pressed("a")) then
            cfg.speed_type = (cfg.speed_type == "progressiva") and "fixa" or "progressiva"
            save_configs(); audio.beep(523, 20)
        elseif config_cursor == 2 and cfg.speed_type == "fixa" then
            if btn.pressed("left") then
                cfg.speed_level = (cfg.speed_level - 2 + 4) % 4 + 1; save_configs(); audio.beep(523, 20)
            elseif btn.pressed("right") or btn.pressed("a") then
                cfg.speed_level = cfg.speed_level % 4 + 1; save_configs(); audio.beep(523, 20)
            end
        elseif config_cursor == 3 and (btn.pressed("left") or btn.pressed("right") or btn.pressed("a")) then
            cfg.arena_mode = (cfg.arena_mode == "trancado") and "infinito" or "trancado"
            save_configs(); audio.beep(523, 20)
        elseif config_cursor == 4 and (btn.pressed("a") or btn.pressed("b")) then
            save_configs(); current_state = STATE_MENU; audio.beep(659, 30)
        end

        if btn.pressed("b") and config_cursor ~= 4 then
            save_configs(); current_state = STATE_MENU; audio.beep(330, 20)
        end

    elseif current_state == STATE_GAMEOVER then
        local has_save = get_slot_meta(current_slot).exists
        local max_opts = has_save and 3 or 2

        if btn.pressed("up") then
            gameover_cursor = (gameover_cursor - 2 + max_opts) % max_opts + 1; audio.beep(440, 15)
        elseif btn.pressed("down") then
            gameover_cursor = gameover_cursor % max_opts + 1; audio.beep(440, 15)
        end

        if btn.pressed("a") then
            if has_save then
                if gameover_cursor == 1 then
                    if load_game_state(current_slot) then
                        current_speed_delay = get_current_delay()
                        current_state = STATE_PLAY; audio.beep(659, 40)
                    else
                        status_msg = "Erro ao recarregar!"; status_timer = 40; audio.beep(220, 50)
                    end
                elseif gameover_cursor == 2 then
                    check_and_submit_ranking(); audio.beep(523, 30); start_new_game()
                elseif gameover_cursor == 3 then
                    check_and_submit_ranking(); audio.beep(440, 20); current_state = STATE_MENU
                end
            else
                if gameover_cursor == 1 then
                    check_and_submit_ranking(); audio.beep(523, 30); start_new_game()
                elseif gameover_cursor == 2 then
                    check_and_submit_ranking(); audio.beep(440, 20); current_state = STATE_MENU
                end
            end
        elseif btn.pressed("b") then
            check_and_submit_ranking(); current_state = STATE_MENU; audio.beep(330, 20)
        end

    elseif current_state == STATE_RANKING and (btn.pressed("a") or btn.pressed("b")) then
        current_state = STATE_MENU; audio.beep(440, 20)
    end
end

function draw()
    oled.clear()

    if current_state == STATE_MENU then
        oled.fill(0, 0, 128, 11, 1)
        oled.text(center_x("BOITATA"), 2, "BOITATA", 0)

        for i, opt in ipairs(menu_opts) do
            local y = 14 + (i - 1) * 9
            local label = (opt == "Continuar" and not any_save_exists()) and "Continuar (-)" or opt
            local tx = center_x(label)
            if i == menu_cursor then
                oled.fill(14, y - 1, 100, 9, 1); oled.text(tx, y, label, 0)
            else
                oled.text(tx, y, label, 1)
            end
        end

        if status_timer > 0 and #status_msg > 0 then
            oled.fill(8, 53, 112, 10, 1)
            oled.text(center_x(status_msg), 54, status_msg, 0)
        end

    elseif current_state == STATE_NAME_ENTRY then
        oled.fill(0, 0, 128, 11, 1)
        oled.text(center_x("NOVO JOGO"), 2, "NOVO JOGO", 0)
        oled.text(center_x("DIGITE SEU NOME:"), 17, "DIGITE SEU NOME:", 1)

        for i = 1, 3 do
            local ch = char_at(name_chars[i])
            local sx = 41 + (i - 1) * 16
            if i == name_cursor then
                oled.fill(sx, 30, 13, 14, 1)
                if ch == " " then oled.line(sx + 3, 40, sx + 9, 40, 0) else oled.text(sx + 4, 33, ch, 0) end
            else
                oled.rect(sx, 30, 13, 14, 1)
                if ch == " " then oled.line(sx + 3, 40, sx + 9, 40, 1) else oled.text(sx + 4, 33, ch, 1) end
            end
        end
        local tip = "[D-PAD] Altera  [A] OK"
        oled.text(center_x(tip), 52, tip, 1)

    elseif current_state == STATE_SLOT_SELECT then
        oled.fill(0, 0, 128, 11, 1)
        local hdr = (slot_mode == "new") and "SALVAR EM QUAL SLOT?" or "CARREGAR JOGO SALVO"
        oled.text(center_x(hdr), 2, hdr, 0)

        for s = 1, 3 do
            local y = 16 + (s - 1) * 12
            local m = get_slot_meta(s)
            local lbl = m.exists and string.format("Slot %d: [%s] %04d pts", s, m.name, m.score)
                                 or string.format("Slot %d: [ Vazio ]", s)
            local tx = center_x(lbl)
            if s == slot_cursor then
                oled.fill(4, y - 1, 120, 11, 1); oled.text(tx, y + 1, lbl, 0)
            else
                oled.rect(4, y - 1, 120, 11, 1); oled.text(tx, y + 1, lbl, 1)
            end
        end
        local tip = (slot_mode == "new") and "[A] Iniciar  [B] Voltar" or "[A] Carregar  [B] Voltar"
        oled.text(center_x(tip), 53, tip, 1)

    elseif current_state == STATE_PLAY then
        if cfg.arena_mode == "trancado" then oled.rect(0, 0, 128, 63, 1) end
        draw_food(OFFSET_X + food.x * CELL_SZ, OFFSET_Y + food.y * CELL_SZ)
        for _, s in ipairs(snake) do
            oled.rect(OFFSET_X + s.x * CELL_SZ, OFFSET_Y + s.y * CELL_SZ, 4, 4, 1)
        end

    elseif current_state == STATE_PAUSE then
        if cfg.arena_mode == "trancado" then oled.rect(0, 0, 128, 63, 1) end
        draw_food(OFFSET_X + food.x * CELL_SZ, OFFSET_Y + food.y * CELL_SZ)
        for _, s in ipairs(snake) do
            oled.rect(OFFSET_X + s.x * CELL_SZ, OFFSET_Y + s.y * CELL_SZ, 4, 4, 1)
        end

        oled.fill(14, 5, 100, 54, 0); oled.rect(14, 5, 100, 54, 1)
        oled.fill(16, 7, 96, 10, 1)
        oled.text(center_x("PAUSA", 96, 16), 8, "PAUSA", 0)

        local slot_info = string.format("Slot %d: %s | %d pts", current_slot, current_player_name or "BOI", score)
        oled.text(center_x(slot_info, 100, 14), 19, slot_info, 1)

        if pause_timer > 0 and #pause_msg > 0 then
            oled.fill(18, 27, 92, 9, 1)
            oled.text(center_x(pause_msg, 96, 16), 28, pause_msg, 0)
        end

        for i, opt in ipairs(pause_opts) do
            local y = 30 + (i - 1) * 9
            local tx = center_x(opt, 96, 16)
            if i == pause_cursor then
                oled.fill(18, y - 1, 92, 9, 1); oled.text(tx, y, opt, 0)
            else
                oled.text(tx, y, opt, 1)
            end
        end

    elseif current_state == STATE_CONFIG then
        oled.fill(0, 0, 128, 11, 1)
        oled.text(center_x("CONFIGURACOES"), 2, "CONFIGURACOES", 0)
        local spd_names = {"Lenta", "Normal", "Rapida", "Turbo"}
        local rows = {
            (cfg.speed_type == "progressiva") and "Velocidade: Progressiva" or "Velocidade: Fixa",
            (cfg.speed_type == "fixa") and ("Nivel: " .. (spd_names[cfg.speed_level] or "Normal")) or "Nivel: ---",
            (cfg.arena_mode == "trancado") and "Arena: Trancada" or "Arena: Infinita",
            "Salvar e Voltar"
        }
        for i, text in ipairs(rows) do
            local y = 16 + (i - 1) * 11
            local tx = center_x(text)
            if i == config_cursor then
                oled.fill(6, y - 1, 116, 10, 1); oled.text(tx, y, text, 0)
            else
                oled.text(tx, y, text, 1)
            end
        end

    elseif current_state == STATE_GAMEOVER then
        local has_save = get_slot_meta(current_slot).exists
        oled.fill(10, 4, 108, 56, 0); oled.rect(10, 4, 108, 56, 1)
        oled.fill(12, 6, 104, 10, 1)
        oled.text(center_x("FIM DE JOGO!", 104, 12), 7, "FIM DE JOGO!", 0)

        local s_str = string.format("[%s] Pontos: %d", current_player_name or "BOI", score)
        oled.text(center_x(s_str, 108, 10), 18, s_str, 1)

        local go_opts = has_save and {"Retornar ao Save", "Novo Jogo", "Menu Principal"}
                                 or {"Novo Jogo", "Menu Principal"}
        for i, opt in ipairs(go_opts) do
            local y = 28 + (i - 1) * 9
            local tx = center_x(opt, 104, 12)
            if i == gameover_cursor then
                oled.fill(14, y - 1, 100, 9, 1); oled.text(tx, y, opt, 0)
            else
                oled.text(tx, y, opt, 1)
            end
        end

    elseif current_state == STATE_RANKING then
        oled.fill(0, 0, 128, 11, 1)
        oled.text(center_x("=== RANKING ==="), 2, "=== RANKING ===", 0)
        for i = 1, 5 do
            local y = 16 + (i - 1) * 9
            local r = ranking[i]
            local line_str = r and string.format("%d. %-3s ..... %04d", i, r.name, r.score)
                               or string.format("%d. --- ..... 0000", i)
            oled.text(center_x(line_str), y, line_str, 1)
        end
    end
end
