-- Start build/Release/astra.exe --serve before running this script.

-- References
local Environment = getgenv()
local GetBytecode = getscriptbytecode or Environment.getscriptbytecode
local CompareInstances = compareinstances or Environment.compareinstances
local HookFunction = hookfunction or Environment.hookfunction
local WrapFunction = newcclosure or Environment.newcclosure
local Provider = WebSocket or websocket
local Connect = Provider and Provider.connect

-- Settings
-- Edit these defaults here, or set getgenv().AstraConfig before running the script.
Environment.AstraConfig = Environment.AstraConfig or {
    Url = "ws://127.0.0.1:8877/decompile",
    Timeout = 30,
    MaxPending = 32,
    MaxQueued = 16384, -- Calls waiting for an active request slot; 0 rejects overflow.
    Concurrency = 8, -- Workers used by AstraBridge.DecompileMany/PrepareSave.
    BinaryTransport = true, -- ASTRA5 avoids Lua hex encoding and halves the payload.
    CacheBytes = 32 * 1024 * 1024, -- Bounded successful-source cache; 0 disables it.
    ErrorMode = "silent", -- "silent" returns nil; "raise" raises LastError.
}

local DefaultOptions = {
    -- Output and comments
    Encoding = "auto", -- "auto", "plain", "roblox"
    IncludeHeader = true,
    IncludeLineComments = true,
    IncludeUpvalueComments = true,
    IncludeUpvalueIndexes = true,
    CompactUpvalues = false,
    IncludeFunctionLocators = true,
    IncludeRequireLocators = true,
    IncludeGcLocators = true,

    -- Naming and reconstruction
    Casing = "pascal", -- "pascal", "camel", "snake", "preserve"
    Naming = "inferred", -- "inferred", "debug", "generic"
    LocalPrefix = "Value",
    ArgumentPrefix = "Argument",
    UseTypeNames = true,
    PreserveFunctionNames = true,
    InferMemberNames = true,
    InferCallNames = true,
    NativeCaptures = true,
    MethodSyntax = true,
    CompactArrays = true,

    -- Spacing
    IndentWidth = 4,
    UseTabs = false,
    GroupSpacing = true,
    BlankLinesBetweenGroups = 1,
    SpaceInsideTableBraces = true,

    -- Processing limits
    OptimizationPasses = 5,
    ForceStateMachine = false,
    AllowFallback = false,
    MaxNesting = 128,
    MaxAnalysisCells = 16000000,
    MaxAnalysisWork = 32000000,
    MaxInputBytes = 64 * 1024 * 1024,
    MaxOutputBytes = 64 * 1024 * 1024,
}

-- Editing or replacing this global table affects the next request.
-- AstraConfig.Options and per-call overrides take precedence in that order.
Environment.AstraOptions = Environment.AstraOptions or table.clone(DefaultOptions)

local Config = Environment.AstraConfig
if type(Config) ~= "table" or type(Environment.AstraOptions) ~= "table" then
    return nil
end

local Url = Config.Url or "ws://127.0.0.1:8877/decompile"
local DefaultTimeout = Config.Timeout or 30
local MaxPending = Config.MaxPending or 32
local MaxQueued = if Config.MaxQueued == nil then 16384 else Config.MaxQueued
local Concurrency = Config.Concurrency or math.min(8, if type(MaxPending) == "number" then MaxPending else 8)
local CacheLimit = if Config.CacheBytes == nil then 32 * 1024 * 1024 else Config.CacheBytes
local BinaryTransport = Config.BinaryTransport ~= false
local BridgeByteLimit = 64 * 1024 * 1024

if type(Url) ~= "string" or not Url:match("^wss?://") then
    return nil
end

if type(MaxPending) ~= "number" or MaxPending % 1 ~= 0 or MaxPending < 1 or MaxPending > 128 then
    return nil
end

if type(Concurrency) ~= "number" or Concurrency % 1 ~= 0 or Concurrency < 1 or Concurrency > MaxPending
    or type(MaxQueued) ~= "number" or MaxQueued ~= MaxQueued or MaxQueued % 1 ~= 0 or MaxQueued < 0 or MaxQueued > 65536
    or (Config.BinaryTransport ~= nil and type(Config.BinaryTransport) ~= "boolean")
    or type(CacheLimit) ~= "number" or CacheLimit ~= CacheLimit or CacheLimit % 1 ~= 0
    or CacheLimit < 0 or CacheLimit > 256 * 1024 * 1024 then
    return nil
