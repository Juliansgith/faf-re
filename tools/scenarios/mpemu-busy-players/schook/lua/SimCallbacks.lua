-- Lockstep tests where the human players give orders (tools/linux-build/mixed-lockstep.py,
-- docs/LINUX_CLIENT.md): an init file that mounts this folder at / adds two sim callbacks, which
-- the UI hook next to this file (schook/lua/ui/game/gamemain.lua) sends from every player's game.
-- What each order says (which units, how many, where to, whom to attack) is picked at random in
-- the sending player's UI, so the other games only have it from the network.
do
    local baseDoCallback = DoCallback
    local GetCurrentCommandSourceArmy = import("/lua/simutils.lua").GetCurrentCommandSourceArmy

    -- Reinforcements for the sender's army, around data.pos (offsets from its start position).
    local function Spawn(data)
        local army = GetCurrentCommandSourceArmy()
        if not army or army < 1 then
            return
        end
        local brain = ArmyBrains[army]
        if brain:GetCurrentUnits(categories.MOBILE) > 160 then
            return
        end
        local sx, sz = brain:GetArmyStartPos()
        for i = 1, math.min(data.count or 1, 12) do
            local x = sx + (data.dx or 0) + math.mod(i, 4) * 2
            local z = sz + (data.dz or 0) + math.floor(i / 4) * 2
            CreateUnitHPR(data.bp, army, x, GetTerrainHeight(x, z), z, 0, 0, 0)
        end
    end

    -- The sender's mobile units, without its commander, attack-move to data.target's start
    -- position, offset by data.dx / data.dz.
    local function Attack(data)
        local army = GetCurrentCommandSourceArmy()
        local target = ArmyBrains[data.target]
        if not army or army < 1 or not target then
            return
        end
        local units = ArmyBrains[army]:GetListOfUnits(categories.MOBILE - categories.COMMAND, false)
        if table.getn(units) == 0 then
            return
        end
        local tx, tz = target:GetArmyStartPos()
        local x, z = tx + (data.dx or 0), tz + (data.dz or 0)
        if data.clear then
            IssueClearCommands(units)
        end
        IssueAggressiveMove(units, { x, GetTerrainHeight(x, z), z })
    end

    function DoCallback(name, data, units)
        if name == 'MpemuBusySpawn' then
            Spawn(data)
        elseif name == 'MpemuBusyAttack' then
            Attack(data)
        else
            return baseDoCallback(name, data, units)
        end
    end
end
