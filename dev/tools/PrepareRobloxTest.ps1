param([string]$BuildDirectory = "$PSScriptRoot/../../build/Release")
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ArtifactDirectory = Join-Path $ProjectRoot 'artifacts'
New-Item -ItemType Directory -Path $ArtifactDirectory -Force | Out-Null
$Fixture = Join-Path $ProjectRoot 'examples/RobloxFixture.luau'
& "$BuildDirectory/astra-compile.exe" $Fixture "$ArtifactDirectory/RobloxFixture.luac" 1 2
if ($LASTEXITCODE) { throw 'Fixture compilation failed.' }
& "$BuildDirectory/astra.exe" "$ArtifactDirectory/RobloxFixture.luac" -o "$ArtifactDirectory/RobloxFixture.decompiled.luau"
if ($LASTEXITCODE) { throw 'Fixture decompilation failed.' }
$Original = Get-Content -LiteralPath $Fixture -Raw
$Decompiled = Get-Content -LiteralPath "$ArtifactDirectory/RobloxFixture.decompiled.luau" -Raw
$Harness = @'
getgenv().AstraTestResult = { Running = true }
local Ok, Failure = pcall(function()
local function Run(Source)
    local Function, CompileError = loadstring(Source, "AstraFixture")
    assert(Function, CompileError)
    return table.pack(Function())
end

'@
$Harness += "`nlocal Expected = Run([====[`n" + $Original + "`n]====])`n"
$Harness += "local Actual = Run([====[`n" + $Decompiled + "`n]====])`n"
$Harness += @'
assert(Expected.n == Actual.n, "Return count differs")
for Index = 1, Expected.n do
    assert(Expected[Index] == Actual[Index], "Return value differs at " .. Index)
end
getgenv().AstraTestResult.ReturnCount = Actual.n
getgenv().AstraTestResult.Values = Actual
'@
if (Test-Path -LiteralPath "$ArtifactDirectory/CameraModule.luac") {
    & "$BuildDirectory/astra.exe" "$ArtifactDirectory/CameraModule.luac" -o "$ArtifactDirectory/CameraModule.luau"
    if ($LASTEXITCODE) { throw 'CameraModule decompilation failed.' }
    $CameraSource = Get-Content -LiteralPath "$ArtifactDirectory/CameraModule.luau" -Raw
    $Harness += "`nlocal CameraFunction, CameraError = loadstring([====[`n" + $CameraSource + "`n]====], 'AstraCameraModule')`n"
    $Harness += "assert(CameraFunction, CameraError)`ngetgenv().AstraTestResult.CameraCompiled = true`n"
}
$Harness += "`nend)`nlocal Report = getgenv().AstraTestResult`nReport.Running = false`nReport.Passed = Ok`nReport.Error = if Ok then nil else tostring(Failure)`nReport.Version = version()`n"
[IO.File]::WriteAllText("$ArtifactDirectory/RobloxTest.luau", $Harness)
Write-Output "$ArtifactDirectory/RobloxTest.luau"
