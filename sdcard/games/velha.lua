-- ==========================================
-- __META__
-- title: Jogo da Velha
-- author: Petnator Team
-- version: 1.0
-- icon24:
-- ........................
-- ....#...#......#...#....
-- .....#.#.......#...#....
-- ......#........#...#....
-- .....#.#.......#...#....
-- ....#...#......#...#....
-- ########################
-- ########################
-- .......#####...#...#....
-- ......#.....#..#...#....
-- ......#.....#..#...#....
-- ......#.....#..#...#....
-- .......#####...#...#....
-- ########################
-- ########################
-- ....#...#......#...#....
-- .....#.#.......#...#....
-- ......#........#...#....
-- .....#.#.......#...#....
-- ....#...#......#...#....
-- ....#...#......#...#....
-- ........................
-- ........................
-- ........................
-- ==========================================
-- __LUA__
local board = {0,0,0, 0,0,0, 0,0,0}
local cx, cy = 1, 1
local turn = 1
local winner = 0

function init()
    reset_game()
end

function reset_game()
    for i = 1, 9 do board[i] = 0 end
    cx, cy = 1, 1
    turn = 1
    winner = 0
end

function check_winner()
    local wins = {
        {1,2,3}, {4,5,6}, {7,8,9},
        {1,4,7}, {2,5,8}, {3,6,9},
        {1,5,9}, {3,5,7}
    }
    for _, w in ipairs(wins) do
        if board[w[1]] ~= 0 and board[w[1]] == board[w[2]] and board[w[2]] == board[w[3]] then
            return board[w[1]]
        end
    end
    for i = 1, 9 do if board[i] == 0 then return 0 end end
    return 3
end

function update()
    if winner ~= 0 then
        if btn.pressed("a") then reset_game(); audio.beep(600, 80)
        elseif btn.pressed("b") then audio.beep(400, 80); sair() end
        return
    end
    if btn.pressed("b") then audio.beep(400, 80); sair(); return end
    if btn.pressed("left")  and cx > 0 then cx = cx - 1; audio.beep(300, 30) end
    if btn.pressed("right") and cx < 2 then cx = cx + 1; audio.beep(300, 30) end
    if btn.pressed("up")    and cy > 0 then cy = cy - 1; audio.beep(300, 30) end
    if btn.pressed("down")  and cy < 2 then cy = cy + 1; audio.beep(300, 30) end
    if btn.pressed("a") then
        local idx = cy * 3 + cx + 1
        if board[idx] == 0 then
            board[idx] = turn
            audio.beep(turn == 1 and 600 or 800, 60)
            winner = check_winner()
            if winner == 0 then turn = (turn == 1) and 2 or 1
            else audio.beep(winner == 3 and 250 or 900, 200) end
        else
            audio.beep(150, 80)
        end
    end
end

function draw()
    oled.clear()
    oled.text(24, 1, "JOGO DA VELHA")
    local ox, oy, sz = 42, 14, 14
    oled.line(ox + sz, oy, ox + sz, oy + sz * 3)
    oled.line(ox + sz * 2, oy, ox + sz * 2, oy + sz * 3)
    oled.line(ox, oy + sz, ox + sz * 3, oy + sz)
    oled.line(ox, oy + sz * 2, ox + sz * 3, oy + sz * 2)
    for r = 0, 2 do
        for c = 0, 2 do
            local mark = board[r * 3 + c + 1]
            local px = ox + c * sz + 3
            local py = oy + r * sz + 3
            if mark == 1 then
                oled.line(px, py, px + 7, py + 7)
                oled.line(px + 7, py, px, py + 7)
            elseif mark == 2 then
                oled.circle(px + 3, py + 3, 4)
            end
        end
    end
    oled.rect(ox + cx * sz, oy + cy * sz, sz, sz)
    if winner == 0 then
        oled.text(2, 54, "Vez:" .. (turn == 1 and "X" or "O"))
        oled.text(52, 54, "A:Poe B:Sair")
    elseif winner == 1 then oled.text(2, 54, "Venceu:X! A:Novo B:Sair")
    elseif winner == 2 then oled.text(2, 54, "Venceu:O! A:Novo B:Sair")
    else oled.text(2, 54, "Velha! A:Novo B:Sair") end
end
