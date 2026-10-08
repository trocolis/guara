-- ==========================================
-- __META__
-- title: Jogo da Forca
-- author: Petnator Team
-- version: 1.0
-- icon24:
-- ........................
-- ....#############.......
-- ....#...........#.......
-- ....#...........#.......
-- ....#.........#####.....
-- ....#........#.....#....
-- ....#........#.....#....
-- ....#.........#####.....
-- ....#...........#.......
-- ....#.........#####.....
-- ....#........#..#..#....
-- ....#.......#...#...#...
-- ....#......#....#....#..
-- ....#...........#.......
-- ....#..........#.#......
-- ....#.........#...#.....
-- ....#........#.....#....
-- ....#.......#.......#...
-- ....#...................
-- ....#...................
-- ..#####.................
-- .#######................
-- #########...............
-- ........................
-- ==========================================
-- __LUA__
local words = {"PETNATOR", "ROBO", "ESP32", "CONSOLE", "GAMING", "ARDUBOY", "PIXEL", "BATERIA", "WIFI"}
local secret = ""
local revealed = {}
local letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
local let_idx = 1
local errors = 0
local max_errors = 6
local game_over = 0
local used = {}

function init()
    new_game()
end

function new_game()
    local w_idx = math.random(#words)
    if w_idx < 1 then w_idx = 1 end
    secret = words[w_idx]
    revealed = {}
    used = {}
    for i = 1, #secret do revealed[i] = false end
    let_idx = 1
    errors = 0
    game_over = 0
    audio.beep(500, 60)
end

function check_win()
    for i = 1, #secret do
        if not revealed[i] then return false end
    end
    return true
end

function try_letter(ch)
    if used[ch] then return end
    used[ch] = true
    local hit = false
    for i = 1, #secret do
        if secret:sub(i, i) == ch then
            revealed[i] = true
            hit = true
        end
    end
    if hit then
        audio.beep(700, 80)
        if check_win() then game_over = 1; audio.beep(900, 250) end
    else
        errors = errors + 1
        audio.beep(200, 100)
        if errors >= max_errors then game_over = 2; audio.beep(150, 300) end
    end
end

function update()
    if game_over ~= 0 then
        if btn.pressed("a") then new_game()
        elseif btn.pressed("b") then audio.beep(400, 80); exit() end
        return
    end
    if btn.pressed("b") then audio.beep(400, 80); exit(); return end
    if btn.pressed("left")  and let_idx > 1  then let_idx = let_idx - 1; audio.beep(350, 20) end
    if btn.pressed("right") and let_idx < 26 then let_idx = let_idx + 1; audio.beep(350, 20) end
    if btn.pressed("up")    and let_idx > 7  then let_idx = let_idx - 7; audio.beep(350, 20) end
    if btn.pressed("down")  and let_idx <= 19 then let_idx = let_idx + 7; audio.beep(350, 20) end
    if btn.pressed("a") then
        local cur_char = letters:sub(let_idx, let_idx)
        try_letter(cur_char)
    end
end

function draw()
    oled.clear()
    oled.line(6, 52, 34, 52)
    oled.line(14, 52, 14, 12)
    oled.line(14, 12, 30, 12)
    oled.line(30, 12, 30, 18)
    if errors >= 1 then oled.circle(30, 22, 3) end
    if errors >= 2 then oled.line(30, 25, 30, 36) end
    if errors >= 3 then oled.line(30, 28, 25, 33) end
    if errors >= 4 then oled.line(30, 28, 35, 33) end
    if errors >= 5 then oled.line(30, 36, 25, 45) end
    if errors >= 6 then oled.line(30, 36, 35, 45) end
    local word_disp = ""
    for i = 1, #secret do
        if revealed[i] or game_over == 2 then
            word_disp = word_disp .. secret:sub(i, i) .. " "
        else
            word_disp = word_disp .. "_ "
        end
    end
    oled.text(42, 14, word_disp)
    local cur_char = letters:sub(let_idx, let_idx)
    local sel_txt = "Letra:[" .. cur_char .. "]"
    if used[cur_char] then sel_txt = sel_txt .. "*" end
    oled.text(42, 30, sel_txt)
    oled.text(42, 42, "Erros: " .. errors .. "/6")
    if game_over == 0 then oled.text(16, 56, "A:Tentar B:Sair")
    elseif game_over == 1 then oled.text(4, 56, "VENCEU! A:Novo B:Sair")
    else oled.text(4, 56, "PERDEU! A:Novo B:Sair") end
end
