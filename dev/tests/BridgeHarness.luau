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
    local print = function() error("Bridge must not print") end
    local warn = function() error("Bridge must not warn") end
    local typeof = function(Value) return if type(Value) == "table" and Value.IsA then "Instance" else type(Value) end
    local task = {
        spawn = function(Function)
            if Environment.DelaySpawn then
                table.insert(Environment.Scheduled, Function)
            else
                Function()
            end
        end,
        wait = function() Environment.Time += 0.1 end,
    }
    local os = { clock = function() return Environment.Time end }
    --!BRIDGE_SOURCE!
end

local function Instance(Name, Class, Parent)
    local Value = { Name = Name, Class = Class, Parent = Parent, Children = {} }
    function Value:IsA(Expected) return self.Class == Expected end
    function Value:GetChildren() local R={} for _, Child in self.Children do table.insert(R, Child) end return R end
    function Value:GetDescendants()
        local R={} for _, Child in self.Children do
            table.insert(R, Child)
            for _, Descendant in Child:GetDescendants() do table.insert(R, Descendant) end
        end return R
    end
    function Value:FindFirstChild(Key, Recursive)
        if self.Children[Key] then return self.Children[Key] end
        if Recursive then for _, Child in self.Children do local Found=Child:FindFirstChild(Key, true) if Found then return Found end end end
    end
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
    local Environment = { Time = 0, Connects = 0, AstraConfig = {} }
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
            assert(Options.IgnoreExecutor == false and Options.IgnoreSyn == nil)
            if Mode == "Hidden" then return {} end
            local Line
            if Mode == "Both" then Line = Options.StartLine or Options.Line
            elseif Mode ~= "Ignored" then Line = Options[Mode] end
            return if Line == nil or Line == 123 then { Environment.Probe } else {}
        end
    end

    Environment.Provider = { connect = function()
        Environment.Connects += 1
        if Environment.ConnectFailure then error("mock connect failure") end
        local Socket = { OnMessage = Signal(), OnClose = Signal(), IsClosed = false }
        function Socket:Close()
            if not self.IsClosed then
                self.IsClosed = true
                self.OnClose:Fire()
            end
        end
        function Socket:Send(Message)
            if self.IsClosed then return end
            Environment.Message = Message
            local Id = Message:match("^(%d+)\n")
            if Environment.ServerError then
                self.OnMessage:Fire(Id .. "\nerror\nmock server failure")
            else
                self.OnMessage:Fire(Id .. "\nok\nreturn 1")
            end
        end
        Environment.Socket = Socket
        if Environment.MissingEvents then Socket.OnMessage = nil end
        return Socket
    end }

    local Bridge = Install(Environment)
    local Expected = if Mode == "StartLine" or Mode == "Both" then "StartLine" else "Line"
    assert(Bridge.FilterLineField == Expected, "Wrong detected line field for " .. Mode)
    assert(Bridge.LineProbe == (if Mode == "Ignored" or Mode == "Missing" or Mode == "Hidden" then "fallback" else "verified"))
    assert(Environment.decompile(Target) == "return 1")
    assert(Bridge.Version == 8 and Bridge.Decompile == Environment.decompile)
    assert(Environment.AstraOptions.CompactArrays == true)
    assert(Environment.AstraOptions.IncludeUpvalueIndexes == true)
    assert(Environment.AstraOptions.CompactUpvalues == false)
    assert(Environment.AstraOptions.AllowFallback == false, "Silent state-machine fallback must be disabled by default")
    assert(Environment.AstraOptions.UseTabs == false)
    assert(Environment.AstraOptions.BlankLinesBetweenGroups == 1)
    assert(Environment.AstraOptions.SpaceInsideTableBraces == true)
    assert(Bridge.LastRequest.Options ~= Environment.AstraOptions, "Request options must be snapshots")
    local InternalDefaults = table.clone(Bridge.LastRequest.Options)
    local InitialConfig, InitialOptions = Environment.AstraConfig, Environment.AstraOptions
    Environment.AstraConfig, Environment.AstraOptions = nil, nil
    assert(Environment.decompile(Target) == "return 1", "Clearing global settings lost the internal defaults")
    for Key, Value in InternalDefaults do
        assert(Bridge.LastRequest.Options[Key] == Value, "Missing global setting lost its default: " .. Key)
    end
    assert(Environment.decompile(Target, { CompactArrays = false }) == "return 1")
    assert(Bridge.LastRequest.Options.CompactArrays == false and Bridge.LastRequest.Options.Casing == "pascal",
        "Per-call overrides did not layer over missing global settings")
    Environment.AstraConfig = false
    assert(Environment.decompile(Target) == nil, "Invalid config was silently treated as missing")
    Environment.AstraConfig, Environment.AstraOptions = InitialConfig, false
    assert(Environment.decompile(Target) == nil, "Invalid options were silently treated as missing")
    Environment.AstraOptions = InitialOptions
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
    assert(Environment.decompile(Target) == "return 1", Mode .. ": " .. tostring(Bridge.LastError))
    local Field, HexPath, Options, Bytecode = Environment.Message:match("ASTRA4\n([^\n]+)\n([^\n]*)\n([^\n]*)\n(.+)$")
    assert(Options:find('"Casing":"pascal"', 1, true))
    assert(Field == Expected and Bytecode == "090300ff")
    local Path = HexPath:gsub("..", function(Pair) return string.char(tonumber(Pair, 16)) end)
    assert(Path == 'game:GetService("Players").LocalPlayer:FindFirstChild("PlayerScripts"):FindFirstChild("Quoted\\\"\\010\\255")', Path)
    assert(Bridge.LastRequest.ModulePath == Path and Bridge.LastRequest.PathStatus == "available")
    local Clone = setmetatable({ Canonical = Target }, { __index = Target })
    assert(Environment.decompile(Clone) == "return 1")
    assert(Bridge.LastRequest.ModulePath == Path, "A cloned instance reference lost its module path")
    local Deep = Instance("Deep", "Folder", Folder)
    local Unique = Instance("Unique", "ModuleScript", Deep)
    Unique.Bytecode = Target.Bytecode
    assert(Environment.decompile(Unique) == "return 1")
    assert(Bridge.LastRequest.ModulePath == 'game:GetService("Players").LocalPlayer:FindFirstChild("Unique", true)')
    local Duplicate = Instance("Unique", "ModuleScript", Folder)
    Duplicate.Bytecode = Target.Bytecode
    assert(Environment.decompile(Unique) == "return 1")
    assert(not Bridge.LastRequest.ModulePath:find(', true)', 1, true), "Duplicate descendant shortened ambiguously")
    Folder.Deep = Deep
    Deep.Unique = Unique
    Players.LocalPlayer.PlayerScripts = Folder
    assert(Environment.decompile(Unique) == "return 1")
    assert(Bridge.LastRequest.ModulePath == 'game:GetService("Players").LocalPlayer.PlayerScripts.Deep.Unique')
    local Children = Deep.GetChildren
    Deep.GetChildren = function(self) local Result=Children(self) table.insert(Result, { Name="Unique" }) return Result end
    assert(Environment.decompile(Unique) == "return 1")
    assert(Bridge.LastRequest.ModulePath == "", "Duplicate siblings must suppress require locators")
    Deep.GetChildren = function() error("mock hierarchy access denied") end
    assert(Environment.decompile(Unique) == "return 1", "Unavailable module metadata prevented decompilation")
    assert(Bridge.LastRequest.ModulePath == "" and Bridge.LastRequest.PathStatus == "module path unavailable")
    assert(Bridge.LastRequest.Options.IncludeGcLocators == true, "Missing require metadata disabled GC locators")
    assert(Environment.decompile(Unique, { IncludeGcLocators = false }) == "return 1")
    assert(Bridge.LastRequest.ModulePath == "" and Bridge.LastRequest.Options.IncludeGcLocators == false,
        "Unavailable module metadata overrode disabled GC locators")
    Deep.GetChildren = Children
    local Detached = Instance("Detached", "ModuleScript")
    Detached.Bytecode = Target.Bytecode
    assert(Environment.decompile(Detached) == "return 1")
    assert(Bridge.LastRequest.ModulePath == "" and Bridge.LastRequest.PathStatus:find("not parented", 1, true))
    Target.Class = "LocalScript"
    assert(Environment.decompile(Target) == "return 1")
    assert(Environment.Message:find("\n" .. Expected .. "\n\n", 1, true))
    Target.Class, Target.RunContext = "Script", "Client"
    assert(Environment.decompile(Target) == "return 1", "A Script using client RunContext was rejected")
    assert(Bridge.LastRequest.ModulePath == "" and Bridge.LastRequest.PathStatus == "not a ModuleScript")
    Target.RunContext = nil
    Target.Class = "CoreScript"
    assert(Environment.decompile(Target) == "return 1", "Documented client-visible CoreScript bytecode was rejected")
    Target.Class = "ModuleScript"
    Environment.Socket:Close()
    assert(not Bridge.Connected)
    assert(Environment.decompile(Target) == "return 1" and Environment.Connects == 2)
    Environment.Socket.IsClosed = true
    assert(Environment.decompile(Target) == "return 1" and Environment.Connects == 3,
        "IsClosed must reconnect even when the close event was not delivered")
    Environment.Socket.Send = function() error("mock send failure") end
    assert(Environment.decompile(Target) == nil, "Send failures must exit silently")
    Environment.ServerError = true
    assert(Environment.decompile(Target) == nil, "Server errors must exit silently")
    Environment.ServerError = false
    assert(Bridge.LastError == "mock server failure" and Bridge.LastRequest.Succeeded == false)
    Environment.AstraConfig.Options = { Casing = "snake", ["IncludeGcLocators"] = false }
    assert(Environment.decompile(Target, { Naming = "generic", ["IncludeRequireLocators"] = false }) == "return 1")
    assert(Bridge.LastError == nil and Bridge.LastRequest.Succeeded)
    assert(Bridge.LastRequest.ModulePath == "" and Bridge.LastRequest.PathStatus == "require lookups disabled")
    assert(Bridge.LastRequest.Options.Casing == "snake" and Bridge.LastRequest.Options.Naming == "generic")
    local HexOptions = Environment.Message:match("ASTRA4\n[^\n]*\n[^\n]*\n([^\n]*)\n")
    local OptionBlock = HexOptions
    assert(OptionBlock:find('"Casing":"snake"', 1, true) and OptionBlock:find('"IncludeRequireLocators":false', 1, true), OptionBlock)
    assert(Environment.AstraConfig.Options.Naming == nil, "Per-call overrides mutated defaults")
    Environment.AstraOptions = { Casing = "camel", ["MethodSyntax"] = false }
    Environment.AstraConfig = { Options = { Naming = "debug" } }
    assert(Environment.decompile(Target) == "return 1")
    assert(Bridge.LastRequest.Options.Casing == "camel" and Bridge.LastRequest.Options.Naming == "debug")
    assert(Bridge.LastRequest.Options["MethodSyntax"] == false, "Replacing the global options table was ignored")
    assert(Bridge.LastRequest.Options.CompactArrays == true, "Partial option tables lost defaults")
    local SavedSend = Environment.Socket.Send
    Environment.Socket.Send = function(self, Message)
        Environment.AstraOptions.Casing = "preserve"
        SavedSend(self, Message)
    end
    assert(Environment.decompile(Target) == "return 1")
    assert(Bridge.LastRequest.Options.Casing == "camel", "A pending request's option snapshot changed")
    Environment.Socket.Send = SavedSend
    assert(Environment.decompile(Target) == "return 1" and Bridge.LastRequest.Options.Casing == "preserve")
    assert(Environment.decompile(Target, { CompactArrays = false, UseTabs = true, BlankLinesBetweenGroups = 2 }) == "return 1")
    assert(Bridge.LastRequest.Options.CompactArrays == false and Bridge.LastRequest.Options.UseTabs == true)
    assert(Bridge.LastRequest.Options.BlankLinesBetweenGroups == 2)
    assert(Environment.AstraOptions.CompactArrays == nil, "Per-call overrides leaked into the global table")
    assert(Environment.decompile(Target, { MaxInputBytes = 2 }) == nil)
    assert(Bridge.LastError:find("input byte limit", 1, true))
    assert(Environment.decompile(Target, { MaxOutputBytes = 2 }) == nil)
    assert(Bridge.LastError:find("output byte limit", 1, true))
    assert(Environment.decompile(Target, { IndentWidth = 0/0 }) == nil)
    assert(Environment.decompile(Target, { IndentWidth = math.huge }) == nil)
    assert(Environment.decompile(Target, { BlankLinesBetweenGroups = 0.5 }) == nil)
    assert(Environment.decompile(Target, { CompactArrays = "true" }) == nil)
    assert(Environment.decompile(Target, { IncludeHeader = {} }) == nil)
    assert(Environment.decompile(Target, { UnknownOption = true }) == nil)
    assert(Environment.decompile(Target, { ModulePath = "game" }) == nil)
    assert(Environment.decompile(Target, { FilterLineField = "Line" }) == nil)
    assert(Environment.decompile(Target, { Casing = "snake;header=false" }) == nil)
    assert(Bridge.LastError:find("Invalid option value", 1, true))
    assert(Environment.decompile(Target, { ["module-path"] = "game" }) == nil)
    Environment.AstraConfig.ErrorMode = "raise"
    local Success, Failure = pcall(Environment.decompile, {})
    assert(not Success and tostring(Failure):find("Expected a script Instance", 1, true))
    Environment.AstraConfig.ErrorMode = nil
    Environment.AstraConfig.Options = nil
    local SavedWrapper
    SavedWrapper = function(...) return Implementations[SavedWrapper](...) end
    Implementations[SavedWrapper] = function() return "wrapper" end
    Environment.decompile = SavedWrapper
    local Replacement = Install(Environment)
    assert(Environment.AstraBridge == Replacement and Replacement.Original == Original)
    Replacement = Install(Environment)
    if Mode ~= "Missing" and Mode ~= "Hidden" then
        assert(SavedWrapper(Target) == "return 1", "Reinstallation lost a previously hooked cached wrapper")
    end
    Replacement.Stop()
    assert(Replacement.Decompile(Target) == nil, "Stopped bridges must exit silently")
    assert(Environment.decompile == Original and Environment.AstraBridge == nil)
    assert(Original(Target) == "original", "Stop did not restore the cached decompiler")
    assert(SavedWrapper(Target) == "wrapper", "Stop did not restore the cached wrapper")
    Environment.AstraConfig.Options = { ["IncludeGcLocators"] = false }
    Environment.AstraConfig.MaxPending = 1
    local Limited = Install(Environment)
    assert(Limited.LineProbe == "disabled", "Disabled GC lookups should skip probing")
    assert(Limited.Decompile(Target, { ["IncludeGcLocators"] = true }) == "return 1")
    assert(Limited.LineProbe ~= "disabled", "Enabling GC per call should probe lazily")
    local Send = Environment.Socket.Send
    local NestedError
    Environment.Socket.Send = function(self, Message)
        assert(Limited.Decompile(Target) == nil)
        NestedError = Limited.LastError
        Send(self, Message)
    end
    assert(Limited.Decompile(Target) == "return 1")
    assert(NestedError == "Astra pending request limit reached")
    Environment.Socket.Send = function() end
    assert(Limited.Decompile(Target, 0.5) == nil and Limited.LastError == "Astra request timed out")
    Environment.Socket.Send = Send
    assert(Limited.Decompile(Target) == "return 1", "Timeout leaked pending state")
    Environment.Socket.Send = function(self, Message)
        local Id = Message:match("^(%d+)\n")
        self.OnMessage:Fire(Id .. "\nok\nignored binary response", true)
        self.OnMessage:Fire("not a response")
        self.OnMessage:Fire("999999\nok\nstale response")
        self.OnMessage:Fire(Id .. "\nok\nreturn 1", false)
        self.OnMessage:Fire(Id .. "\nerror\nduplicate response")
    end
    assert(Limited.Decompile(Target) == "return 1", "Malformed, binary, stale, or duplicate responses changed the result")
    Environment.Socket.Send = Send
    Environment.Socket.Send = function(self) self.IsClosed = true end
    local CloseTime = Environment.Time
    assert(Limited.Decompile(Target) == nil and Environment.Time == CloseTime,
        "A closed socket silently swallowed Send and waited for the timeout")
    assert(Limited.Decompile(Target) == "return 1", "Closed socket did not reconnect")
    Limited.Stop()
    Limited.Stop()
    Environment.AstraConfig.Timeout = 0
    assert(Install(Environment) == nil and Environment.decompile == Original,
        "Invalid transport settings replaced the existing decompiler")
    Environment.AstraConfig.Timeout = 1
    Environment.ConnectFailure = true
    assert(Install(Environment) == nil and Environment.decompile == Original,
        "Failed connection replaced the existing decompiler")
    Environment.ConnectFailure = nil
    Environment.MissingEvents = true
    assert(Install(Environment) == nil and Environment.decompile == Original)
    assert(Environment.Socket.IsClosed == true, "Unsupported event API leaked a connection")
    Environment.MissingEvents = nil
    Environment.DelaySpawn = true
    Environment.Scheduled = {}
    assert(Install(Environment) == nil and Environment.decompile == Original,
        "Connection timeout replaced the existing decompiler")
    assert(#Environment.Scheduled == 1)
    Environment.Scheduled[1]()
    assert(Environment.Socket.IsClosed == true, "Timed-out handshake leaked a late connection")
    Environment.DelaySpawn = nil
    Passed += 1
end
return Passed
