-- Silent live validation. Compiles recovered code; never executes game modules.
-- Inspect getgenv().AstraReadabilityValidation after execution.
local Environment = getgenv()
local Bridge = assert(Environment.AstraBridge, "Install the local bridge first")
local Report = { Running = true, Passed = false, Cases = {} }
Environment.AstraReadabilityValidation = Report
local SavedOptions, SavedConfig = Environment.AstraOptions, Environment.AstraConfig
local Ok, Failure = pcall(function()
    Environment.AstraOptions = { IncludeHeader = false }
    Environment.AstraConfig = {}
    local Player = game:GetService("Players").LocalPlayer
    local Targets, Seen = {}, {}
    local function Add(Target)
        if Target and not Seen[Target] then Seen[Target] = true table.insert(Targets, Target) end
    end
    local Modules = game:GetService("ReplicatedStorage"):FindFirstChild("ModuleScripts")
    local Guns = Modules and Modules:FindFirstChild("GunModules")
    Add(Guns and Guns:FindFirstChild("Projectile"))
    local Framework = Guns and Guns:FindFirstChild("GunFramework")
    Add(Framework and Framework:FindFirstChild("Projectile"))
    local Scripts = Player and Player:FindFirstChild("PlayerScripts")
        or game:GetService("StarterPlayer"):FindFirstChild("StarterPlayerScripts")
    for _, Name in { "CameraModule", "ControlModule", "Data" } do Add(Scripts and Scripts:FindFirstChild(Name, true)) end
    local LocalCount = 0
    for _, Target in if Scripts then Scripts:GetDescendants() else {} do
        if Target:IsA("LocalScript") then
            Add(Target)
            LocalCount += 1
            if LocalCount == 3 then break end
        end
    end
    assert(#Targets > 0, "No readable test scripts available")
    local function Check(Target, Options)
        local Source = assert(Bridge.Decompile(Target, Options), Bridge.LastError)
        local Compiled, Error = loadstring(Source, "AstraReadabilityValidation")
        assert(Compiled, Target:GetFullName() .. ": " .. tostring(Error))
        return Source
    end
    for _, Target in Targets do
        local Source = Check(Target, {})
        local _, Boxes = Source:gsub("= { [^\n]*%[1%] }", "")
        local Case = { Path = Target:GetFullName(), Class = Target.ClassName, Bytes = #Source, ClosingBoxes = Boxes,
            ModulePath = Bridge.LastRequest.ModulePath, Variants = 1 }
        table.insert(Report.Cases, Case)
        for _, Options in {
            { NativeCaptures = false },
            { MethodSyntax = false },
            {
                Casing = "snake",
                Naming = "generic",
                LocalPrefix = "item",
                ArgumentPrefix = "arg",
            },
            {
                Casing = "camel",
                CompactUpvalues = false,
                GroupSpacing = false,
            },
            {
                IncludeLineComments = false,
                IncludeUpvalueComments = false,
            },
            { IncludeGcLocators = false },
            {
                IncludeRequireLocators = false,
                IncludeGcLocators = true,
            },
            {
                CompactArrays = false,
                UseTabs = true,
                BlankLinesBetweenGroups = 0,
                SpaceInsideTableBraces = false,
            },
            {
                CompactArrays = true,
                UseTabs = false,
                IndentWidth = 2,
                BlankLinesBetweenGroups = 2,
                SpaceInsideTableBraces = true,
            },
        } do
            local Variant = Check(Target, Options)
            for Key, Value in Options do
                assert(Bridge.LastRequest.Options[Key] == Value, "Option was not sent: " .. Key)
            end
            if Options.IncludeRequireLocators == false then
                assert(not Variant:find(" | require(", 1, true))
                Case.GcOnly = Bridge.LastRequest.GcLookups
            end
            Case.Variants += 1
        end
        assert(Check(Target, {}) == Source, "Options leaked between calls")
        local Timings, Trips = {}, {}
        for Index = 1, 11 do
            local Start = os.clock()
            local Timed = Check(Target, { IncludeHeader = true })
            Trips[Index] = (os.clock() - Start) * 1000
            assert(Timed:match("^%-%- Decompiled with Astra | Bytecode Version: %d+\n%-%- Time taken: [%d%.]+s\n"))
            Timings[Index] = assert(tonumber(Timed:match("Time taken: ([%d%.]+)s"))) * 1000
        end
        table.sort(Timings) table.sort(Trips)
        Case.EngineMedianMs, Case.EngineP95Ms = Timings[6], Timings[11]
        Case.RoundTripMedianMs = Trips[6]
    end
    Report.Passed = true
end)
Environment.AstraOptions, Environment.AstraConfig = SavedOptions, SavedConfig
Report.Running = false
Report.Error = if Ok then nil else tostring(Failure)
return Report
