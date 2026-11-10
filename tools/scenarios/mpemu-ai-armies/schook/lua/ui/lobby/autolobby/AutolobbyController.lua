-- Lockstep tests with AI armies (tools/linux-build/mixed-lockstep.py, docs/LINUX_CLIENT.md): an
-- init file that mounts this folder at / makes this hook run after FAF's AutolobbyController.lua.
-- Every client adds the same AI armies to the free start spots of the launch configuration before
-- it launches, so the sim has armies that build and fight while the human players stay idle. The
-- host broadcasts the configuration before its own LaunchGame runs, so each client adds them once,
-- from the same configuration.
do
    local personalities = { 'rush', 'adaptive', 'tech', 'turtle' }
    local wanted = 4 -- AI armies, at most; fewer when the map has fewer free start spots

    local baseLaunchGame = AutolobbyCommunications.LaunchGame
    AutolobbyCommunications.LaunchGame = function(self, gameConfig)
        local scenario = gameConfig.GameOptions and gameConfig.GameOptions.ScenarioFile
        local spots = 0
        if scenario then
            local info = import('/lua/ui/maputil.lua').LoadScenario(scenario)
            if info and info.Configurations and info.Configurations.standard then
                spots = table.getn(info.Configurations.standard.teams[1].armies)
            end
        end
        local added = 0
        for spot = 1, spots do
            if added >= wanted then
                break
            end
            if not gameConfig.PlayerOptions[spot] then
                added = added + 1
                local personality = personalities[math.mod(added - 1, table.getn(personalities)) + 1]
                gameConfig.PlayerOptions[spot] = {
                    OwnerID = self.HostID,
                    PlayerName = 'AI ' .. added .. ' ' .. personality,
                    Ready = true,
                    Human = false,
                    Civilian = false,
                    AIPersonality = personality,
                    Faction = math.mod(added - 1, 4) + 1,
                    Team = 1,
                    StartSpot = spot,
                    PlayerColor = spot,
                    ArmyColor = spot,
                    PlayerClan = '',
                    MEAN = 0, DEV = 0, PL = 0, NG = 0,
                }
                LOG('mpemu-ai-armies: AI army ' .. personality .. ' at start spot ' .. spot)
            end
        end
        LOG('mpemu-ai-armies: launching with ' .. added .. ' AI armies on ' .. tostring(scenario))
        return baseLaunchGame(self, gameConfig)
    end
end
