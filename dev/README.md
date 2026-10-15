# Developing Astra

Most users need only the executable and [public bridge](../tools/Decompile.luau). Tests, benchmarks, and maintenance tools live here so the repository's user-facing surface stays small.

## Directory map

| Directory | Contents |
| --- | --- |
| `include/Astra/` | Public C++ API |
| `src/api/` | Entry points, options, and batch orchestration |
| `src/bytecode/` | Bytecode reader and validation |
| `src/analysis/` | Control-flow and export analysis |
| `src/reconstruction/` | Register analysis, lowering, structuring, optimization, rendering, and names |
| `src/cli/` | CLI, batch files, and Windows WebSocket server |
| `dev/tests/` | Compiler/VM tests, stress fixtures, bridge harness, and adapter tests |
| `dev/benchmarks/` | Single-file and batch benchmark programs |
| `dev/tools/` | Fixture compiler, corpus checks, live audits, and packaging helpers |
| `cmake/` | Developer-tool and test targets |
| `tools/` | Public installation scripts |
| `docs/`, `site/` | Markdown documentation and site generator |

See the [architecture](../docs/ARCHITECTURE.md) for the pipeline and [current control-flow validation](../docs/OBFUSCATED_CONTROL_FLOW.md) for recent fixes.

## Build and test

From the repository root:

```powershell
./build.ps1 -Test
```

For an existing build:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The tests cover semantic round trips, control-flow stress, inline recovery, option combinations, bridge lifecycle, WebSocket transport, and batch CLI behavior. Compiler/VM test targets require the [official Luau source](https://github.com/luau-lang/luau). `-DASTRA_BUILD_TESTS=OFF` omits those targets; API benchmarks remain available.

## Check a fixture

```powershell
./build/Release/astra-compile.exe input.luau input.luac 1 2
./build/Release/astra.exe input.luac --no-header --stats -o output.luau
./build/Release/astra-bench.exe input.luac 100
./build/Release/astra-batch-bench.exe input.luac
```

`astra-compile` takes optimization and debug levels after the output path. Keep extracted scripts, bytecode, generated outputs, and measurement logs under ignored `artifacts/`.

Reconstruction is strict by default. To inspect a deliberately forced diagnostic dispatcher, opt in explicitly:

```powershell
./build/Release/astra.exe input.luac --fallback true --state-machine --stats -o diagnostic.luau
```

Do not use diagnostic fallback to hide a failed regression case. A generated file compiling successfully is one check; semantic fixtures compare behavior too.

## Package a local build

```powershell
./dev/tools/PackageRelease.ps1
```

This writes the Windows ZIP, standalone executable, bridge, and SHA-256 checksums to `artifacts/releases/v0.5.0/`. Both copies of `Decompile.luau` contain the actual matching public bridge, so installation needs no download, published tag, or push. The optional online loader remains at [`tools/LoadDecompiler.luau`](../tools/LoadDecompiler.luau).

## Live checks and documentation

After starting the local server and installing the public bridge, [AuditControlFlow.luau](tools/AuditControlFlow.luau) compiles generated output without executing the target modules. [TestCameraModule.luau](tools/TestCameraModule.luau) exercises output variants. [PrepareRobloxTest.ps1](tools/PrepareRobloxTest.ps1) generates the controlled Roblox fixture harness.

Build and validate the documentation with `python site/build.py`. Follow the [Pages guide](../docs/GITHUB_PAGES.md) to preview or publish it. Source snippets and raw HTML are escaped, and local links are checked during the build.
