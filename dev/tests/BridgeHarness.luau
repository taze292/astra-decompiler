local function Install(Environment)
    local getgenv = function() return Environment end
    local getscriptbytecode = function(Target)
        Environment.Time += Environment.BytecodeDelay or 0
        return Target.Bytecode
    end
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
            if Environment.EventMode then
                local Thread = if type(Function) == "thread" then Function else coroutine.create(Function)
                local Ok, Failure = coroutine.resume(Thread)
                assert(Ok, Failure)
            elseif Environment.DelaySpawn then
                table.insert(Environment.Scheduled, Function)
            else
                Function()
            end
        end,
        wait = function()
            if Environment.YieldWait then coroutine.yield() else Environment.Time += 0.1 end
        end,
        delay = if Environment.EventMode then Environment.Schedule else nil,
        cancel = if Environment.EventMode then function(Timer) Timer.Cancelled = true end else nil,
        defer = if Environment.EventMode then function(Function) table.insert(Environment.Deferred, Function) end else nil,
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
    local Environment = { Time = 0, Connects = 0, AstraConfig = { CacheBytes = 0, BinaryTransport = false } }
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
    assert(Bridge.Version == 9 and Bridge.Decompile == Environment.decompile)
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
    local SavedDescendants = Players.LocalPlayer.GetDescendants
    Players.LocalPlayer.GetDescendants = function() error("Module paths must not scan the full game hierarchy") end
    local Wide = Instance("Wide", "Folder", Players.LocalPlayer)
    local Visits = 0
    for Index = 1, 600 do
        local Child = Instance("Extra" .. Index, "Folder", Wide)
        local GetChildren = Child.GetChildren
        Child.GetChildren = function(self) Visits += 1 return GetChildren(self) end
    end
    assert(Environment.decompile(Unique) == "return 1")
    assert(Bridge.LastRequest.ModulePath == 'game:GetService("Players").LocalPlayer.PlayerScripts.Deep.Unique',
        "Exhausted shortening budget lost the validated explicit path")
    assert(Visits <= 512, "Module-path shortening exceeded its shared traversal budget")
    Players.LocalPlayer.Children.Wide = nil
    Players.LocalPlayer.GetDescendants = SavedDescendants
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
    Environment.AstraConfig = { Options = { Naming = "debug" }, CacheBytes = 0, BinaryTransport = false }
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
        assert(Limited.Decompile(Target, 0.2) == nil)
        NestedError = Limited.LastError
        Send(self, Message)
    end
    assert(Limited.Decompile(Target) == "return 1")
    assert(NestedError == "Astra pending request queue timed out")
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
    assert(Limited.Decompile(Target.Bytecode) == "return 1", "Raw bytecode input was rejected")
    assert(Limited.LastRequest.ModulePath == "" and Limited.LastRequest.PathStatus == "raw bytecode has no module path")
    assert(Limited.Decompile(Target, {
        SmartVariableRenamer = false,
        GuardClauses = true,
        Formatter = { IndentWidth = 2, FunctionMetadataEnabled = false, FunctionMetadataIncludeUpvalues = false },
    }) == "return 1", "Volt-style decompiler options were rejected")
    assert(Limited.LastRequest.Options.Naming == "generic" and Limited.LastRequest.Options.IndentWidth == 2)
    assert(Limited.LastRequest.Options.IncludeLineComments == false and Limited.LastRequest.Options.IncludeUpvalueComments == false)
    assert(table.find(Limited.LastRequest.IgnoredNativeOptions, "GuardClauses"),
        "Unsupported native options were discarded without a diagnostic")
    if type(newproxy) == "function" then
        local Native = newproxy(true)
        getmetatable(Native).__index = { SmartVariableRenamer = false, Formatter = { IndentWidth = 3 } }
        assert(Limited.Decompile(Target, Native) == "return 1", "Native DecompilerOptions userdata was rejected")
        assert(Limited.LastRequest.Options.Naming == "generic" and Limited.LastRequest.Options.IndentWidth == 3)
        assert(Limited.Decompile(Target, newproxy()) == nil and Limited.LastError == "Unsupported native decompiler options",
            "Unrelated userdata was silently treated as default options")
    end
    Limited.Stop()
    Limited.Stop()
    Environment.AstraConfig = { MaxPending = 2, Concurrency = 2, CacheBytes = 1024, ErrorMode = "raise" }
    local Cached = Install(Environment)
    local Sends = 0
    local CachedSend = Environment.Socket.Send
    Environment.Socket.Send = function(self, Message, IsBinary)
        Sends += 1
        assert(IsBinary == true and Message:find("\nASTRA5\n", 1, true), "Binary transport did not use ASTRA5")
        assert(Message:sub(-#Target.Bytecode) == Target.Bytecode, "Binary transport altered raw bytecode")
        CachedSend(self, Message)
    end
    assert(Cached.Decompile(Target) == "return 1" and not Cached.LastRequest.CacheHit)
    assert(Cached.Decompile(Target) == "return 1" and Cached.LastRequest.CacheHit and Sends == 1,
        "Repeated script requests did not reuse successful source")
    assert(Cached.Stats.CacheHits == 1 and Cached.Stats.Succeeded == 2)
    assert(Cached.Decompile(Target, { Casing = "snake" }) == "return 1" and Sends == 2,
        "Changed options reused stale cached output")
    Cached.ClearCache()
    assert(Cached.Decompile(Target) == "return 1" and Sends == 3)
    Environment.AstraConfig.ErrorMode = nil
    Environment.BytecodeDelay = 0.2
    assert(Cached.Decompile(Target, 0.1) == nil and Cached.LastError == "Timed out while preparing bytecode",
        "A cache hit ignored the request preparation deadline")
    Environment.BytecodeDelay = nil
    Environment.AstraConfig.ErrorMode = "raise"
    local Batch = Cached.DecompileMany({ Target, {}, Target })
    assert(#Batch == 3 and Batch[1].Source == "return 1" and Batch[3].Source == "return 1")
    assert(Batch[2].Source == nil and Batch[2].Error:find("Expected a script Instance", 1, true),
        "Batch failure lost its per-script diagnostic")
    local SaveRoot = Instance("SaveRoot", "Folder", Environment.Game)
    local SaveScript = Instance("SaveScript", "LocalScript", SaveRoot)
    SaveScript.Bytecode = Target.Bytecode
    Instance("ServerOnly", "Script", SaveRoot)
    local ClientScript = Instance("ClientScript", "Script", SaveRoot)
    ClientScript.RunContext, ClientScript.Bytecode = "Client", Target.Bytecode
    local Prepared, Targets = Cached.PrepareSave({ SaveRoot, SaveScript })
    assert(#Prepared == 2 and #Targets == 2 and Prepared[1].Source == "return 1" and Prepared[2].Source == "return 1"
        and table.find(Targets, SaveScript) and table.find(Targets, ClientScript),
        "Save preparation missed a script or duplicated overlapping roots")
    assert(Cached.Stats.Failed == 2 and Cached.LastError == nil)
    Cached.Stop()
    Environment.EventMode = true
    Environment.Timers, Environment.Deferred = {}, {}
    Environment.Schedule = function(Delay, Callback)
        local Timer = { Due = Environment.Time + Delay, Callback = Callback }
        table.insert(Environment.Timers, Timer)
        return Timer
    end
    local function Pump(Elapsed)
        Environment.Time += Elapsed
        local Ready = {}
        for Index = #Environment.Timers, 1, -1 do
            local Timer = Environment.Timers[Index]
            if Timer.Due <= Environment.Time then
                table.remove(Environment.Timers, Index)
                if not Timer.Cancelled then table.insert(Ready, Timer.Callback) end
            end
        end
        for _, Callback in Ready do Callback() end
        local Deferred = Environment.Deferred
        Environment.Deferred = {}
        for _, Thread in Deferred do
            local Ok, Failure = coroutine.resume(Thread)
            assert(Ok, Failure)
        end
    end
    Environment.AstraConfig = { MaxPending = 2, CacheBytes = 0 }
    local EventBridge = Install(Environment)
    local SentIds = {}
    Environment.Socket.Send = function(self, Message)
        local Id = Message:match("^(%d+)\n")
        table.insert(SentIds, Id)
        local Ordinal = #SentIds
        Environment.Schedule(if Ordinal == 1 then 0.02 else 0.01, function()
            self.OnMessage:Fire(Id .. "\nok\nreturn " .. Ordinal)
        end)
    end
    local First, Second
    local FirstThread = coroutine.create(function() First = EventBridge.Decompile(Target) end)
    local SecondThread = coroutine.create(function() Second = EventBridge.Decompile(Target) end)
    assert(coroutine.resume(FirstThread) and coroutine.resume(SecondThread))
    assert(coroutine.status(FirstThread) == "suspended" and coroutine.status(SecondThread) == "suspended")
    Pump(0.01)
    assert(Second == "return 2" and First == nil, "A fast request waited behind an earlier slow request")
    Pump(0.02)
    assert(First == "return 1", "Out-of-order completion resumed the wrong coroutine")
    Environment.Socket.Send = function() end
    local TimedOut, TimeoutError
    local TimeoutThread = coroutine.create(function() TimedOut, TimeoutError = EventBridge.Decompile(Target, 0.5) end)
    assert(coroutine.resume(TimeoutThread))
    Pump(0.6)
    assert(TimedOut == nil and TimeoutError == "Astra request timed out" and coroutine.status(TimeoutThread) == "dead",
        "Event-based wait did not resume on timeout")
    local StopError
    local StopThread = coroutine.create(function() local _, Failure = EventBridge.Decompile(Target) StopError = Failure end)
    assert(coroutine.resume(StopThread))
    EventBridge.Stop()
    Pump(0)
    assert(StopError == "Astra bridge stopped" and coroutine.status(StopThread) == "dead",
        "Stopping the bridge stranded an event waiter")
    local QueueBridge = Install(Environment)
    local QueueSent, QueueCompleted = 0, 0
    Environment.Socket.Send = function(self, Message)
        QueueSent += 1
        local Id = Message:match("^(%d+)\n")
        Environment.Schedule(0.01, function() self.OnMessage:Fire(Id .. "\nok\nqueued source") end)
    end
    for _ = 1, 8 do
        local Thread = coroutine.create(function()
            assert(QueueBridge.Decompile(Target) == "queued source")
            QueueCompleted += 1
        end)
        assert(coroutine.resume(Thread))
    end
    assert(QueueSent == 2 and QueueCompleted == 0, "Backpressure exceeded MaxPending active requests")
    for _ = 1, 12 do Pump(0.02) end
    assert(QueueSent == 8 and QueueCompleted == 8 and QueueBridge.Stats.Failed == 0,
        "Requests above MaxPending were rejected or stranded")
    Environment.Socket.Send = function() end
    local QueuedError, Blocked = nil, 0
    for _ = 1, 2 do
        local Thread = coroutine.create(function()
            local _, Failure = QueueBridge.Decompile(Target, 1)
            assert(Failure == "Astra bridge stopped")
            Blocked += 1
        end)
        assert(coroutine.resume(Thread))
    end
    local QueuedThread = coroutine.create(function()
        local _, Failure = QueueBridge.Decompile(Target, 0.05)
        QueuedError = Failure
    end)
    assert(coroutine.resume(QueuedThread))
    Pump(0.06)
    assert(QueuedError == "Astra pending request queue timed out" and coroutine.status(QueuedThread) == "dead",
        "Admission did not honor the queued request's own timeout")
    QueueBridge.Stop()
    Pump(0)
    assert(Blocked == 2, "Stop stranded admitted requests after a queued timeout")
    Environment.AstraConfig = { MaxPending = 1, MaxQueued = 1, CacheBytes = 0 }
    local Capped = Install(Environment)
    Environment.Socket.Send = function() end
    local BlockedThread = coroutine.create(function() Capped.Decompile(Target, 10) end)
    assert(coroutine.resume(BlockedThread))
    local Expired = 0
    local function QueueShortRequest()
        local Thread = coroutine.create(function()
            local _, Failure = Capped.Decompile(Target, 0.05)
            assert(Failure == "Astra pending request queue timed out")
            Expired += 1
        end)
        assert(coroutine.resume(Thread))
    end
    QueueShortRequest()
    assert(Capped.Decompile(Target, 0.1) == nil and Capped.LastError == "Astra queued request limit reached",
        "The caller queue exceeded its configured bound")
    Pump(0.06)
    QueueShortRequest()
    Pump(0.06)
    assert(Expired == 2, "Expired waiters retained the bounded queue slot")
    Capped.Stop()
    Pump(0)
    Environment.AstraConfig = { MaxPending = 2, Concurrency = 2, CacheBytes = 0 }
    local StoppedBatch = Install(Environment)
    Environment.Socket.Send = function() end
    Environment.YieldWait = true
    local StoppedResults
    local BatchThread = coroutine.create(function()
        StoppedResults = StoppedBatch.DecompileMany({ Target, Target, Target, Target, Target })
    end)
    assert(coroutine.resume(BatchThread))
    StoppedBatch.Stop()
    Pump(0)
    assert(coroutine.resume(BatchThread))
    assert(#StoppedResults == 5, "Stopping a batch lost entries that had not started")
    for _, Result in StoppedResults do assert(Result.Error == "Astra bridge stopped" and Result.Source == nil) end
    Environment.YieldWait = nil
    Environment.EventMode = false
    Environment.AstraConfig.BinaryTransport = "invalid"
    assert(Install(Environment) == nil and Environment.decompile == Original, "Non-boolean BinaryTransport was accepted")
    Environment.AstraConfig.BinaryTransport = false
    Environment.AstraConfig.MaxQueued = 65537
    assert(Install(Environment) == nil and Environment.decompile == Original, "Unbounded MaxQueued was accepted")
    Environment.AstraConfig.MaxQueued = 0
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
