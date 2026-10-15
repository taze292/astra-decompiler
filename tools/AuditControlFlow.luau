-- Silent compile-only audit of replicated scripts. Inspect TazeControlFlowAudit.
local Environment = getgenv()
local Bridge = assert(Environment.TazeBridge, "Install the local bridge first")
local Options = Environment.TazeControlFlowAuditOptions or {
    header = false, upvalues = false, lines = false, ["state-machine"] = false,
    casing = "pascal", naming = "inferred",
}
local Started = os.clock()
local Targets, Seen = {}, {}
local CoreGui = game:FindFirstChild("CoreGui")
local CorePackages = game:FindFirstChild("CorePackages")
local function Add(Target)
    if Seen[Target] or not (Target:IsA("ModuleScript") or Target:IsA("LocalScript")) then return end
    if (CoreGui and Target:IsDescendantOf(CoreGui)) or (CorePackages and Target:IsDescendantOf(CorePackages)) then return end
    Seen[Target] = true
    table.insert(Targets, Target)
end
for _, Target in game:GetDescendants() do Add(Target) end
if type(getscripts) == "function" then
    for _, Target in getscripts() do Add(Target) end
end
table.sort(Targets, function(A, B) return A:GetFullName() < B:GetFullName() end)
local Report = { Running = true, Total = #Targets, Tested = 0, Compiled = 0, Fallbacks = 0, EmptySources = 0, Cases = {}, Errors = {}, Retries = {} }
Environment.TazeControlFlowAudit = Report
Environment.TazeControlFlowAuditTargets = Targets
for Index, Target in Targets do
    if Report.Cancelled then break end
    local Path = Target:GetFullName()
    local Ok, Source = pcall(Bridge.Decompile, Target, 90, Options)
    local Failure = if Ok then Bridge.LastError or tostring(Source) else tostring(Source)
    if not (Ok and type(Source) == "string") and
        (Failure:find("disconnected", 1, true) or Failure:find("send failed", 1, true) or Failure:find("connection failed", 1, true)) then
        table.insert(Report.Retries, { Index = Index, Path = Path, Error = Failure })
        Ok, Source = pcall(Bridge.Decompile, Target, 90, Options)
    end
    Report.Tested += 1
    if Ok and type(Source) == "string" then
        if #Source == 0 then Report.EmptySources += 1 end
        local Compiled, Error = loadstring(Source, "TazeFlowAudit")
        local Reasons = {}
        for Line in Source:gmatch("[^\n]+") do
            if Line:find("-- Control flow required an explicit state machine.", 1, true) then
                table.insert(Reasons, Line:match("%-%- (.*)") or Line)
            end
        end
        Report.Fallbacks += #Reasons
        if Compiled then Report.Compiled += 1 end
        if #Reasons > 0 or not Compiled then
            table.insert(Report.Cases, { Index = Index, Path = Path, Bytes = #Source, Reasons = Reasons, Error = Error })
        end
    else
        table.insert(Report.Errors, { Index = Index, Path = Path, Error = Bridge.LastError or tostring(Source) })
    end
    if Index % 10 == 0 then task.wait() end
end
Report.Running = false
Report.ElapsedSeconds = os.clock() - Started
return Report
