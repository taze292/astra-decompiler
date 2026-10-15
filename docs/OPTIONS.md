# Configuration reference

The C++ API, CLI, and bridge use the same option parser and validation rules. Defaults preserve the original PascalCase output. Configuration changes local bindings and generated comments; it never renames globals, methods, table keys, string literals, or debug names used to identify live functions.

## Shared options

The Luau bridge uses the readable PascalCase names in the second column, for example `{ Casing = "Snake", IncludeGcLocators = false }`. C++ callers can set those fields directly or call `Astra::ApplyOption(Options, "Casing", "Snake")`. The backend also accepts the CLI names in the first column; aliases resolve before duplicate, reserved-field, and resource-cap validation. The bridge deliberately exposes only the PascalCase schema.

CLI syntax is `--key value`. Boolean switches also support `--no-key`; `--state-machine` alone means true. Enum values are case-insensitive, and `PascalCase`, `camelCase`, and `snake_case` are accepted casing aliases. Names and prefixes remain case-sensitive.

| CLI key | Luau / C++ field | Default | Values / meaning |
| --- | --- | --- | --- |
| `encoding` | `Encoding` | `Auto` | `auto`, `plain`, `roblox`; validate the whole instruction stream |
| `casing` | `Casing` | `Pascal` | `pascal`, `camel`, `snake`, `preserve` |
| `naming` | `Naming` | `Inferred` | `inferred`, `debug`, `generic` |
| `type-names` | `UseTypeNames` | true | Use TableN, NumberN, etc. for anonymous values with an unambiguous type |
| `preserve-function-names` | `PreserveFunctionNames` | true | Keep recovered original function bindings' spelling; false applies the selected casing |
| `method-syntax` | `MethodSyntax` | true | Recover `function Table:Method(...)` with implicit lowercase `self` |
| `native-captures` | `NativeCaptures` | true | Use native reference locals when lifetime analysis proves safe; false retains boxes |
| `infer-members` | `InferMemberNames` | true | Infer local names from accessed members |
| `infer-calls` | `InferCallNames` | true | Infer service, module, constructor, Connection, and RaycastResult names |
| `local-prefix` | `LocalPrefix` | `Value` | Prefix for anonymous locals without a selected type name; ASCII identifier, 1–32 characters |
| `argument-prefix` | `ArgumentPrefix` | `Argument` | Prefix for anonymous parameters; same identifier rules |
| `indent` | `IndentWidth` | 4 | Spaces per level, 1–16; ignored for indentation when UseTabs is enabled |
| `use-tabs` | `UseTabs` | false | Indent with one tab per level |
| `blank-lines-between-groups` | `BlankLinesBetweenGroups` | 1 | Blank lines between statement groups, 0–3; GroupSpacing=false suppresses them |
| `space-inside-table-braces` | `SpaceInsideTableBraces` | true | Inline table padding: `{ Value }` versus `{Value}`; strings/comments are untouched |
| `compact-arrays` | `CompactArrays` | true | Omit proven contiguous numeric prefix keys; retain gaps, nils, overlaps, and unsafe keys |
| `header` | `IncludeHeader` | true | Two-line generator/version and measured elapsed-time header |
| `lines` | `IncludeLineComments` | true | Function line comments, including their lookup expressions |
| `upvalues` | `IncludeUpvalueComments` | true | Capture comments with lowercase copy/ref kinds |
| `compact-upvalues` | `CompactUpvalues` | false | One-line capture list; default is a block comment with an Upvalues heading and indexed copy/ref rows |
| `upvalue-indexes` | `IncludeUpvalueIndexes` | true | Show 1-based capture indexes in either comment style |
| `group-spacing` | `GroupSpacing` | true | Separate statement groups with blank lines |
| `locators` | `IncludeFunctionLocators` | true | Master switch for both kinds of function lookup |
| `gc-locators` | `IncludeGcLocators` | true | Allow `filtergc(...)` lookups |
| `require-locators` | `IncludeRequireLocators` | true | Allow proven `require(ModulePath)` export paths |
| `filter-line` | `FilterLineField` | `Line` | `Line` or `StartLine`; file/API only, bridge supplies detected metadata |
| `module-path` | `ModulePath` | empty | Single-line ASCII Instance expression, ≤8,192 bytes; file/API only |
| `optimization-passes` | `OptimizationPasses` | 5 | Maximum 0–8 simplification passes; stop earlier on convergence; 0 retains noisier lowering |
| `state-machine` | `ForceStateMachine` | false | Force explicit program-counter control flow for diagnostics; requires AllowFallback=true |
| `fallback` | `AllowFallback` | false | Opt in to state-machine output when structuring cannot safely finish |
| `max-input-bytes` | `MaxInputBytes` | 67,108,864 | 1–268,435,456 bytes |
| `max-output-bytes` | `MaxOutputBytes` | 67,108,864 | 1–268,435,456 bytes |
| `max-analysis-cells` | `MaxAnalysisCells` | 16,000,000 | 1–64,000,000 instruction × register cells per function |
| `max-analysis-work` | `MaxAnalysisWork` | 32,000,000 | 1–256,000,000 graph/dataflow/region work units per request |
| `max-nesting` | `MaxNesting` | 128 | Maximum closure depth, 1–128; root is depth 0 |