end

if type(DefaultTimeout) ~= "number" or DefaultTimeout ~= DefaultTimeout or DefaultTimeout <= 0 or DefaultTimeout >= math.huge then
    return nil
end

if type(GetBytecode) ~= "function" or type(Connect) ~= "function" then
    return nil
end

local Previous = Environment.AstraBridge
local PreviousDecompile = Environment.decompile or decompile
local IsPreviousBridge = type(Previous) == "table" and Previous.Marker == "AstraBridge1"
local Original = if IsPreviousBridge then Previous.Original else PreviousDecompile
local Bridge = {
    Marker = "AstraBridge1",
    Version = 9,
    Original = Original,
    Url = Url,
    Connected = false,
    HookTargets = {},
    FilterLineField = "Line",
    LineProbe = "disabled",
    Protocol = if BinaryTransport then "ASTRA5" else "ASTRA4",
    Stats = { Requests = 0, Succeeded = 0, Failed = 0, CacheHits = 0, InputBytes = 0, OutputBytes = 0 },
}

local HookTargets = { Original, PreviousDecompile }
if IsPreviousBridge and type(Previous.HookTargets) == "table" then
    for _, Target in Previous.HookTargets do
        table.insert(HookTargets, Target)
    end
end

local Keywords = {
    ["and"] = true,
    ["break"] = true,
    ["do"] = true,
    ["else"] = true,
    ["elseif"] = true,
    ["end"] = true,
    ["false"] = true,
    ["for"] = true,
    ["function"] = true,
    ["if"] = true,
    ["in"] = true,
    ["local"] = true,
    ["nil"] = true,
    ["not"] = true,
    ["or"] = true,
    ["repeat"] = true,
    ["return"] = true,
    ["then"] = true,
    ["true"] = true,
    ["until"] = true,
    ["while"] = true,
}

local Hex = {}
for Byte = 0, 255 do
    Hex[string.char(Byte)] = string.format("%02x", Byte)
end

local Socket
local Connections = {}
local Pending = {}
local Hooks = {}
local NextId = 0
local ActiveRequests = 0
local Connecting = false
local Stopped = false
local Installed
local SlotWaiters = {}
local FirstSlot = 1
local LastSlot = 0
local QueuedRequests = 0
local Cache = {}
local CacheSize = 0
local CacheEntries = 0
local CacheClock = 0

local function NativeOptions(Value)
    -- Volt passes DecompilerOptions userdata to its cached decompile function.
    -- Read only documented properties and map the compatible formatting knobs.
    local Mapped = {}
    local Ignored = {}
    local Found = false
    local function Read(Object, Key)
        local Ok, Result = pcall(function() return Object[Key] end)
        return if Ok then Result else nil
    end
    local Rename = Read(Value, "SmartVariableRenamer")
    if type(Rename) == "boolean" then
        Mapped.Naming = if Rename then "inferred" else "generic"
        Found = true
    end
    for _, Key in { "FunctionDeclarations", "GuardClauses", "ConstantFolding", "ConditionalStructurer", "DoBlockInsertionThreshold" } do
        if Read(Value, Key) ~= nil then
            Found = true
            table.insert(Ignored, Key)
        end
    end
    local Formatter = Read(Value, "Formatter")
    if Formatter ~= nil then
        Found = true
        for Native, Astra in {
            IndentWidth = "IndentWidth",
            FunctionMetadataEnabled = "IncludeLineComments",
            FunctionMetadataIncludeUpvalues = "IncludeUpvalueComments",
        } do
            local Field = Read(Formatter, Native)
            if type(Field) == type(DefaultOptions[Astra]) then
                Mapped[Astra] = Field
            end
        end
        for _, Key in { "ColumnLimit", "ParenthesizeConditions", "AppendSemicolons", "FunctionMetadataLayout",
            "FunctionMetadataIncludeName", "FunctionMetadataIncludeLine", "FunctionMetadataUpvalueFormat" } do
            if Read(Formatter, Key) ~= nil then table.insert(Ignored, "Formatter." .. Key) end
        end
        if Read(Formatter, "FunctionMetadataEnabled") == false then
            Mapped.IncludeUpvalueComments = false
        end
    end
    return if Found then Mapped else nil, Ignored
end

