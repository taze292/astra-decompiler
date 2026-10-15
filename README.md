# Astra Decompiler

A C++20 Luau bytecode decompiler with a library, CLI, Windows WebSocket bridge, and bounded parallel batch API. It reconstructs structured Luau, preserves tested capture lifetimes and evaluation order, and reports when state-machine fallback is necessary.

The local **Astra 0.4.0 update** builds on the staged decoder, analysis, lowering, structuring, simplification, rendering, and naming pipeline. It adds a readable PascalCase bridge/options table, safe array-prefix compaction, bracketed upvalue comments, and spacing controls. See [all options](docs/OPTIONS.md), [architecture](docs/ARCHITECTURE.md), [current validation](docs/ASTRA_VALIDATION.md), and [performance](docs/PERFORMANCE.md).

The Luau checkout, build products, supplied CameraModule source, and extracted scripts remain local and excluded from Git. Use the locally built executable and local bridge while reviewing this update. The optional download loader points to the renamed Astra repository's main branch and is ready after you rename and publish the repository.

The documentation site and Pages workflow are ready in this checkout. Follow the [repository rename and GitHub Pages guide](docs/GITHUB_PAGES.md); an extra Pages branch is not required. See [the Astra migration guide](docs/ASTRA_MIGRATION.md) for new API, options, executable, and protocol names.

## Build and test

```powershell
./build.ps1 -Test
```

The Windows helper finds CMake and builds with ClangCL and the Windows SDK. Products in `build/Release/`:

- `astra.exe`: decompilation, disassembly, batches, and Windows server.
- `AstraDecompiler.lib`: reusable C++ library.
- `astra-compile.exe`: Luau fixture compiler.
- `astra-tests.exe` and `astra-pipeline-tests.exe`: semantic and pipeline tests.
- `astra-bench.exe` and `astra-batch-bench.exe`: deterministic latency/throughput benchmarks.

Default dependency locations are `luau/` or `luau-master/luau-master/`. Override with `./build.ps1 -Test -LuauDirectory C:/path/to/luau`. On other platforms:

