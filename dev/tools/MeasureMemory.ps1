param(
    [Parameter(Mandatory)][string]$InputPath,
    [Parameter(Mandatory)][string]$BaselineDirectory,
    [string]$BuildDirectory = "$PSScriptRoot/../../build/Release",
    [string]$OutputDirectory = "$PSScriptRoot/../../artifacts/rewrite/memory"
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$Rows = @()
foreach ($Variant in @('baseline', 'current')) {
    for ($Run = 1; $Run -le 3; $Run++) {
        $Directory = if ($Variant -eq 'baseline') { $BaselineDirectory } else { $BuildDirectory }
        $Start = [Diagnostics.ProcessStartInfo]::new()
        $Start.FileName = (Resolve-Path "$Directory/astra-bench.exe").Path
        $Start.UseShellExecute = $false
        $Start.CreateNoWindow = $true
        $Start.RedirectStandardOutput = $true
        $Start.RedirectStandardError = $true
        $Start.ArgumentList.Add((Resolve-Path $InputPath).Path)
        $Start.ArgumentList.Add('100')
        $Process = [Diagnostics.Process]::Start($Start)
        $PeakWorking = 0L
        $PeakPaged = 0L
        while (-not $Process.HasExited) {
            $Process.Refresh()
            $PeakWorking = [math]::Max($PeakWorking, $Process.PeakWorkingSet64)
            $PeakPaged = [math]::Max($PeakPaged, $Process.PeakPagedMemorySize64)
            Start-Sleep -Milliseconds 10
        }
        $Process.WaitForExit()
        $Text = $Process.StandardOutput.ReadToEnd()
        $Failure = $Process.StandardError.ReadToEnd()
        if ($Process.ExitCode) { throw $Failure }
        $Text | Set-Content "$OutputDirectory/$Variant-$Run.csv"
        $Rows += [pscustomobject]@{ variant = $Variant; run = $Run; peak_working_set_bytes = $PeakWorking; peak_paged_bytes = $PeakPaged }
        $Process.Dispose()
    }
}
$Rows | Export-Csv -NoTypeInformation "$OutputDirectory/summary.csv"
$Rows | Format-Table