local function NormalizeOverrides(Value)
    if type(Value) == "userdata" or (type(Value) == "table" and
        (Value.SmartVariableRenamer ~= nil or Value.Formatter ~= nil or Value.ConditionalStructurer ~= nil
            or Value.GuardClauses ~= nil or Value.FunctionDeclarations ~= nil or Value.ConstantFolding ~= nil
            or Value.DoBlockInsertionThreshold ~= nil)) then
        return NativeOptions(Value)
    end
    return Value
end

-- Functions
local function SameInstance(Left, Right)
    if Left == Right then
        return true
    end

    if Left and Right and type(CompareInstances) == "function" then
        local Ok, Same = pcall(CompareInstances, Left, Right)
        return Ok and Same == true
    end

    return false
end

local function QuoteName(Name)
    return '"' .. Name:gsub(".", function(Character)
        local Byte = string.byte(Character)
        if Character == '"' or Character == "\\" then
            return "\\" .. Character
        end

        if Byte < 32 or Byte >= 127 then
            return string.format("\\%03d", Byte)
        end

        return Character
    end) .. '"'
end

local function RequestOptions(Overrides)
    -- Clearing either global restores the internal defaults on the next call.
    local CurrentConfig = Environment.AstraConfig
    local GlobalOptions = Environment.AstraOptions
    if CurrentConfig == nil then
        CurrentConfig = {}
    end
    if GlobalOptions == nil then
        GlobalOptions = {}
    end
    if type(CurrentConfig) ~= "table" then
        error("AstraConfig must be a table")
    end

    if type(GlobalOptions) ~= "table" then
        error("AstraOptions must be a table")
    end

    if CurrentConfig.Options ~= nil and type(CurrentConfig.Options) ~= "table" then
        error("AstraConfig.Options must be a table")
    end

    if Overrides ~= nil and type(Overrides) ~= "table" then
        error("Per-call options must be a table")
    end

    local Options = table.clone(DefaultOptions)
    for _, Layer in { GlobalOptions, CurrentConfig.Options or {}, Overrides or {} } do
        for Key, Value in Layer do
            Options[Key] = Value
        end
    end

    local Keys = {}
    for Key, Value in Options do
        local Expected = DefaultOptions[Key]
        if type(Key) ~= "string" or Expected == nil then
            error("Invalid or reserved option key: " .. tostring(Key))
        end

        if type(Value) ~= type(Expected) then
            error("Invalid option value for " .. Key .. ": expected " .. type(Expected))
        end

        if type(Value) == "number" then
            if Value ~= Value or Value < 0 or Value >= math.huge or Value % 1 ~= 0 then
                error("Invalid option value for " .. Key .. ": expected a nonnegative finite integer")
            end
        elseif type(Value) == "string" and (Value == "" or not Value:match("^[%w_%-]+$")) then
            error("Invalid option value for " .. Key)
        end

        table.insert(Keys, Key)
    end

    table.sort(Keys)
    local Entries = {}
    for _, Key in Keys do
        local Value = Options[Key]
        local Text = if type(Value) == "string" then '"' .. Value .. '"' else tostring(Value)
        table.insert(Entries, '"' .. Key .. '":' .. Text)
    end

    -- ASTRA4 carries a flat JSON object with the same names as Astra's Options.
    local Block = "{" .. table.concat(Entries, ",") .. "}"
    if #Block > 4096 then
        error("Option block exceeds 4096 bytes")
    end

    return Options, Block
end

local function GcEnabled(Options)
    return Options.IncludeLineComments ~= false
        and Options.IncludeFunctionLocators ~= false
        and Options.IncludeGcLocators ~= false
end

local function DetectLineField()
    local Filter = filtergc or Environment.filtergc
    if type(Filter) ~= "function" or type(debug) ~= "table" or type(debug.info) ~= "function" then
        return "Line", "fallback"
    end

    local function AstraLineProbe()
        return "AstraBridge.LineProbe.20261015"
    end

    local Ok, Line = pcall(debug.info, AstraLineProbe, "l")
    if not Ok or type(Line) ~= "number" or Line < 1 then
        return "Line", "fallback"
    end

    local function ContainsProbe(Field, Value)
        local Options = {
            Constants = { "AstraBridge.LineProbe.20261015" },
            IgnoreExecutor = false,
        }
        Options[Field] = Value

        local Success, Matches = pcall(Filter, "function", Options, false)
        if not Success then
            return nil
        end

        if type(Matches) == "function" then
            return Matches == AstraLineProbe
        end

        if type(Matches) == "table" then
            for _, Match in Matches do
                if Match == AstraLineProbe then
                    return true
                end
            end
        end

        return false
    end

    for _, Field in { "StartLine", "Line" } do
        -- Reject executors that silently ignore an unknown filter field.
        if ContainsProbe(Field, Line) == true and ContainsProbe(Field, Line + 1000000) == false then
            return Field, "verified"
        end
    end

    return "Line", "fallback"