```sh
cmake -S . -B build -DASTRA_LUAU_DIR=/path/to/luau -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Obtain the [official Luau source](https://github.com/luau-lang/luau) separately for a fresh clone. `-DASTRA_BUILD_TESTS=OFF` omits compiler/VM tests and the fixture compiler. The decompiler needs Luau's bytecode-definition headers at build time, not its compiler/VM or checkout at runtime.

The tested snapshot declares instructions through `FASTPCALL`/`NEWCLASS` and ordinary bytecode versions through 14. `Common/include/Luau/Bytecode.h` SHA-256:

```text
efdb830caeeac39f2d57c8bcf62671f7e77e5309ab718cb3ad06d9205551d52f
```

## Live Roblox use

```powershell
./build/Release/astra.exe --serve
```

Execute **this checkout's `tools/Decompile.luau`** in the executor. Bridge v7 connects before replacing `getgenv().decompile`; supported cached references are hooked and restored on stop. The readable script follows [Volt's current WebSocket API](https://docs.voltbz.net/docs/websocket) and does not print or warn.

```luau
local Camera = game:GetService("StarterPlayer"):FindFirstChild("CameraModule", true)
local Source = decompile(Camera)
local Snake = decompile(Camera, {
    Casing = "Snake",
    PreserveFunctionNames = false,
    IncludeGcLocators = false,
    IncludeRequireLocators = true,
})
local Generic = decompile(Camera, 60, {
    Naming = "Generic",
    UseTypeNames = false,
})
```

Set defaults before installing the bridge:

```luau
getgenv().AstraConfig = {
    Url = "ws://127.0.0.1:8877/decompile",
    Timeout = 30,
    MaxPending = 32,
    ErrorMode = "silent", -- or "raise" for debugging
}
getgenv().AstraOptions = {
    Casing = "Camel",
    Naming = "Inferred",
    IncludeUpvalueComments = false,
    IncludeRequireLocators = false,
    IncludeGcLocators = true,
    MethodSyntax = true,
    NativeCaptures = true,
}
```

Edit or replace `AstraOptions` at any time; every request sends a snapshot as JSON over WebSocket. Per-call options override it. See [all options](docs/OPTIONS.md) for naming prefixes, inference switches, casing, compact comments, spacing, and resource limits. The header reports bytecode version and real elapsed seconds; disable it for deterministic full-file comparisons.

Captured locals now use lexical references where lifetime analysis proves safe, eliminating the closing table-copy noise in ordinary modules. Loop-created or reused capture identities retain conservative boxes. Methods use implicit lowercase `self`, arithmetic temporaries are folded where evaluation order permits, and loop guards become direct while conditions. See [the readability rewrite record](docs/READABILITY_REWRITE.md).

Failures return nil by default. `AstraBridge.LastError` explains the failure; `.LastRequest` reports timing, sizes, request overrides, lookup counts, and path status. A timeout stops waiting, without cancelling C++ work. Calls reconnect after disconnection. `AstraBridge.Decompile` is a direct entry point; `AstraBridge.Stop()` restores previous functions.

Required executor APIs: `getscriptbytecode` and `WebSocket.connect` (or its `websocket.connect` alias), with `Send`, `Close`, `OnMessage`, and `OnClose`. Volt's `IsClosed` property lets the bridge detect closed sockets even without a close event. The server binds only to IPv4 loopback, handles up to eight clients, supports masked/fragmented messages and ping/pong, and closes sockets after ten idle minutes. Browser HTTP(S) origins are rejected. Each socket processes requests in order. The server never executes submitted code or accesses client-supplied paths.

## Files and batches

```powershell
./build/Release/astra.exe input.luac -o output.luau
./build/Release/astra.exe input.luac --casing snake --no-locators --stats -o output.luau
./build/Release/astra.exe input.luac --disassemble -o instructions.asm
./build/Release/astra.exe --batch --jobs 4 --output-dir artifacts/output a.luac b.luac
./build/Release/astra.exe --help
```

Input must be **raw, decompressed serialized Luau bytecode**, such as `getscriptbytecode` returns. Source, hex/base64 text, compressed/encrypted assets, process memory, and machine code are not supported inputs. Auto encoding validates the full instruction stream; override with `--encoding plain` or `--encoding roblox`. Roblox opcode decoding multiplies opcode bytes by 203 modulo 256, leaving AUX words unchanged. An observed 24-byte Roblox trailer is accepted only after Roblox instruction validation; it is opaque, not authenticated.

Batch mode preserves input order, isolates file failures, rejects filename collisions, and requires `--overwrite` for existing outputs. Workers default to one; `--jobs 0` selects up to eight hardware threads, and explicit counts are capped at 32. A single request remains deterministic and sequential internally.

## Output controls

| Area | Choices |
| --- | --- |
| Binding case | PascalCase, camelCase, snake_case, preserve |
| Naming | inferred hints, debug names with fallbacks, generic names |
| Anonymous names | type categories or uniform ValueN |
| Function spelling | preserve original names or apply selected casing |
| Comments | header, lines, multiline/compact captures, independent capture indexes, GC lookups, require lookups, all locators |
| Arrays | safe consecutive numeric keys become list entries by default |
| Spacing | spaces or tabs, group blank lines, table-brace padding |
| Reconstruction | simplification passes, forced state machine, fail instead of fallback |
| Limits | input/output bytes, analysis cells/work, closure depth |

Globals, fields, methods, strings, and lookup metadata retain their identity. Locals avoid reserved words, globals, and enclosing bindings. Names are assigned to surviving symbols after optimization. Formatting uses configurable indentation, no statement semicolons, and grouped declarations/control flow.

Function comments contain a line number and optional copyable lookup. A statically proven export uses `require(ModulePath).Field`; otherwise GC locators use serialized name/line metadata and, when needed, up to five representable constants. The bridge probes `StartLine`/`Line` with positive and negative controls. It never requires the module while decompiling.

`--no-gc-locators` disables only GC expressions. `--no-require-locators` disables only exports. `--no-locators` disables both. Legacy `--no-filtergc` still disables both for compatibility. `--no-lines` hides the entire function line/lookup comment. [OPTIONS.md](docs/OPTIONS.md) specifies every field, default, interaction, and bridge setting.

## C++ API

```cpp
#include <Astra/Decompiler.hpp>