Unknown keys, invalid enums, partial numbers such as `2junk`, numeric overflow, and invalid Boolean text are errors. Boolean text must be `true` or `false`. Forced state machines with fallback disabled are rejected. `ValidateOptions` also validates settings constructed directly in C++.

Astra 0.5 defaults to structured reconstruction with `AllowFallback=false` in both the bridge and C++ API. If structuring cannot finish safely, the request fails rather than inserting a dispatcher. State-machine output is an explicit diagnostic opt-in: enable `AllowFallback` first, then optionally `ForceStateMachine` to force that output. On the CLI, use `--fallback true --state-machine` for the forced form.

The legacy `--no-filtergc` switch still disables **both** locator kinds for compatibility. Prefer `--no-gc-locators` to disable only GC comments, or `--no-locators` to disable both.

## Array and spacing rules

With `CompactArrays=true`, a safe explicit prefix such as `{ [1] = "hi", [2] = "bye", [4] = "later" }` becomes `{ "hi", "bye", [4] = "later" }`. The transformation preserves source field order and stops at gaps or nils. Ambiguous computed keys, overlapping duplicates, and call/vararg expansion need additional checks; the decompiler keeps explicit keys wherever equivalence cannot be proved. Set CompactArrays=false for fully explicit reconstructed indices.

UseTabs selects tabs; otherwise IndentWidth supplies 1–16 spaces per level. GroupSpacing and BlankLinesBetweenGroups control statement-group separation. SpaceInsideTableBraces affects inline constructor padding. These controls never rewrite the contents of strings or comments and do not rename table keys.

## Naming rules

- **Inferred:** prefer debug names, then useful service/module/member hints, then typed or generic numbered names.
- **Debug:** use available debug local/function names and numbered fallbacks; do not expose inferred service/member names as binding names.
- **Generic:** number all local bindings and parameters. This overrides original function-name preservation. `UseTypeNames=false` makes anonymous local prefixes uniformly Value/value; parameters use Argument/argument.
- **Preserve casing:** retain valid debug/inferred spelling, including underscores. Invalid or erased identifiers still need sanitized/generated names.

Names are allocated after simplification, in deterministic first-reference order. Global names and enclosing bindings are reserved; collisions receive numeric suffixes. Snake casing recognizes acronym boundaries (`HTTPServer` → `http_server`), camel casing gives `httpServer`, and reserved words receive a suffix. Original function names remain unchanged unless `PreserveFunctionNames=false` or generic naming is selected.

