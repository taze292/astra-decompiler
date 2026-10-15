param([string]$Version = '0.4.0', [string]$BuildDirectory = 'build')
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must use major.minor.patch format.' }
$Project = Split-Path $PSScriptRoot -Parent
$Binary = Join-Path $Project "$BuildDirectory/Release/astra.exe"
$Loader = Join-Path $PSScriptRoot 'LoadDecompiler.luau'
$MainUrl = 'https://raw.githubusercontent.com/taze292/astra-decompiler/main/tools/Decompile.luau'
$Url = "https://raw.githubusercontent.com/taze292/astra-decompiler/v$Version/tools/Decompile.luau"
if (-not (Test-Path -LiteralPath $Binary)) { throw 'Build the Release executable first.' }
$LoaderSource = Get-Content -LiteralPath $Loader -Raw
if (-not $LoaderSource.Contains('local SourceUrl = "' + $MainUrl + '"')) {
    throw 'The source loader URL changed. Update PackageRelease.ps1 to match it before packaging.'
}
# Pin only the staged loader. The tracked development loader continues to use main.
$ReleaseLoader = $LoaderSource.Replace($MainUrl, $Url)
$Utf8 = [System.Text.UTF8Encoding]::new($false)
$Output = Join-Path $Project "artifacts/releases/v$Version"
$Package = Join-Path $Output 'package'
New-Item -ItemType Directory -Path $Package -Force | Out-Null
Copy-Item -LiteralPath $Binary -Destination (Join-Path $Output 'astra.exe')
[System.IO.File]::WriteAllText((Join-Path $Output 'Decompile.luau'), $ReleaseLoader, $Utf8)
Copy-Item -LiteralPath $Binary -Destination (Join-Path $Package 'astra.exe')
[System.IO.File]::WriteAllText((Join-Path $Package 'Decompile.luau'), $ReleaseLoader, $Utf8)
Copy-Item -LiteralPath (Join-Path $Project 'THIRD_PARTY_NOTICES.md') -Destination $Package
Copy-Item -LiteralPath (Join-Path $Project 'docs/Luau-LICENSE.txt') -Destination $Package
@'
@echo off
"%~dp0astra.exe" --serve
pause
'@ | Set-Content -LiteralPath (Join-Path $Package 'Start-Decompiler.cmd') -Encoding ascii
@"
Astra Decompiler v$Version - Windows x64

1. Extract all files from the ZIP.
2. Run Start-Decompiler.cmd, or run: astra.exe --serve
3. Keep the server running. The default port is 8877 on 127.0.0.1.
4. Execute Decompile.luau in your Roblox executor. It downloads the bridge
   from the v$Version GitHub tag and replaces the executor's decompile function.
5. Call decompile(YourScriptInstance) to obtain source.

The executor must provide getscriptbytecode and a compatible WebSocket API.
The loader also requires loadstring and game:HttpGet.
If you already have an Astra server running, close it before starting this one.
Use getgenv().AstraBridge.Stop() to restore the previous decompiler.

Offline bridge script: tools/Decompile.luau in the tagged GitHub source.
Loader URL: $Url
CLI help: astra.exe --help
File input: astra.exe input.luac -o output.luau

The executable includes the C++ runtime. Clang and the Luau source are not
needed to run it. Inputs must be raw decompressed serialized Luau bytecode.
This release does not guarantee perfect recovery of every Roblox script.
Luau attribution and license are included alongside this file.

Documentation: https://github.com/taze292/astra-decompiler/tree/v$Version
"@ | Set-Content -LiteralPath (Join-Path $Package 'README.txt') -Encoding utf8
$Files = @('astra.exe', 'Decompile.luau', 'Start-Decompiler.cmd', 'README.txt', 'THIRD_PARTY_NOTICES.md', 'Luau-LICENSE.txt') |
    ForEach-Object { Join-Path $Package $_ }
$Archive = "Astra-Decompiler-v$Version-windows-x64.zip"
Compress-Archive -LiteralPath $Files -DestinationPath (Join-Path $Output $Archive) -Force
@('astra.exe', 'Decompile.luau', $Archive) | ForEach-Object {
    $Hash = (Get-FileHash -LiteralPath (Join-Path $Output $_) -Algorithm SHA256).Hash.ToLowerInvariant()
    "$Hash  $_"
} | Set-Content -LiteralPath (Join-Path $Output 'SHA256SUMS.txt') -Encoding ascii
Write-Output $Output
