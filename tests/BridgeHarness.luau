local function Install(Environment)
    local getgenv = function() return Environment end
    local getscriptbytecode = function(Target) return Target.Bytecode end
    local compareinstances = function(A, B) return (A.Canonical or A) == (B.Canonical or B) end
    local hookfunction = Environment.Hook
    local newcclosure = function(Function) return Function end
    local filtergc = Environment.Filter
    local debug = { info = function(Function) Environment.Probe = Function return 123 end }
    local WebSocket = Environment.Provider
    local game = Environment.Game
    local print = function() end
    local warn = function(Message) Environment.Warning = Message end
    local typeof = function(Value) return if type(Value) == "table" and Value.IsA then "Instance" else type(Value) end
    local task = { spawn = function(Function) Function() end, wait = function() Environment.Time += 0.1 end }
    local os = { clock = function() return Environment.Time end }
    --!BRIDGE_SOURCE!
end

local function Instance(Name, Class, Parent)
    local Value = { Name = Name, Class = Class, Parent = Parent, Children = {} }
    function Value:IsA(Expected) return self.Class == Expected end
    function Value:FindFirstChild(Key) return self.Children[Key] end
    if Parent then Parent.Children[Name] = Value end
    return Value
end

local function Signal()
    local Value = { Listeners = {} }
    function Value:Connect(Callback)
        self.Listeners[Callback] = true
        return { Disconnect = function() self.Listeners[Callback] = nil end }
    end
    function Value:Fire(...)
        for Callback in self.Listeners do Callback(...) end
    end
    return Value
end

