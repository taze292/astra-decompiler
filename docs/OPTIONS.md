# Configuration reference

The C++ API, CLI, and bridge use the same option parser and validation rules. Defaults preserve the original PascalCase output. Configuration changes local bindings and generated comments; it never renames globals, methods, table keys, string literals, or debug names used to identify live functions.

## Shared options

CLI syntax is `--key value`. Boolean switches also support `--no-key`; `--state-machine` alone means true. In Luau use a table with these exact keys, for example `{ casing = "snake", ["gc-locators"] = false }`. C++ callers can set the field directly or call `Taze::ApplyOption(Options, "casing", "snake")`.

| Key | C++ field | Default | Values / meaning |
| --- | --- | --- | --- |
| `encoding` | `Encoding` | `Auto` | `auto`, `plain`, `roblox`; validate the whole instruction stream |
| `casing` | `Casing` | `Pascal` | `pascal`, `camel`, `snake`, `preserve` |
| `naming` | `Naming` | `Inferred` | `inferred`, `debug`, `generic` |
| `type-names` | `UseTypeNames` | true | Use TableN, NumberN, etc. for anonymous values with an unambiguous type |
| `preserve-function-names` | `PreserveFunctionNames` | true | Keep recovered original function bindings' spelling; false applies the selected casing |
| `indent` | `IndentWidth` | 4 | Spaces per level, 1–16 |
| `header` | `IncludeHeader` | true | Generator/version header |
| `lines` | `IncludeLineComments` | true | Function line comments, including their lookup expressions |
| `upvalues` | `IncludeUpvalueComments` | true | Numbered Copy/Reference capture comments |
| `locators` | `IncludeFunctionLocators` | true | Master switch for both kinds of function lookup |
| `gc-locators` | `IncludeGcLocators` | true | Allow `filtergc(...)` lookups |
| `require-locators` | `IncludeRequireLocators` | true | Allow proven `require(ModulePath)` export paths |
| `filter-line` | `FilterLineField` | `Line` | `Line` or `StartLine`; file/API only, bridge supplies detected metadata |
| `module-path` | `ModulePath` | empty | Single-line ASCII Instance expression, ≤8,192 bytes; file/API only |
| `optimization-passes` | `OptimizationPasses` | 5 | 0–8 statement simplification passes; 0 retains noisier lowering |
| `state-machine` | `ForceStateMachine` | false | Force explicit program-counter control flow |
| `fallback` | `AllowFallback` | true | Permit state-machine output when structuring cannot safely finish |
| `max-input-bytes` | `MaxInputBytes` | 67,108,864 | 1–268,435,456 bytes |
| `max-output-bytes` | `MaxOutputBytes` | 67,108,864 | 1–268,435,456 bytes |
| `max-analysis-cells` | `MaxAnalysisCells` | 16,000,000 | 1–64,000,000 instruction × register cells per function |
| `max-analysis-work` | `MaxAnalysisWork` | 32,000,000 | 1–256,000,000 graph/dataflow/region work units per request |
| `max-nesting` | `MaxNesting` | 128 | Maximum closure depth, 1–128; root is depth 0 |

Unknown keys, invalid enums, partial numbers such as `2junk`, numeric overflow, and invalid Boolean text are errors. Boolean text must be `true` or `false`. Forced state machines with fallback disabled are rejected. `ValidateOptions` also validates settings constructed directly in C++.

The legacy `--no-filtergc` switch still disables **both** locator kinds for compatibility. Prefer `--no-gc-locators` to disable only GC comments, or `--no-locators` to disable both.

## Naming rules

- **Inferred:** prefer debug names, then useful service/module/member hints, then typed or generic numbered names.
- **Debug:** use available debug local/function names and numbered fallbacks; do not expose inferred service/member names as binding names.
- **Generic:** number all local bindings and parameters. This overrides original function-name preservation. `type-names=false` makes anonymous local prefixes uniformly Value/value; parameters use Argument/argument.
- **Preserve casing:** retain valid debug/inferred spelling, including underscores. Invalid or erased identifiers still need sanitized/generated names.

Names are allocated after simplification, in deterministic first-reference order. Global names and enclosing bindings are reserved; collisions receive numeric suffixes. Snake casing recognizes acronym boundaries (`HTTPServer` → `http_server`), camel casing gives `httpServer`, and reserved words receive a suffix. Original function names remain unchanged unless `preserve-function-names=false` or generic naming is selected.

## Comment rules

`lines=false` suppresses the entire function line/lookup comment. `locators=false` retains line numbers while hiding lookups. When require lookups are enabled and a reachable, unambiguous export is proven, that lookup takes priority. Otherwise GC is used if enabled. With GC disabled, unresolved functions have only their line number. No dangling separator is emitted.

