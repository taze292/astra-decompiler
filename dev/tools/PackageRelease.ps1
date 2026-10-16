param([string]$Version = '0.5.1', [string]$BuildDirectory = 'build')
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must use major.minor.patch format.' }
$Project = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Binary = Join-Path $Project "$BuildDirectory/Release/astra.exe"
$Bridge = Join-Path $Project 'tools/Decompile.luau'
if (-not (Test-Path -LiteralPath $Binary)) { throw 'Build the Release executable first.' }
if (-not (Test-Path -LiteralPath $Bridge)) { throw 'The matching public bridge script is missing.' }
$Output = Join-Path $Project "artifacts/releases/v$Version"
$Package = Join-Path $Output 'package'
New-Item -ItemType Directory -Path $Package -Force | Out-Null
Copy-Item -LiteralPath $Binary -Destination (Join-Path $Output 'astra.exe')
Copy-Item -LiteralPath $Bridge -Destination (Join-Path $Output 'Decompile.luau')
Copy-Item -LiteralPath $Binary -Destination (Join-Path $Package 'astra.exe')
Copy-Item -LiteralPath $Bridge -Destination (Join-Path $Package 'Decompile.luau')
Copy-Item -LiteralPath (Join-Path $Project 'THIRD_PARTY_NOTICES.md') -Destination $Package
Copy-Item -LiteralPath (Join-Path $Project 'docs/Luau-LICENSE.txt') -Destination $Package
@'
@echo off
"%~dp0astra.exe" --serve
pause
'@ | Set-Content -LiteralPath (Join-Path $Package 'Start-Decompiler.cmd') -Encoding ascii
@'
# Astra Decompiler v__ASTRA_VERSION__

Windows x64. [Documentation](https://taze292.github.io/astra-decompiler/docs.html)

1. Extract all files from the ZIP.
2. Run `Start-Decompiler.cmd`, or run `astra.exe --serve`.
3. Keep the server running. The default port is `8877` on `127.0.0.1`.
4. Execute the included `Decompile.luau` in your Roblox executor. It installs
   the matching bridge and replaces the executor's decompile function.
5. Call `decompile(YourScriptInstance)` to obtain source.

The executor must provide `getscriptbytecode` and a compatible WebSocket API.
The included bridge needs no download or published GitHub tag.
If you already have an Astra server running, close it before starting this one.
Use `getgenv().AstraBridge.Stop()` to restore the previous decompiler.

Edit `AstraConfig` and the vertical `DefaultOptions` table in `Decompile.luau`,
or override fields in `getgenv().AstraOptions` before a call.

```powershell
astra.exe --help
astra.exe input.luac -o output.luau
```

The executable includes the C++ runtime. Clang and the Luau source are not
needed to run it. Inputs must be raw decompressed serialized Luau bytecode.
This release does not guarantee perfect recovery of every Roblox script.
Luau attribution and license are included alongside this file.

'@.Replace('__ASTRA_VERSION__', $Version) | Set-Content -LiteralPath (Join-Path $Package 'README.md') -Encoding utf8
$Files = @('astra.exe', 'Decompile.luau', 'Start-Decompiler.cmd', 'README.md', 'THIRD_PARTY_NOTICES.md', 'Luau-LICENSE.txt') |
    ForEach-Object { Join-Path $Package $_ }
$Archive = "Astra-Decompiler-v$Version-windows-x64.zip"
Compress-Archive -LiteralPath $Files -DestinationPath (Join-Path $Output $Archive) -Force
@('astra.exe', 'Decompile.luau', $Archive) | ForEach-Object {
    $Hash = (Get-FileHash -LiteralPath (Join-Path $Output $_) -Algorithm SHA256).Hash.ToLowerInvariant()
    "$Hash  $_"
} | Set-Content -LiteralPath (Join-Path $Output 'SHA256SUMS.txt') -Encoding ascii
Write-Output $Output