end

local function ModulePath(Target)
    if not Target:IsA("ModuleScript") then
        return "", "not a ModuleScript"
    end

    local Parts = {}
    local Ancestors = {}
    local Current = Target
    local LocalPlayer = game:GetService("Players").LocalPlayer
    local Base = "game"

    while not SameInstance(Current, game) do
        if SameInstance(Current, LocalPlayer) then
            Base = 'game:GetService("Players").LocalPlayer'
            break
        end

        local Parent = Current.Parent
        if not Parent then
            return "", "module is not parented under game"
        end

        if #Parts >= 128 then
            return "", "module hierarchy exceeds 128 levels"
        end

        if not SameInstance(Parent:FindFirstChild(Current.Name), Current) then
            return "", "ambiguous child name or uncomparable instance reference: " .. Current.Name
        end

        local MatchingChildren = 0
        for _, Child in Parent:GetChildren() do
            if Child.Name == Current.Name then
                MatchingChildren += 1
            end
        end

        if MatchingChildren ~= 1 then
            return "", "ambiguous child name: " .. Current.Name
        end

        -- Properties and methods can shadow children with otherwise valid names.
        local Ok, Member = pcall(function()
            return Parent[Current.Name]
        end)
        local Dot = Current.Name:match("^[%a_][%w_]*$")
            and not Keywords[Current.Name]
            and Ok
            and SameInstance(Member, Current)
        local Part = if Dot then "." .. Current.Name else ':FindFirstChild(' .. QuoteName(Current.Name) .. ')'
        table.insert(Parts, Part)
        table.insert(Ancestors, Parent)
        Current = Parent
    end

    -- A full game scan per module makes saves quadratic in the size of the
    -- hierarchy. Shortening is optional; spend a bounded amount of work and
    -- retain the validated explicit path when uniqueness cannot be proved.
    local SearchBudget = 512
    local function UniqueDescendant(Ancestor)
        local Stack = { Ancestor }
        local Matches = 0
        while #Stack > 0 do
            local Parent = table.remove(Stack)
            for _, Child in Parent:GetChildren() do
                if SearchBudget == 0 then return false end
                SearchBudget -= 1
                if Child.Name == Target.Name then
                    Matches += 1
                    if Matches > 1 then return false end
                end
                table.insert(Stack, Child)
            end
        end
        return Matches == 1
    end

    for Index = #Parts, 1, -1 do
        -- Recursive lookup is worthwhile only when it saves two or more segments.
        if SearchBudget > 0 and Index > 2 and (Index < #Parts or Base ~= "game") then
            local Ancestor = Ancestors[Index]
            if UniqueDescendant(Ancestor) and SameInstance(Ancestor:FindFirstChild(Target.Name, true), Target) then
                Base ..= ':FindFirstChild(' .. QuoteName(Target.Name) .. ', true)'
                break
            end
        end

        Base ..= Parts[Index]
    end

    if #Base > 8192 then
        return "", "module path exceeds 8192 bytes"
    end

    return Base, "available"
end

local function IsClosed(Current)
    -- Volt's IsClosed reports intentional closure; OnClose handles remote loss.
    local Ok, Closed = pcall(function()
        return Current.IsClosed
    end)
    return Ok and Closed == true
end

local function Unqueue(Request)
    local Index = Request.SlotIndex
    if Index and SlotWaiters[Index] == Request then
        SlotWaiters[Index] = nil
        QueuedRequests -= 1
        Request.SlotIndex = nil
    end
    while FirstSlot <= LastSlot and SlotWaiters[FirstSlot] == nil do FirstSlot += 1 end
    if FirstSlot > LastSlot then FirstSlot, LastSlot = 1, 0 end
end

local function Finish(Request, Failure)
    if Request.Done then
        return
    end
    Request.Error = Failure
    Request.Done = true
    if Request.SlotIndex then Unqueue(Request) end
    if Request.Waiter then
        if type(task.defer) == "function" then task.defer(Request.Waiter) else task.spawn(Request.Waiter) end
    end
end

local function AcquireSlot(Deadline)
    if Stopped then return nil, "Astra bridge stopped" end
    if ActiveRequests < MaxPending then
        ActiveRequests += 1
        return true
    end
    if QueuedRequests >= MaxQueued then return nil, "Astra queued request limit reached" end
    local Waiting = { Deadline = Deadline }
    LastSlot += 1
    Waiting.SlotIndex = LastSlot
    SlotWaiters[LastSlot] = Waiting
    QueuedRequests += 1
    if type(task.delay) == "function" then
        Waiting.Waiter = coroutine.running()
        Waiting.Timer = task.delay(math.max(0, Deadline - os.clock()), function()
            Finish(Waiting, "Astra pending request queue timed out")
        end)
        coroutine.yield()
        Waiting.Waiter = nil
        if type(task.cancel) == "function" then pcall(task.cancel, Waiting.Timer) end
    else
        while not Waiting.Done and os.clock() < Deadline and not Stopped do task.wait() end
        if not Waiting.Done then Finish(Waiting, if Stopped then "Astra bridge stopped" else "Astra pending request queue timed out") end
    end
    return Waiting.Granted, Waiting.Error
end

local function ReleaseSlot()
    ActiveRequests -= 1
    while FirstSlot <= LastSlot and ActiveRequests < MaxPending do
        local Waiting = SlotWaiters[FirstSlot]
        if Waiting then Unqueue(Waiting) else FirstSlot += 1 end
        if Waiting and not Waiting.Done then
            if Stopped then
                Finish(Waiting, "Astra bridge stopped")
            elseif os.clock() >= Waiting.Deadline then
                Finish(Waiting, "Astra pending request queue timed out")
            else
                Waiting.Granted = true
                ActiveRequests += 1
                Finish(Waiting)
            end
        end
    end
    if FirstSlot > LastSlot then FirstSlot, LastSlot = 1, 0 end
end

local function Disconnect(Reason)
    Bridge.Connected = false
    local OldSocket = Socket
    Socket = nil

    for _, Connection in Connections do
        pcall(function()
            Connection:Disconnect()
        end)
    end
    table.clear(Connections)

    for _, Request in Pending do
        Finish(Request, Reason or "Astra WebSocket disconnected")
    end

    if OldSocket then
        -- Close also stops Volt's automatic reconnect for this discarded client.
        pcall(function()
            OldSocket:Close()
        end)
    end
end

local function EnsureConnected(Deadline)
    while Connecting do
        if os.clock() >= Deadline or Stopped then
            return nil
        end
        task.wait()
    end

    if Stopped then
        return nil
    end

    if Socket then
        if not IsClosed(Socket) then
            return Socket
        end
        Disconnect("Astra WebSocket was closed")
    end

    Connecting = true
    local Attempt = {}
    task.spawn(function()
        Attempt.Ok, Attempt.Result = pcall(Connect, Url)
        Attempt.Done = true
        if Attempt.Abandoned and Attempt.Ok and Attempt.Result then
            pcall(function()
                Attempt.Result:Close()
            end)
        end
    end)

    while not Attempt.Done and os.clock() < Deadline and not Stopped do
        task.wait()
    end
    Connecting = false

    if not Attempt.Done then
        Attempt.Abandoned = true
        return nil
    end

    local Current = Attempt.Result
    if not Attempt.Ok or not Current then
        return nil
    end

    if Stopped or IsClosed(Current) then
        pcall(function()
            Current:Close()
        end)
        return nil
    end

    Socket = Current
    local SetupOk = pcall(function()
        Connections[1] = Current.OnMessage:Connect(function(Message, IsBinary)
            if Socket ~= Current or IsBinary == true or type(Message) ~= "string" then
                return
            end

            local Id, Status, Body = Message:match("^(%d+)\n([^\n]+)\n(.*)$")
            local Request = Id and Pending[Id]
            if not Request or Request.Done then
                return
            end

            local Failure
            if #Body > Request.MaxOutputBytes then
                Failure = "Astra response exceeded the output byte limit"
            elseif Status == "ok" then
                Request.Source = Body
            else
                Failure = if Status == "error" then Body else "Invalid Astra response status: " .. Status
            end
            Finish(Request, Failure)
        end)

        Connections[2] = Current.OnClose:Connect(function()
            if Socket == Current then
                Disconnect("Astra server disconnected. The next call will reconnect.")
            end
        end)
    end)

    if not SetupOk then
        Disconnect("Unsupported executor WebSocket event API")
        return nil
    end

    Bridge.Connected = true
    return Current
end

local function Decompile(Target, Timeout, Overrides, Deadline)
    local IgnoredNativeOptions
    local RawBytecode = type(Target) == "string"
    if not RawBytecode and (typeof(Target) ~= "Instance"
        or not (Target:IsA("LocalScript") or Target:IsA("ModuleScript") or Target:IsA("Script") or Target:IsA("CoreScript"))) then
        return nil, "Expected a script Instance or raw bytecode string"
    end

    if type(Timeout) == "table" or type(Timeout) == "userdata" then
        if Overrides ~= nil then
            return nil, "Options were supplied twice"
        end
        Overrides, IgnoredNativeOptions = NormalizeOverrides(Timeout)
        Timeout = nil
        if Overrides == nil then
            return nil, "Unsupported native decompiler options"
        end
    end

    if Overrides ~= nil then
        local Normalized, Ignored = NormalizeOverrides(Overrides)
        if Normalized == nil then return nil, "Unsupported native decompiler options" end
        Overrides = Normalized
        IgnoredNativeOptions = Ignored or IgnoredNativeOptions
    end

    Timeout = Timeout or DefaultTimeout
    if type(Timeout) ~= "number" or Timeout ~= Timeout or Timeout <= 0 or Timeout >= math.huge then
        return nil, "Timeout must be a positive finite number"
    end

    local Options, EncodedOptions = RequestOptions(Overrides)
    if GcEnabled(Options) and Bridge.LineProbe == "disabled" then
        Bridge.FilterLineField, Bridge.LineProbe = DetectLineField()
    end

    Deadline = Deadline or os.clock() + Timeout
    if os.clock() >= Deadline then return nil, "Astra pending request queue timed out" end
    local Current = EnsureConnected(Deadline)
    if not Current then
        return nil, if Stopped then "Astra bridge stopped" else "Astra connection failed or timed out"
    end

    local Ok, Bytecode = true, Target
    if not RawBytecode then
        Ok, Bytecode = pcall(GetBytecode, Target)
    end
    local InputLimit = math.min(Options.MaxInputBytes, BridgeByteLimit)
    if not Ok or type(Bytecode) ~= "string" or #Bytecode == 0 or #Bytecode > InputLimit then
        return nil, "Could not obtain nonempty bytecode within the input byte limit"
    end

    local Expression = ""
    local PathStatus = if RawBytecode then "raw bytecode has no module path" else "require lookups disabled"
    if not RawBytecode and Options.IncludeLineComments and Options.IncludeFunctionLocators and Options.IncludeRequireLocators then
        -- A restricted or changing hierarchy must not prevent bytecode recovery.
        local PathOk, RecoveredPath, Status = pcall(ModulePath, Target)
        if PathOk then
            Expression, PathStatus = RecoveredPath, Status
        else
            PathStatus = "module path unavailable"
        end
    end
    local Path = Expression:gsub(".", Hex)

    if os.clock() >= Deadline then
        return nil, "Timed out while preparing bytecode"
    end

    local Cached = Cache[Target]
    if Cached and Cached.Bytecode == Bytecode and Cached.EncodedOptions == EncodedOptions
        and Cached.ModulePath == Expression and Cached.FilterLineField == Bridge.FilterLineField then
        CacheClock += 1
        Cached.Access = CacheClock
        Bridge.Stats.CacheHits += 1
        Bridge.LastRequest = table.clone(Cached.Details)
        Bridge.LastRequest.Options = table.clone(Cached.Details.Options)
        Bridge.LastRequest.CacheHit = true
        Bridge.LastRequest.IgnoredNativeOptions = IgnoredNativeOptions
        return Cached.Source
    end
    local Encoded = if BinaryTransport then Bytecode else Bytecode:gsub(".", Hex)

    if os.clock() >= Deadline then
        return nil, "Timed out while preparing bytecode"
    end

    if Socket ~= Current or IsClosed(Current) then
        if Socket == Current then
            Disconnect("Astra WebSocket was closed before sending")
        end
        return nil, "Astra WebSocket disconnected before sending"
    end

    NextId += 1
    local Id = tostring(NextId)
    local Request = {
        MaxOutputBytes = math.min(Options.MaxOutputBytes, BridgeByteLimit),
    }
    Pending[Id] = Request

    local Sent, SendError = pcall(function()
        Current:Send(Id .. "\n" .. Bridge.Protocol .. "\n" .. Bridge.FilterLineField .. "\n" .. Path .. "\n" .. EncodedOptions .. "\n" .. Encoded, BinaryTransport)
    end)

    if not Sent then
        Pending[Id] = nil
        Disconnect("Astra send failed: " .. tostring(SendError))
        return nil, "Astra send failed: " .. tostring(SendError)
    end

    if not Request.Done and type(task.delay) == "function" then
        if Socket ~= Current or IsClosed(Current) then
            Disconnect("Astra WebSocket was closed while awaiting a response")
        else
            -- Resume directly on completion instead of charging every script a
            -- frame of polling latency. The timer also bounds lost responses.
            Request.Waiter = coroutine.running()
            Request.Timer = task.delay(math.max(0, Deadline - os.clock()), function()
                Finish(Request, "Astra request timed out")
            end)
            coroutine.yield()
            Request.Waiter = nil
            if type(task.cancel) == "function" then
                pcall(task.cancel, Request.Timer)
            end
        end
    else
        while not Request.Done and os.clock() < Deadline and not Stopped do
            if Socket ~= Current or IsClosed(Current) then
                if Socket == Current then
                    Disconnect("Astra WebSocket was closed while awaiting a response")
                end
                break
            end
            task.wait()
        end
    end
    Pending[Id] = nil

    if not Request.Done or Request.Error or type(Request.Source) ~= "string" then
        return nil, Request.Error or "Astra request timed out"
    end

    local _, ExportCount = Request.Source:gsub(" | require%(", "")
    local _, GcCount = Request.Source:gsub(" | filtergc%(", "")
    Bridge.LastRequest = {
        ModulePath = Expression,
        PathStatus = PathStatus,
        ExportLookups = ExportCount,
        GcLookups = GcCount,
        FilterLineField = Bridge.FilterLineField,
        Options = Options,
        InputBytes = #Bytecode,
        OutputBytes = #Request.Source,
        CacheHit = false,
        Protocol = Bridge.Protocol,
        IgnoredNativeOptions = IgnoredNativeOptions,
    }
    Bridge.Stats.InputBytes += #Bytecode
    Bridge.Stats.OutputBytes += #Request.Source
    local Size = #Bytecode + #Request.Source
    if CacheLimit > 0 and Size <= CacheLimit then
        -- Other calls may have filled this slot while this request yielded.
        local Replaced = Cache[Target]
        if Replaced then
            CacheSize -= Replaced.Size
            CacheEntries -= 1
            Cache[Target] = nil
        end
        while CacheSize + Size > CacheLimit or CacheEntries >= 8192 do
            local OldestKey, Oldest
            for Key, Entry in Cache do
                if not Oldest or Entry.Access < Oldest.Access then
                    OldestKey, Oldest = Key, Entry
                end
            end
            Cache[OldestKey] = nil
            CacheSize -= Oldest.Size
            CacheEntries -= 1
        end
        CacheClock += 1
        Cache[Target] = {
            Bytecode = Bytecode, Source = Request.Source, EncodedOptions = EncodedOptions,
            ModulePath = Expression, FilterLineField = Bridge.FilterLineField,
            Details = table.clone(Bridge.LastRequest), Size = Size, Access = CacheClock,
        }
        CacheSize += Size
        CacheEntries += 1
    end
    return Request.Source
end

Installed = function(Target, Timeout, Overrides)
    local Started = os.clock()
    Bridge.Stats.Requests += 1
    local CurrentConfig = Environment.AstraConfig
    local Ok, Result, Failure

    local Budget = if type(Timeout) == "number" and Timeout > 0 and Timeout < math.huge then Timeout else DefaultTimeout
    local Deadline = Started + Budget
    local Admitted, AdmissionError = AcquireSlot(Deadline)
    if not Admitted then
        Ok, Failure = true, AdmissionError
    else
        Ok, Result, Failure = pcall(Decompile, Target, Timeout, Overrides, Deadline)
        ReleaseSlot()
    end

    if Ok and Result ~= nil then
        Bridge.LastError = nil
        Bridge.LastRequest.Succeeded = true
        Bridge.LastRequest.ElapsedSeconds = os.clock() - Started
        Bridge.Stats.Succeeded += 1
        return Result
    end

    Bridge.LastError = tostring(if Ok then Failure or "Astra decompilation failed" else Result)
    Bridge.Stats.Failed += 1
    Bridge.LastRequest = {
        Succeeded = false,
        Error = Bridge.LastError,
        ElapsedSeconds = os.clock() - Started,
    }

    if type(CurrentConfig) == "table" and CurrentConfig.ErrorMode == "raise" then
        error(Bridge.LastError, 2)
    end
    return nil, Bridge.LastError
end

function Bridge.ClearCache()
    table.clear(Cache)
    CacheSize = 0
    CacheEntries = 0
end

function Bridge.DecompileMany(Targets, Timeout, Overrides)
    if type(Targets) ~= "table" then
        error("DecompileMany expects an array of scripts or bytecode strings", 2)
    end
    local Results = table.create(#Targets)
    local Next = 0
    local Remaining = math.min(Concurrency, #Targets)
    for _ = 1, Remaining do
        task.spawn(function()
            while not Stopped do
                Next += 1
                local Index = Next
                if Index > #Targets then break end
                local Ok, Source, Failure = pcall(Installed, Targets[Index], Timeout, Overrides)
                Results[Index] = { Source = if Ok then Source else nil, Error = if Ok then Failure else tostring(Source) }
            end
            Remaining -= 1
        end)
    end
    while Remaining > 0 do task.wait() end
    for Index = 1, #Targets do
        if Results[Index] == nil then Results[Index] = { Error = "Astra bridge stopped" } end
    end
    return Results
end

function Bridge.PrepareSave(Roots, Timeout, Overrides)
    if typeof(Roots) == "Instance" then Roots = { Roots } end
    if type(Roots) ~= "table" then error("PrepareSave expects an Instance or array of Instances", 2) end
    local Targets, Seen = {}, {}
    local function Add(Target)
        if Target:IsA("Script") then
            local Ok, RunContext = pcall(function() return tostring(Target.RunContext) end)
            if not Ok or (RunContext ~= "Client" and RunContext ~= "Enum.RunContext.Client") then return end
        end
        if not Seen[Target] and (Target:IsA("LocalScript") or Target:IsA("ModuleScript") or Target:IsA("CoreScript")
            or Target:IsA("Script")) then
            Seen[Target] = true
            table.insert(Targets, Target)
        end
    end
    for _, Root in Roots do
        Add(Root)
        for _, Target in Root:GetDescendants() do Add(Target) end
    end
    return Bridge.DecompileMany(Targets, Timeout, Overrides), Targets
end

function Bridge.Stop()
    if Stopped then
        return
    end

    Stopped = true
    Bridge.ClearCache()
    while FirstSlot <= LastSlot do
        local Waiting = SlotWaiters[FirstSlot]
        if Waiting then Finish(Waiting, "Astra bridge stopped") else FirstSlot += 1 end
    end
    Disconnect("Astra bridge stopped")
    for Index = #Hooks, 1, -1 do
        local Entry = Hooks[Index]
        pcall(HookFunction, Entry.Target, Entry.Backup)
    end
    table.clear(Hooks)

    if Environment.decompile == Installed then
        Environment.decompile = Original
    end

    if Environment.AstraBridge == Bridge then
        Environment.AstraBridge = nil
    end
end

-- Installation
-- Keep the current decompiler until Astra has established its first connection.
local OptionsOk, InitialOptions = pcall(RequestOptions)
if not OptionsOk then
    return nil
end

if GcEnabled(InitialOptions) then
    Bridge.FilterLineField, Bridge.LineProbe = DetectLineField()
end

if not EnsureConnected(os.clock() + DefaultTimeout) then
    return nil
end

if IsPreviousBridge and type(Previous.Stop) == "function" then
    pcall(Previous.Stop)
end

-- Hook cached decompiler references as well as replacing the global function.
Bridge.HookStatus = "unavailable"
if type(HookFunction) == "function" then
    local Replacement = Installed
    if type(WrapFunction) == "function" then
        local Ok, Wrapped = pcall(WrapFunction, Installed)
        if Ok then
            Replacement = Wrapped
        end
    end

    local Seen = {}
    for _, Target in HookTargets do
        if type(Target) == "function" and not Seen[Target] then
            Seen[Target] = true
            local Ok, Backup = pcall(HookFunction, Target, Replacement)
            if Ok and type(Backup) == "function" then
                table.insert(Hooks, { Target = Target, Backup = Backup })
                table.insert(Bridge.HookTargets, Target)
            else
                Bridge.HookError = tostring(Backup)
            end
        end
    end
    Bridge.HookStatus = if #Hooks > 0 then "installed" else "failed"
end

Environment.decompile = Installed
Bridge.Decompile = Installed
Environment.AstraBridge = Bridge
return Bridge