Implicit method receivers always use Luau's required spelling `self`, including in generic naming mode. Set `MethodSyntax=false` to apply ordinary naming to explicit receivers. Method conversion is suppressed where it could shadow a captured outer receiver or a global `self`. Inference does not recover erased source names; it supplies hints from bytecode operations. For example, `Instance.new("Part")` suggests `Part`, event `Connect` suggests `Connection`, and raycasts suggest `RaycastResult`.

## Comment rules

Upvalues use a block comment by default:

```luau
--[[
    Upvalues
    [1] Uv1 (copy)
    [2] Uv2 (ref)
]]--
```

`CompactUpvalues=true` uses a one-line list. `IncludeUpvalueIndexes` controls bracketed numbering independently in both styles; `IncludeUpvalueComments=false` hides the entire list. For example, `{ CompactUpvalues = true, IncludeUpvalueIndexes = false }` produces a compact unnumbered list. Existing `AstraOptions` tables are preserved on reinstallation.

`IncludeLineComments=false` suppresses the entire function line/lookup comment. `IncludeFunctionLocators=false` retains line numbers while hiding lookups. When `IncludeRequireLocators=true`, a valid ModulePath is available, and a reachable, unambiguous export is proven, that lookup takes priority. Missing game paths or disabled require locators use GC instead if `IncludeGcLocators=true`. With GC disabled, unresolved functions have only their line number. No dangling separator is emitted.

A function reconstructed through a retained compiler inline guard has only `-- Uninlined by Astra` as its own metadata comment. Its line, lookup, and upvalue annotations are suppressed regardless of these presentation options. This provenance marker remains with `IncludeLineComments=false` and `IncludeUpvalueComments=false`. Ordinary functions inside that body retain their own settings. See [INLINE_RECOVERY.md](INLINE_RECOVERY.md).

Export analysis is skipped when its output is disabled. It never requires or executes a module. Static export paths can follow nested tables, numeric/quoted keys, directly returned functions, and recognizable constructors/metatables; conflicting writes/returns or unknown mutations prevent a claimed path.

GC lookups use serialized function names, never generated binding names. If both name and line are present, those filters are sufficient for the emitted expression. Otherwise at most five representable constant-pool entries are included. Missing lines display `Unknown`; nil/NaN, table templates, and closure templates are excluded from constants. Lookups depend on executor support and a matching live closure; they are not uniqueness proofs.

## Live bridge configuration