Astra::Options Settings;
Settings.Casing = Astra::VariableCase::Snake;
Settings.PreserveFunctionNames = false;
Settings.IncludeGcLocators = false;
auto Result = Astra::Decompile(Bytecode, Settings);
// Source, Diagnostics, Stats, BytecodeVersion, FunctionCount,
// StateMachineFunctions, Encoding, etc.

std::vector<std::string_view> Inputs = {FirstBytecode, SecondBytecode};
auto Batch = Astra::DecompileBatch(Inputs, Settings, 4);
// Ordered entries: Succeeded(), Output, Failure. Input storage lives until return.
```

Malformed/unsupported inputs and invalid settings throw `Astra::Error`. Batch item failures are captured individually. `Disassemble` exposes prototypes, constants, local ranges, instructions, captures, and targets. `ApplyOption`/`ValidateOptions` share the adapters' strict rules. Limits constrain particular resources, not total RSS or a universal wall-clock timeout.

## Validation

CTest covers compiler/VM round trips, configuration combinations, malformed/truncated inputs, deterministic mutation stress, concurrent batches, mock executor lifecycle, real WebSocket requests, and batch CLI errors. [VALIDATION.md](docs/VALIDATION.md) records results and historical checks; [PERFORMANCE.md](docs/PERFORMANCE.md) records baseline comparisons and reproduction steps.

[CONTROL_FLOW_AUDIT.md](docs/CONTROL_FLOW_AUDIT.md) covers the latest control-flow and evaluation-order fixes, differential stress matrix, live audit, and remaining limits. [INLINE_RECOVERY.md](docs/INLINE_RECOVERY.md) explains recovery of compiler-inlined calls where explicit fallback bytecode survives.

```powershell
./build/Release/astra-bench.exe artifacts/CameraModule.luac 100
./build/Release/astra-batch-bench.exe artifacts/CameraModule.luac
./tools/ValidateCorpus.ps1
./tools/PrepareRobloxTest.ps1
```

Execute `tools/TestCameraModule.luau` through Roblox MCP after installing the bridge. It compiles option variants and checks concurrent requests without executing or replacing CameraModule. Execute generated `artifacts/RobloxTest.luau` separately for the behavioral fixture. Results appear in `AstraRewriteValidation` and `AstraTestResult`.

## Compatibility and limits

The reader supports serialized versions **3–14**, type metadata 1–3, and the experimental version-100 envelope. Compiler/VM tests exercise versions 9 and 11–14, both encodings, and the v12 float/v13 double-vector boundary. Older versions lack historical compiler binaries here. The current live CameraModule fixture uses version 14.

Reconstruction covers arithmetic, comparisons, imports, tables/members, short-circuit branches, methods, fixed/open tuples, varargs, closures, copy/reference captures, closing upvalues, recursion, numeric/generic loops, and fastcall fallback paths. Conservative dataflow and explicit capture snapshots preserve tested lifetimes and evaluation order.

- Unsupported control flow uses a diagnosed `ProgramCounter` loop unless disabled. Canonical generic loops retain native iteration within that dispatcher. Noncanonical generalized loops may require a diagnosed coroutine adapter that changes coroutine identity/yielding context; ordinary function iterators are called directly.
- Experimental classes, runtime-patched `NATIVECALL`, nonzero fourth vector components, and unknown future formats are rejected.
- Debug-stack inspection, closure identity, environment manipulation, and undefined iteration/table-length behavior are not promised to match.
- Original comments, formatting, types, and stripped names cannot be recovered exactly. Integer/vector output requires the corresponding runtime libraries.
- Compiling CameraModule establishes source compatibility, not complete behavioral equivalence of the controller.

The implementation studies the bundled official `Bytecode.h`, `BytecodeUtils.h`, `lvmload.cpp`, and `lvmexecute.cpp`. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
