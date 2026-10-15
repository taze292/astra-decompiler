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
    AllowFallback = true,
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
local BridgeByteLimit = 64 * 1024 * 1024

if type(Url) ~= "string" or not Url:match("^wss?://") then
    return nil
end

if type(MaxPending) ~= "number" or MaxPending % 1 ~= 0 or MaxPending < 1 or MaxPending > 128 then
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
    Version = 7,
    Original = Original,
    Url = Url,
    Connected = false,
    HookTargets = {},
    FilterLineField = "Line",
    LineProbe = "disabled",
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
    local CurrentConfig = Environment.AstraConfig
    if type(CurrentConfig) ~= "table" then
        error("AstraConfig must be a table")
    end

    if type(Environment.AstraOptions) ~= "table" then
        error("AstraOptions must be a table")
    end

    if CurrentConfig.Options ~= nil and type(CurrentConfig.Options) ~= "table" then
        error("AstraConfig.Options must be a table")
    end

    if Overrides ~= nil and type(Overrides) ~= "table" then
        error("Per-call options must be a table")
    end

    local Options = table.clone(DefaultOptions)
    for _, Layer in { Environment.AstraOptions, CurrentConfig.Options or {}, Overrides or {} } do
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

    for Index = #Parts, 1, -1 do
        -- Recursive lookup is worthwhile only when it saves two or more segments.
        if Index > 2 and (Index < #Parts or Base ~= "game") then
            local Ancestor = Ancestors[Index]
            local Matches = 0
            for _, Descendant in Ancestor:GetDescendants() do
                if Descendant.Name == Target.Name then
                    Matches += 1
                    if Matches > 1 then
                        break
                    end
                end
            end

            if Matches == 1 and SameInstance(Ancestor:FindFirstChild(Target.Name, true), Target) then
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
        Request.Error = Reason or "Astra WebSocket disconnected"
        Request.Done = true
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

            if #Body > Request.MaxOutputBytes then
                Request.Error = "Astra response exceeded the output byte limit"
            elseif Status == "ok" then
                Request.Source = Body
            else
                Request.Error = Body or "Invalid Astra response"
            end
            Request.Done = true
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

local function Decompile(Target, Timeout, Overrides)
    if typeof(Target) ~= "Instance"
        or not (Target:IsA("LocalScript") or Target:IsA("ModuleScript") or Target:IsA("Script") or Target:IsA("CoreScript")) then
        return nil, "Expected a script Instance"
    end

    if type(Timeout) == "table" then
        if Overrides ~= nil then
            return nil, "Options were supplied twice"
        end
        Overrides, Timeout = Timeout, nil
    end

    Timeout = Timeout or DefaultTimeout
    if type(Timeout) ~= "number" or Timeout ~= Timeout or Timeout <= 0 or Timeout >= math.huge then
        return nil, "Timeout must be a positive finite number"
    end

    local Options, EncodedOptions = RequestOptions(Overrides)
    if GcEnabled(Options) and Bridge.LineProbe == "disabled" then
        Bridge.FilterLineField, Bridge.LineProbe = DetectLineField()
    end

    local Deadline = os.clock() + Timeout
    local Current = EnsureConnected(Deadline)
    if not Current then
        return nil, if Stopped then "Astra bridge stopped" else "Astra connection failed or timed out"
    end

    local Ok, Bytecode = pcall(GetBytecode, Target)
    local InputLimit = math.min(Options.MaxInputBytes, BridgeByteLimit)
    if not Ok or type(Bytecode) ~= "string" or #Bytecode == 0 or #Bytecode > InputLimit then
        return nil, "Could not obtain nonempty bytecode within the input byte limit"
    end

    local Encoded = Bytecode:gsub(".", Hex)
    local Expression = ""
    local PathStatus = "require lookups disabled"
    if Options.IncludeLineComments and Options.IncludeFunctionLocators and Options.IncludeRequireLocators then
        Expression, PathStatus = ModulePath(Target)
    end
    local Path = Expression:gsub(".", Hex)

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
        Current:Send(Id .. "\nASTRA4\n" .. Bridge.FilterLineField .. "\n" .. Path .. "\n" .. EncodedOptions .. "\n" .. Encoded)
    end)

    if not Sent then
        Pending[Id] = nil
        Disconnect("Astra send failed: " .. tostring(SendError))
        return nil, "Astra send failed: " .. tostring(SendError)
    end

    while not Request.Done and os.clock() < Deadline and not Stopped do
        if Socket ~= Current or IsClosed(Current) then
            if Socket == Current then
                Disconnect("Astra WebSocket was closed while awaiting a response")
            end
            break
        end
        task.wait()
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
    }
    return Request.Source
end

Installed = function(...)
    local Started = os.clock()
    local CurrentConfig = Environment.AstraConfig
    local Ok, Result, Failure

    if ActiveRequests >= MaxPending then
        Ok, Failure = true, "Astra pending request limit reached"
    else
        ActiveRequests += 1
        Ok, Result, Failure = pcall(Decompile, ...)
        ActiveRequests -= 1
    end

    if Ok and Result ~= nil then
        Bridge.LastError = nil
        Bridge.LastRequest.Succeeded = true
        Bridge.LastRequest.ElapsedSeconds = os.clock() - Started
        return Result
    end

    Bridge.LastError = tostring(if Ok then Failure or "Astra decompilation failed" else Result)
    Bridge.LastRequest = {
        Succeeded = false,
        Error = Bridge.LastError,
        ElapsedSeconds = os.clock() - Started,
    }

    if type(CurrentConfig) == "table" and CurrentConfig.ErrorMode == "raise" then
        error(Bridge.LastError, 2)
    end
    return nil
end

function Bridge.Stop()
    if Stopped then
        return
    end

    Stopped = true
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