Start the local server, then execute the **local** `tools/Decompile.luau` (bridge v8). The optional download loader follows the Astra repository's `main` branch. Installation and requests do not print or warn. The bridge follows [Volt's WebSocket API](https://docs.voltbz.net/docs/websocket), including binary-message flags and explicit closure of discarded clients.

```luau
getgenv().AstraConfig = {
    Url = "ws://127.0.0.1:8877/decompile",
    Timeout = 30,
    MaxPending = 32,
    ErrorMode = "silent", -- or "raise"
}
getgenv().AstraOptions = {
    Casing = "Pascal",
    Naming = "Inferred",
    IncludeRequireLocators = false,
    IncludeGcLocators = true,
    MethodSyntax = true,
    NativeCaptures = true,
    CompactUpvalues = false,
    IncludeUpvalueIndexes = true,
    CompactArrays = true,
    UseTabs = false,
    IndentWidth = 4,
    GroupSpacing = true,
    BlankLinesBetweenGroups = 1,
    SpaceInsideTableBraces = true,
}
-- Execute tools/Decompile.luau after setting installation options.
```

```luau
local Camera = game:GetService("StarterPlayer"):FindFirstChild("CameraModule", true)
local Source = decompile(Camera)
local Snake = decompile(Camera, { Casing = "Snake", IncludeLineComments = false })
local Generic = decompile(Camera, 60, { Naming = "Generic", UseTypeNames = false })
local Details = getgenv().AstraBridge.LastRequest
```

Each call snapshots `getgenv().AstraOptions`, then overlays `AstraConfig.Options` and per-call options. Input tables are never mutated. Edit or replace `AstraOptions` between requests without reinstalling. A replacement `AstraConfig` supplies the next call's `Options` and `ErrorMode`; its transport settings still require reinstallation, as described below. Missing fields use the documented defaults. The table is serialized as a bounded JSON object in each ASTRA4 WebSocket message; the server never evaluates Luau configuration code. Resource limits can be reduced per request but never raised above server limits. The bridge also enforces the smaller of MaxInputBytes and its fixed 64 MiB bytecode cap. ModulePath and FilterLineField are reserved metadata; the bridge supplies their validated values. GC probing is skipped when disabled and performed lazily if a later request enables it.

`Url`, `Timeout`, and `MaxPending` are captured at installation; reinstall to change connection settings. `Options` and `ErrorMode` are read from the current configuration table on each call. `MaxPending` is 1–128. A timeout stops waiting and releases the pending slot; it does not cancel C++ work already running.

Module lookup metadata comes from the live instance hierarchy. The bridge uses dot access only when it resolves to the actual child, otherwise escaped `FindFirstChild` segments. It shortens three or more remaining segments to `:FindFirstChild("Name", true)` only after counting exactly one matching descendant and checking identity. Searches start within the relevant service or local player, not the whole game. Duplicate sibling names suppress require paths; duplicate descendants retain the explicit path. Nothing is required or executed during this analysis. Paths describe the hierarchy at request time.

The default header is exactly two comments followed by a blank line:

```luau
-- Decompiled with Astra | Bytecode Version: 14
-- Time taken: 0.004497s
```

Time is measured inside the C++ decompiler from decoding through finalization, in seconds; it excludes WebSocket transfer, Roblox scheduling, and header formatting. `LastRequest.ElapsedSeconds` includes bridge work. Use `IncludeHeader=false` for byte-identical output comparisons and hashes; the generated body remains deterministic.

Failures return nil by default. Inspect `AstraBridge.LastError` and `LastRequest.Succeeded/Error/ElapsedSeconds`; successful calls clear LastError and also report input/output sizes, resolved options, module-path status, GC/require counts, and line field. These fields describe the last completed call, so concurrent callers should use their own returned source. `ErrorMode="raise"` makes failures raise instead. Installation failures still return nil without replacing the original decompiler.

`AstraBridge.Decompile` is a direct entry point. Reinstallation restores old hooks before installing replacements; `AstraBridge.Stop()` disconnects and restores supported cached executor references and the original global. The server processes each socket's requests in order and serves up to eight clients concurrently. A slow request on one socket can delay that socket's later requests.

## CLI and batch controls

`--serve --port N` starts the Windows loopback server. `--disassemble` prints decoded instructions. `--stats` adds decode/reconstruction/finalization timings, instruction/symbol counts, analysis work, and fallback count to stderr for single-file decompilation. It never changes source output.

```powershell
./build/Release/astra.exe input.luac --casing snake --no-preserve-function-names -o output.luau
./build/Release/astra.exe input.luac --no-gc-locators --module-path 'game.Modules.Example' --stats
./build/Release/astra.exe --batch --jobs 4 --output-dir artifacts/output a.luac b.luac
```

Batch worker count is 1 by default, 0 selects up to eight hardware threads, and explicit counts are 1–32. Results and error reporting follow input order. Invalid inputs do not prevent valid files from being written; the process exits nonzero if any fail. Filename collisions and input/output aliases are rejected before creating the output directory. Existing files require `--overwrite`. The CLI retains at most one worker-sized window of inputs/results. Batch mode cannot combine with server mode, disassembly, `--stats`, or `-o`.

The C++ `DecompileBatch(span<string_view>, Settings, Workers)` is synchronous and returns ordered `BatchResult` entries, each with `Output`, `Failure`, and `Succeeded()`. Input storage must remain alive until it returns. Shared invalid settings throw before processing; individual decompilation failures are contained in their entries. The API retains all returned results, unlike the CLI's windowed processing.

Limits constrain particular resources, not total process RSS or a wall-clock deadline. Worker count multiplies active request memory. Graph/dataflow work accounting is deterministic but is not a CPU instruction count or a timeout for every transformation.
