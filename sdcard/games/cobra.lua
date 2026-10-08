-- ==========================================
-- __META__
-- title: Jogo da Cobrinha
-- author: Petnator Team
-- version: 1.0
-- icon24:
-- ........................
-- .........######.........
-- .......##########.......
-- ......############......
-- .....###..####..###.....
-- .....###..####..###.....
-- .....##############.....
-- ......############......
-- .......##########...#...
-- .........######....##...
-- ..........####....###...
-- .........######..####...
-- .......##############...
-- ......################..
-- .....#####......######..
-- ....#####........#####..
-- ....####..........####..
-- ....####..........####..
-- ....#####........#####..
-- .....######....######...
-- ......##############....
-- .......############.....
-- .........########.......
-- ........................
-- ==========================================
-- __LUA__
local snake = {}
local dir_x, dir_y = 1, 0
local next_dx, next_dy = 1, 0
local food = {x = 10, y = 6}
local score = 0
local dead = false
local move_tick = 0
local speed = 4

function init()
    reset_snake()
end

function reset_snake()
    snake = {
        {x = 10, y = 7},
        {x = 9, y = 7},
        {x = 8, y = 7}
    }
    dir_x, dir_y = 1, 0
    next_dx, next_dy = 1, 0
    score = 0
    dead = false
    spawn_food()
    audio.beep(500, 60)
end

function spawn_food()
    food.x = math.random(29) - 1
    food.y = math.random(12) - 1
end

function update()
    if dead then
        if btn.pressed("a") then
            reset_snake()
        elseif btn.pressed("b") then
            audio.beep(400, 80)
            exit()
        end
        return
    end

    if btn.pressed("b") then
        audio.beep(400, 80)
        exit()
        return
    end

    if btn.pressed("up")    and dir_y == 0 then next_dx, next_dy = 0, -1 end
    if btn.pressed("down")  and dir_y == 0 then next_dx, next_dy = 0, 1 end
    if btn.pressed("left")  and dir_x == 0 then next_dx, next_dy = -1, 0 end
    if btn.pressed("right") and dir_x == 0 then next_dx, next_dy = 1, 0 end

    move_tick = move_tick + 1
    if move_tick < speed then return end
    move_tick = 0

    dir_x, dir_y = next_dx, next_dy
    local head = snake[1]
    local new_head = { x = head.x + dir_x, y = head.y + dir_y }

    if new_head.x < 0 or new_head.x >= 30 or new_head.y < 0 or new_head.y >= 13 then
        dead = true
        audio.beep(160, 250)
        return
    end

    for i = 1, #snake do
        if snake[i].x == new_head.x and snake[i].y == new_head.y then
            dead = true
            audio.beep(160, 250)
            return
        end
    end

    table.insert(snake, 1, new_head)
    if new_head.x == food.x and new_head.y == food.y then
        score = score + 10
        audio.beep(750, 40)
        spawn_food()
    else
        table.remove(snake)
    end
end

function draw()
    oled.clear()
    oled.rect(3, 8, 122, 54)
    oled.text(4, 0, "PTS:" .. score)
    oled.text(92, 0, "B:Sair")
    local fx = 4 + food.x * 4
    local fy = 9 + food.y * 4
    oled.fillcircle(fx + 1, fy + 1, 1)
    for i = 1, #snake do
        local sx = 4 + snake[i].x * 4
        local sy = 9 + snake[i].y * 4
        if i == 1 then
            oled.fill(sx, sy, 3, 3)
        else
            oled.rect(sx, sy, 3, 3)
        end
    end
    if dead then
        oled.fill(16, 20, 96, 25, 0)
        oled.rect(16, 20, 96, 25, 1)
        oled.text(20, 23, "GAME OVER! PTS:" .. score)
        oled.text(20, 33, "A:Rejogar   B:Sair")
    end
end
