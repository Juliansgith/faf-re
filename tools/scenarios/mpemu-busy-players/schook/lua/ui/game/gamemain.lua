-- Lockstep tests where the human players give orders: see schook/lua/SimCallbacks.lua in this
-- folder. Every player's game sends, every few seconds, an order for reinforcements and an attack
-- order, both picked with the UI's own random numbers (seeded from the clock, so no two games
-- pick alike). Nothing is sent in a replay or by an observer.
do
    local blueprints = { 'uel0201', 'uel0103', 'url0107', 'ual0201', 'uea0103', 'uel0202', 'xsl0201' }

    local function BusyThread()
        math.randomseed(math.floor(GetSystemTimeSeconds() * 1000))
        while GameTick() < 300 do
            WaitSeconds(1)
        end
        local armies = GetArmiesTable()
        local count = table.getn(armies.armiesTable)
        local sent = 0
        while not SessionIsGameOver() do
            local me = GetFocusArmy()
            if me >= 1 and not SessionIsReplay() then
                SimCallback({ Func = 'MpemuBusySpawn', Args = {
                    bp = blueprints[math.random(table.getn(blueprints))],
                    count = math.random(3, 10),
                    dx = math.random(-14, 14),
                    dz = math.random(-14, 14),
                } })
                local target = math.random(count)
                if target ~= me and not armies.armiesTable[target].civilian then
                    SimCallback({ Func = 'MpemuBusyAttack', Args = {
                        target = target,
                        dx = math.random(-20, 20),
                        dz = math.random(-20, 20),
                        clear = math.random(2) == 1,
                    } })
                end
                sent = sent + 1
                if math.mod(sent, 10) == 0 then
                    LOG('mpemu-busy-players: ' .. sent .. ' rounds of orders sent as army ' .. me)
                end
            end
            WaitSeconds(3 + math.random() * 4)
        end
    end

    local baseOnFirstUpdate = OnFirstUpdate
    function OnFirstUpdate()
        baseOnFirstUpdate()
        ForkThread(BusyThread)
    end
end
