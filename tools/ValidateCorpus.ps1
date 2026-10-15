param(
    [string]$BuildDirectory = "$PSScriptRoot/../build/Release",
    [string]$Source = "$PSScriptRoot/../CameraModule.luau",
    [string]$OutputDirectory = "$PSScriptRoot/../artifacts/rewrite/source-corpus",
    [string]$BaselineDirectory = '',
    [ValidateRange(5, 100000)][int]$Samples = 100,
    [switch]$RequireIdentical
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$Rows = @()
foreach ($Optimization in 0..2) {
    foreach ($DebugLevel in @(0, 2)) {
        $Name = "Camera-O$Optimization-D$DebugLevel"
        $Bytes = Join-Path $OutputDirectory "$Name.luac"
        $Recovered = Join-Path $OutputDirectory "$Name.luau"
        & "$BuildDirectory/taze-compile.exe" $Source $Bytes $Optimization $DebugLevel
        if ($LASTEXITCODE) { throw "Compilation failed: $Name" }
        & "$BuildDirectory/taze.exe" $Bytes --no-fallback -o $Recovered
        if ($LASTEXITCODE) { throw "Decompilation failed: $Name" }
        & "$BuildDirectory/taze-compile.exe" $Recovered "$OutputDirectory/$Name.recompiled.luac"
        if ($LASTEXITCODE) { throw "Recovered source did not compile: $Name" }
        $Current = & "$BuildDirectory/taze-bench.exe" $Bytes $Samples
        if ($LASTEXITCODE) { throw "Benchmark failed: $Name" }
        $Current | Set-Content "$OutputDirectory/$Name.csv"
        $Row = $Current | ConvertFrom-Csv
        $Record = [ordered]@{ case = $Name; median_ms = $Row.median_ms; p95_ms = $Row.p95_ms; output_bytes = $Row.output_bytes; fallbacks = $Row.fallbacks }
        if ($BaselineDirectory) {
            & "$BaselineDirectory/taze.exe" $Bytes -o "$OutputDirectory/$Name.baseline.luau"
            if ($LASTEXITCODE) { throw "Baseline decompilation failed: $Name" }
            $Record.identical_source = (Get-FileHash $Recovered).Hash -eq (Get-FileHash "$OutputDirectory/$Name.baseline.luau").Hash
            if ($RequireIdentical -and -not $Record.identical_source) { throw "Default output changed: $Name" }
            $Baseline = & "$BaselineDirectory/taze-bench.exe" $Bytes $Samples
            if ($LASTEXITCODE) { throw "Baseline benchmark failed: $Name" }
            $Baseline | Set-Content "$OutputDirectory/$Name.baseline.csv"
            $Before = $Baseline | ConvertFrom-Csv
            $Record.baseline_median_ms = $Before.median_ms
            $Record.speedup = [math]::Round([double]$Before.median_ms / [double]$Row.median_ms, 3)
        }
        $Rows += [pscustomobject]$Record
    }
}
$Rows | Export-Csv -NoTypeInformation "$OutputDirectory/summary.csv"
$Rows | Format-Table -AutoSize