Export analysis is skipped when its output is disabled. It never requires or executes a module. Static export paths can follow nested tables, numeric/quoted keys, directly returned functions, and recognizable constructors/metatables; conflicting writes/returns or unknown mutations prevent a claimed path.

GC lookups use serialized function names, never generated binding names. If both name and line are present, those filters are sufficient for the emitted expression. Otherwise at most five representable constant-pool entries are included. Missing lines display `Unknown`; nil/NaN, table templates, and closure templates are excluded from constants. Lookups depend on executor support and a matching live closure; they are not uniqueness proofs.

## Live bridge configuration

Start the local server, then execute the **local** `tools/Decompile.luau` (bridge v5). The previously published release loader remains pinned to its published version.

```luau
getgenv().TazeConfig = {
    Url = "ws://127.0.0.1:8877/decompile",
    Timeout = 30,
    MaxPending = 32,
    ErrorMode = "silent", -- or "raise"
    Options = {
        casing = "camel",
        naming = "inferred",
        ["gc-locators"] = false,
        ["require-locators"] = true,
        ["preserve-function-names"] = false,
    },
}
-- Execute tools/Decompile.luau after setting installation options.
```

```luau
local Camera = game:GetService("StarterPlayer"):FindFirstChild("CameraModule", true)
local Source = decompile(Camera)
local Snake = decompile(Camera, { casing = "snake", lines = false })
local Generic = decompile(Camera, 60, { naming = "generic", ["type-names"] = false })
print(getgenv().TazeBridge.LastRequest)
```

Per-call options overlay `TazeConfig.Options` without changing it. Server CLI options are the base; only supplied request options override them. Resource limits can be reduced per request but never raised above server limits. The bridge has its own fixed 64 MiB bytecode cap. `module-path` and `filter-line` are reserved in option tables; the bridge supplies validated path metadata and probes the executor line field. Probing is skipped when GC lookups are disabled in defaults, and happens lazily if a later call enables them.

`Url`, `Timeout`, and `MaxPending` are captured at installation. `Options` and `ErrorMode` are read from that configuration table on each call. Edit the existing table to update them; reinstall after replacing the whole configuration table. `MaxPending` is 1–128. A timeout stops waiting and releases the pending slot; it does not cancel C++ work already running.

Failures return nil by default. Inspect `TazeBridge.LastError` and `LastRequest.Succeeded/Error/ElapsedSeconds`; successful calls clear LastError and also report input/output sizes, resolved options, module-path status, GC/require counts, and line field. These fields describe the last completed call, so concurrent callers should use their own returned source. `ErrorMode="raise"` makes failures raise instead. Installation failures still return nil without replacing the original decompiler.

`TazeBridge.Decompile` is a direct entry point. Reinstallation restores old hooks before installing replacements; `TazeBridge.Stop()` disconnects and restores supported cached executor references and the original global. The server processes each socket's requests in order and serves up to eight clients concurrently. A slow request on one socket can delay that socket's later requests.

## CLI and batch controls

`--serve --port N` starts the Windows loopback server. `--disassemble` prints decoded instructions. `--stats` adds decode/reconstruction/finalization timings, instruction/symbol counts, analysis work, and fallback count to stderr for single-file decompilation. It never changes source output.

```powershell
./build/Release/taze.exe input.luac --casing snake --no-preserve-function-names -o output.luau
./build/Release/taze.exe input.luac --no-gc-locators --module-path 'game.Modules.Example' --stats
./build/Release/taze.exe --batch --jobs 4 --output-dir artifacts/output a.luac b.luac
```

Batch worker count is 1 by default, 0 selects up to eight hardware threads, and explicit counts are 1–32. Results and error reporting follow input order. Invalid inputs do not prevent valid files from being written; the process exits nonzero if any fail. Filename collisions and input/output aliases are rejected before creating the output directory. Existing files require `--overwrite`. The CLI retains at most one worker-sized window of inputs/results. Batch mode cannot combine with server mode, disassembly, `--stats`, or `-o`.

The C++ `DecompileBatch(span<string_view>, Settings, Workers)` is synchronous and returns ordered `BatchResult` entries, each with `Output`, `Failure`, and `Succeeded()`. Input storage must remain alive until it returns. Shared invalid settings throw before processing; individual decompilation failures are contained in their entries. The API retains all returned results, unlike the CLI's windowed processing.

Limits constrain particular resources, not total process RSS or a wall-clock deadline. Worker count multiplies active request memory. Graph/dataflow work accounting is deterministic but is not a CPU instruction count or a timeout for every transformation.