local Passed = 0
for _, Mode in { "Line", "StartLine", "Both", "Ignored", "Missing", "Hidden" } do
    local Environment = { Time = 0, Connects = 0, TazeConfig = {} }
    Environment.Game = Instance("Game", "DataModel")
    local Players = Instance("Players", "Players", Environment.Game)
    Players.LocalPlayer = Instance("TestPlayer", "Player", Players)
    function Environment.Game:GetService(Name) return self.Children[Name] end
    local Folder = Instance("PlayerScripts", "Folder", Players.LocalPlayer)
    local Target = Instance('Quoted"\n\255', "ModuleScript", Folder)
    Target.Bytecode = "\009\003\000\255"
    local Implementations = {}
    local Original
    Original = function(...) return Implementations[Original](...) end
    Implementations[Original] = function() return "original" end
    Environment.decompile = Original
    if Mode ~= "Missing" then
        Environment.Hook = function(Target, Replacement)
            assert(Mode ~= "Hidden", "mock executor rejected hook")
            assert(Implementations[Target], "mock only hooks registered closures")
            local Backup = Implementations[Target]
            Implementations[Target] = Replacement
            return Backup
        end
    end

    if Mode ~= "Missing" then
        Environment.Filter = function(Kind, Options, One)
            assert(Kind == "function" and One == false)
            assert(Options.IgnoreExecutor == false and Options.IgnoreSyn == false)
            if Mode == "Hidden" then return {} end
            local Line
            if Mode == "Both" then Line = Options.StartLine or Options.Line
            elseif Mode ~= "Ignored" then Line = Options[Mode] end
            return if Line == nil or Line == 123 then { Environment.Probe } else {}
        end
    end

    Environment.Provider = { connect = function()
        Environment.Connects += 1
        local Socket = { OnMessage = Signal(), OnClose = Signal() }
        function Socket:Close() self.OnClose:Fire() end
        function Socket:Send(Message)
            Environment.Message = Message
            local Id = Message:match("^(%d+)\n")
            if Environment.ServerError then
                self.OnMessage:Fire(Id .. "\nerror\nmock server failure")
            else
                self.OnMessage:Fire(Id .. "\nok\nreturn 1")
            end
        end
        Environment.Socket = Socket
        return Socket
    end }

    local Bridge = Install(Environment)
    local Expected = if Mode == "StartLine" or Mode == "Both" then "StartLine" else "Line"
    assert(Bridge.FilterLineField == Expected, "Wrong detected line field for " .. Mode)
    assert(Bridge.LineProbe == (if Mode == "Ignored" or Mode == "Missing" or Mode == "Hidden" then "fallback" else "verified"))
    assert(Environment.decompile(Target) == "return 1")
    assert(Bridge.Version == 5 and Bridge.Decompile == Environment.decompile)
    assert(Environment.decompile({}) == nil, "Invalid targets must exit silently")
    assert(Environment.decompile(Target, 0) == nil, "Invalid timeouts must exit silently")
    assert(Environment.decompile(Target, 0/0) == nil, "NaN timeout must be rejected")
    local SavedBytecode = Target.Bytecode
    Target.Bytecode = nil
    assert(Environment.decompile(Target) == nil, "Missing bytecode must exit silently")
    Target.Bytecode = SavedBytecode
    if Mode ~= "Missing" and Mode ~= "Hidden" then
        assert(Bridge.HookStatus == "installed" and Original(Target) == "return 1")
    else
        assert(Original(Target) == "original")
    end
    assert(Environment.decompile(Target) == "return 1")
    local Field, HexPath, Options, Bytecode = Environment.Message:match("TAZE3\n([^\n]+)\n([^\n]*)\n([^\n]*)\n(.+)$")
    assert(Options == "")
    assert(Field == Expected and Bytecode == "090300ff")
    local Path = HexPath:gsub("..", function(Pair) return string.char(tonumber(Pair, 16)) end)
    assert(Path == 'game:GetService("Players").LocalPlayer:FindFirstChild("PlayerScripts"):FindFirstChild("Quoted\\\"\\010\\255")', Path)
    assert(Bridge.LastRequest.ModulePath == Path and Bridge.LastRequest.PathStatus == "available")
    local Clone = setmetatable({ Canonical = Target }, { __index = Target })
    assert(Environment.decompile(Clone) == "return 1")
    assert(Bridge.LastRequest.ModulePath == Path, "A cloned instance reference lost its module path")
    local Detached = Instance("Detached", "ModuleScript")
    Detached.Bytecode = Target.Bytecode
    assert(Environment.decompile(Detached) == "return 1")
    assert(Bridge.LastRequest.ModulePath == "" and Bridge.LastRequest.PathStatus:find("not parented", 1, true))
    Target.Class = "LocalScript"
    assert(Environment.decompile(Target) == "return 1")
    assert(Environment.Message:find("\n" .. Expected .. "\n\n", 1, true))
    Target.Class = "ModuleScript"
    Environment.Socket:Close()
    assert(not Bridge.Connected)
    assert(Environment.decompile(Target) == "return 1" and Environment.Connects == 2)
    Environment.Socket.Send = function() error("mock send failure") end
    assert(Environment.decompile(Target) == nil, "Send failures must exit silently")
    Environment.ServerError = true
    assert(Environment.decompile(Target) == nil, "Server errors must exit silently")
    Environment.ServerError = false
    assert(Bridge.LastError == "mock server failure" and Bridge.LastRequest.Succeeded == false)
    Environment.TazeConfig.Options = { casing = "snake", ["gc-locators"] = false }
    assert(Environment.decompile(Target, { naming = "generic", ["require-locators"] = false }) == "return 1")
    assert(Bridge.LastError == nil and Bridge.LastRequest.Succeeded)
    assert(Bridge.LastRequest.ModulePath == "" and Bridge.LastRequest.PathStatus == "require lookups disabled")
    assert(Bridge.LastRequest.Options.casing == "snake" and Bridge.LastRequest.Options.naming == "generic")
    local HexOptions = Environment.Message:match("TAZE3\n[^\n]*\n[^\n]*\n([^\n]*)\n")
    local OptionBlock = HexOptions:gsub("..", function(Pair) return string.char(tonumber(Pair, 16)) end)
    assert(OptionBlock == "casing=snake;gc-locators=false;naming=generic;require-locators=false", OptionBlock)
    assert(Environment.TazeConfig.Options.naming == nil, "Per-call overrides mutated defaults")
    assert(Environment.decompile(Target, { casing = "snake;header=false" }) == nil)
    assert(Bridge.LastError:find("Invalid option value", 1, true))
    assert(Environment.decompile(Target, { ["module-path"] = "game" }) == nil)
    Environment.TazeConfig.ErrorMode = "raise"
    local Success, Failure = pcall(Environment.decompile, {})
    assert(not Success and tostring(Failure):find("Expected a script Instance", 1, true))
    Environment.TazeConfig.ErrorMode = nil
    Environment.TazeConfig.Options = nil
    local SavedWrapper
    SavedWrapper = function(...) return Implementations[SavedWrapper](...) end
    Implementations[SavedWrapper] = function() return "wrapper" end
    Environment.decompile = SavedWrapper
    local Replacement = Install(Environment)
    assert(Environment.TazeBridge == Replacement and Replacement.Original == Original)
    Replacement = Install(Environment)
    if Mode ~= "Missing" and Mode ~= "Hidden" then
        assert(SavedWrapper(Target) == "return 1", "Reinstallation lost a previously hooked cached wrapper")
    end
    Replacement.Stop()
    assert(Replacement.Decompile(Target) == nil, "Stopped bridges must exit silently")
    assert(Environment.decompile == Original and Environment.TazeBridge == nil)
    assert(Original(Target) == "original", "Stop did not restore the cached decompiler")
    assert(SavedWrapper(Target) == "wrapper", "Stop did not restore the cached wrapper")
    Environment.TazeConfig.Options = { ["gc-locators"] = false }
    Environment.TazeConfig.MaxPending = 1
    local Limited = Install(Environment)
    assert(Limited.LineProbe == "disabled", "Disabled GC lookups should skip probing")
    assert(Limited.Decompile(Target, { ["gc-locators"] = true }) == "return 1")
    assert(Limited.LineProbe ~= "disabled", "Enabling GC per call should probe lazily")
    local Send = Environment.Socket.Send
    local NestedError
    Environment.Socket.Send = function(self, Message)
        assert(Limited.Decompile(Target) == nil)
        NestedError = Limited.LastError
        Send(self, Message)
    end
    assert(Limited.Decompile(Target) == "return 1")
    assert(NestedError == "Taze pending request limit reached")
    Environment.Socket.Send = function() end
    assert(Limited.Decompile(Target, 0.5) == nil and Limited.LastError == "Taze request timed out")
    Environment.Socket.Send = Send
    assert(Limited.Decompile(Target) == "return 1", "Timeout leaked pending state")
    Limited.Stop()
    Passed += 1
end
return Passed
