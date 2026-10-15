-- Run after installing tools/Decompile.luau. Compiles CameraModule output without executing it.
local Environment = getgenv()
local Bridge = assert(Environment.TazeBridge, "Install the Taze bridge first")
local Player = game:GetService("Players").LocalPlayer
local Scripts = Player and Player:FindFirstChild("PlayerScripts")
local Module = Scripts and Scripts:FindFirstChild("CameraModule", true)
    or game:GetService("StarterPlayer"):FindFirstChild("CameraModule", true)
assert(Module and Module:IsA("ModuleScript"), "CameraModule is unavailable")
local Report = { Running = true, Client = version(), Path = Module:GetFullName(), Cases = {} }
Environment.TazeRewriteValidation = Report
local Ok, Failure = pcall(function()
    local function Check(Options)
        local Started = os.clock()
        local Source = assert(Bridge.Decompile(Module, Options), Bridge.LastError)
        local Compiled, Error = loadstring(Source, "TazeCameraValidation")
        assert(Compiled, Error)
        table.insert(Report.Cases, { Bytes = #Source, Seconds = os.clock() - Started })
        return Source
    end
    local Default = Check({ header = false })
    Report.DefaultBytes = #Default
    Report.GcLookups = Bridge.LastRequest.GcLookups
    Report.RequireLookups = Bridge.LastRequest.ExportLookups
    Report.FilterLineField = Bridge.FilterLineField
    for _, Casing in { "pascal", "camel", "snake", "preserve" } do
        for _, Naming in { "inferred", "debug", "generic" } do
            Check({ casing = Casing, naming = Naming, ["preserve-function-names"] = false })
        end
    end
    local Clean = Check({ header = false, lines = false, upvalues = false })
    assert(not Clean:find("filtergc", 1, true) and not Clean:find("-- Line:", 1, true))
    Check({ ["optimization-passes"] = 0 })
    Check({ ["state-machine"] = true })
    local NoGc = Check({ ["gc-locators"] = false })
    assert(not NoGc:find(" | filtergc(", 1, true))
    local Results, Completed = {}, 0
    for Index = 1, 4 do
        task.spawn(function()
            Results[Index] = Bridge.Decompile(Module, { header = false })
            Completed += 1
        end)
    end
    local Deadline = os.clock() + 40
    while Completed < 4 and os.clock() < Deadline do task.wait() end
    assert(Completed == 4, "Concurrent requests did not complete")
    for Index = 1, 4 do assert(Results[Index] == Default, "Concurrent output differs") end
    Report.Concurrent = Completed
    assert(Bridge.Decompile(Module, { casing = "invalid" }) == nil)
    assert(Bridge.LastError and Bridge.LastError:find("Casing", 1, true))
    assert(Bridge.Decompile(Module, { header = false }) == Default, "Request failure leaked into subsequent settings")
    local Times = {}
    for Index = 1, 11 do
        local Start = os.clock()
        assert(Bridge.Decompile(Module, { header = false }) == Default)
        Times[Index] = os.clock() - Start
    end
    table.sort(Times)
    Report.MedianRoundTripMilliseconds = Times[6] * 1000
    Report.P95RoundTripMilliseconds = Times[11] * 1000
end)
Report.Running = false
Report.Passed = Ok
Report.Error = if Ok then nil else tostring(Failure)

return Report
