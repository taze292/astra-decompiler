param([Parameter(Mandatory)][string]$Executable, [Parameter(Mandatory)][string]$Compiler, [Parameter(Mandatory)][string]$WorkDirectory)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $WorkDirectory | Out-Null
# Unique output directories keep repeated runs independent without deleting user files.
$OutputDirectory = Join-Path $WorkDirectory ([guid]::NewGuid().ToString('N'))
$One = Join-Path $WorkDirectory 'one.luac'
$Two = Join-Path $WorkDirectory 'two.luac'
$Bad = Join-Path $WorkDirectory 'bad.luac'
$Source = Join-Path $WorkDirectory 'fixture.luau'
[IO.File]::WriteAllText($Source, 'local function Count(x) return x + 17 end; return Count(25)')
& $Compiler $Source $One
if ($LASTEXITCODE) { throw 'Fixture compiler failed' }
Copy-Item -LiteralPath $One -Destination $Two -Force
[IO.File]::WriteAllText($Bad, 'invalid')
& $Executable --batch --jobs 4 --output-dir $OutputDirectory $One $Bad $Two
if ($LASTEXITCODE -ne 1) { throw 'Mixed batch must report a failure' }
if (-not (Test-Path "$OutputDirectory/one.luau") -or -not (Test-Path "$OutputDirectory/two.luau") -or (Test-Path "$OutputDirectory/bad.luau")) { throw 'Mixed batch output isolation failed' }
if ((Get-FileHash "$OutputDirectory/one.luau").Hash -ne (Get-FileHash "$OutputDirectory/two.luau").Hash) { throw 'Parallel outputs differ' }
& $Executable --batch --output-dir $OutputDirectory $One
if ($LASTEXITCODE -ne 1) { throw 'Existing batch output overwritten without flag' }
& $Executable --batch --output-dir $OutputDirectory --overwrite $One $Two
if ($LASTEXITCODE) { throw 'Explicit batch overwrite failed' }
& $Executable --batch --output-dir "$OutputDirectory/collision" $One $One
if ($LASTEXITCODE -ne 1 -or (Test-Path "$OutputDirectory/collision")) { throw 'Collision not rejected before writes' }
& $Executable $One --indent 2junk -o "$OutputDirectory/invalid.luau"
if ($LASTEXITCODE -ne 1 -or (Test-Path "$OutputDirectory/invalid.luau")) { throw 'Invalid option created output' }
& $Executable --batch --output-dir "$OutputDirectory/nested" --jobs 33 $One
if ($LASTEXITCODE -ne 1) { throw 'Unbounded worker count accepted' }
Write-Output '7 batch CLI checks passed'
