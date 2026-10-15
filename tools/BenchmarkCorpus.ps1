param(
    [Parameter(Mandatory)][string[]]$Inputs,
    [Parameter(Mandatory)][string]$BaselineDirectory,
    [string]$BuildDirectory = "$PSScriptRoot/../build/Release",
    [string]$OutputDirectory = "$PSScriptRoot/../artifacts/rewrite/comparison",
    [ValidateRange(5, 100000)][int]$Samples = 100
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$Rows = @()
foreach ($InputPath in $Inputs) {
    $Name = [IO.Path]::GetFileNameWithoutExtension($InputPath)
    $BeforeText = & "$BaselineDirectory/taze-bench.exe" $InputPath $Samples
    if ($LASTEXITCODE) { throw "Baseline failed: $InputPath" }
    $AfterText = & "$BuildDirectory/taze-bench.exe" $InputPath $Samples
    if ($LASTEXITCODE) { throw "Current benchmark failed: $InputPath" }
    $BeforeText | Set-Content "$OutputDirectory/$Name.before.csv"
    $AfterText | Set-Content "$OutputDirectory/$Name.after.csv"
    $Before = $BeforeText | ConvertFrom-Csv
    $After = $AfterText | ConvertFrom-Csv
    & "$BaselineDirectory/taze.exe" $InputPath -o "$OutputDirectory/$Name.before.luau"
    if ($LASTEXITCODE) { throw "Baseline source failed: $InputPath" }
    & "$BuildDirectory/taze.exe" $InputPath -o "$OutputDirectory/$Name.after.luau"
    if ($LASTEXITCODE) { throw "Current source failed: $InputPath" }
    if ((Get-FileHash "$OutputDirectory/$Name.before.luau").Hash -ne (Get-FileHash "$OutputDirectory/$Name.after.luau").Hash) { throw "Default source changed: $InputPath" }
    $Rows += [pscustomobject][ordered]@{
        case = $Name; input_bytes = $After.input_bytes; output_bytes = $After.output_bytes; functions = $After.functions
        samples = $Samples; before_ms = $Before.median_ms; after_ms = $After.median_ms
        before_p95_ms = $Before.p95_ms; after_p95_ms = $After.p95_ms
        speedup = [math]::Round([double]$Before.median_ms / [double]$After.median_ms, 3)
        latency_reduction_percent = [math]::Round(100 * (1 - [double]$After.median_ms / [double]$Before.median_ms), 1)
        fallbacks = $After.fallbacks
    }
}
$Rows | Export-Csv -NoTypeInformation "$OutputDirectory/summary.csv"
$Rows | Format-Table -AutoSize
