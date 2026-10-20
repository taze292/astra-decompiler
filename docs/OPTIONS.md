# Options reference

The bridge, CLI, and C++ library share the same output settings. This page lists every option; start with [customization](CUSTOMIZATION.md) for common examples.

## Shared options

Use PascalCase fields in Luau and C++, such as `{ Casing = "Snake" }`. CLI flags use kebab-case, such as `--casing snake`.

Boolean flags accept `true` or `false`; `--no-key` disables an option. Enum values are case-insensitive. Names and prefixes are case-sensitive.

### Naming

| CLI key | Luau / C++ field | Default | Values / meaning |
| --- | --- | --- | --- |
| `casing` | `Casing` | `Pascal` | `pascal`, `camel`, `snake`, `preserve` |
| `naming` | `Naming` | `Inferred` | `inferred`, `debug`, `generic` |
| `type-names` | `UseTypeNames` | true | Use TableN, NumberN, etc. for anonymous values with an unambiguous type |
| `preserve-function-names` | `PreserveFunctionNames` | true | Keep recovered original function bindings' spelling; false applies the selected casing |
| `infer-members` | `InferMemberNames` | true | Infer local names from accessed members |
| `infer-calls` | `InferCallNames` | true | Infer service, module, constructor, Connection, and RaycastResult names |
| `local-prefix` | `LocalPrefix` | `Value` | Prefix for anonymous locals without a selected type name; ASCII identifier, 1–32 characters |
| `argument-prefix` | `ArgumentPrefix` | `Argument` | Prefix for anonymous parameters; same identifier rules |

### Layout

| CLI key | Luau / C++ field | Default | Values / meaning |
| --- | --- | --- | --- |
| `indent` | `IndentWidth` | 4 | Spaces per level, 1–16; ignored for indentation when UseTabs is enabled |
| `use-tabs` | `UseTabs` | false | Indent with one tab per level |
| `blank-lines-between-groups` | `BlankLinesBetweenGroups` | 1 | Blank lines between statement groups, 0–3; GroupSpacing=false suppresses them |
| `space-inside-table-braces` | `SpaceInsideTableBraces` | true | Inline table padding: `{ Value }` versus `{Value}`; strings/comments are untouched |
| `compact-arrays` | `CompactArrays` | true | Omit proven contiguous numeric prefix keys; retain gaps, nils, overlaps, and unsafe keys |
| `group-spacing` | `GroupSpacing` | true | Separate statement groups with blank lines |

### Comments and locators

| CLI key | Luau / C++ field | Default | Values / meaning |
| --- | --- | --- | --- |
| `header` | `IncludeHeader` | true | Two-line generator/version and measured elapsed-time header |
| `lines` | `IncludeLineComments` | true | Function line comments, including their lookup expressions |
| `upvalues` | `IncludeUpvalueComments` | true | Capture comments with lowercase copy/ref kinds |
| `compact-upvalues` | `CompactUpvalues` | false | One-line capture list; default is a block comment with an Upvalues heading and indexed copy/ref rows |
| `upvalue-indexes` | `IncludeUpvalueIndexes` | true | Show 1-based capture indexes in either comment style |
| `locators` | `IncludeFunctionLocators` | true | Master switch for both kinds of function lookup |
| `gc-locators` | `IncludeGcLocators` | true | Allow `filtergc(...)` lookups |
| `require-locators` | `IncludeRequireLocators` | true | Allow proven `require(ModulePath)` export paths |
| `filter-line` | `FilterLineField` | `Line` | `Line` or `StartLine`; file/API only, bridge supplies detected metadata |
| `module-path` | `ModulePath` | empty | Single-line ASCII Instance expression, ≤8,192 bytes; file/API only |

### Reconstruction

| CLI key | Luau / C++ field | Default | Values / meaning |
| --- | --- | --- | --- |
| `encoding` | `Encoding` | `Auto` | `auto`, `plain`, `roblox`; validate the whole instruction stream |
| `method-syntax` | `MethodSyntax` | true | Recover `function Table:Method(...)` with implicit lowercase `self` |
| `native-captures` | `NativeCaptures` | true | Use native reference locals when lifetime analysis proves safe; false retains boxes |
| `optimization-passes` | `OptimizationPasses` | 5 | Maximum 0–8 simplification passes; stop earlier on convergence; 0 retains noisier lowering |
| `state-machine` | `ForceStateMachine` | false | Force explicit program-counter control flow for diagnostics; requires AllowFallback=true |
| `fallback` | `AllowFallback` | false | Opt in to state-machine output when structuring cannot safely finish |

### Processing limits

| CLI key | Luau / C++ field | Default | Values / meaning |
| --- | --- | --- | --- |
| `max-input-bytes` | `MaxInputBytes` | 67,108,864 | 1–268,435,456 bytes |
| `max-output-bytes` | `MaxOutputBytes` | 67,108,864 | 1–268,435,456 bytes |
| `max-analysis-cells` | `MaxAnalysisCells` | 16,000,000 | 1–64,000,000 instruction × register cells per function |
| `max-analysis-work` | `MaxAnalysisWork` | 32,000,000 | 1–256,000,000 graph/dataflow/region work units per request |
| `max-nesting` | `MaxNesting` | 128 | Maximum closure depth, 1–128; root is depth 0 |

Unknown keys, invalid values, and exceeded limits return errors. Missing fields keep their defaults. `ValidateOptions` checks settings created directly in C++ too.

`AllowFallback=false` is the default. A reconstruction failure returns an error. For diagnostic state-machine output, explicitly enable fallback first; forced output also needs `ForceStateMachine=true`.

The legacy `--no-filtergc` flag disables both locator types. Prefer `--no-gc-locators` or `--no-locators`.

## Naming rules

- **Inferred:** use debug names, useful bytecode hints, then generated names.
- **Debug:** use debug names and numbered fallbacks.
- **Generic:** number all locals and parameters, including functions.
- **Preserve casing:** retain valid recovered spelling.

`UseTypeNames=false` uses the configured local prefix instead of names such as Number1. `PreserveFunctionNames=false` lets the chosen casing change recovered function names.

Snake casing turns `HTTPServer` into `http_server`; camel casing gives `httpServer`. Collisions and reserved words receive suffixes. Globals and member keys are unchanged.

Colon methods keep Luau's required lowercase `self`. Set `MethodSyntax=false` to name an explicit receiver normally. Unsafe method conversions are skipped.

## Array and spacing rules

`CompactArrays=true` turns a safe prefix such as `{ [1] = "hi", [2] = "bye", [4] = "later" }` into `{ "hi", "bye", [4] = "later" }`. Gaps, nils, overlapping keys, and unsafe computed values keep explicit keys. Values are not reordered.

`UseTabs` selects tabs; otherwise `IndentWidth` controls spaces. `GroupSpacing` and `BlankLinesBetweenGroups` control blank lines. `SpaceInsideTableBraces` controls inline table padding. Strings and comments are unchanged.

## Comment rules

The default header contains the bytecode version and core decompilation time:

```luau
-- Decompiled with Astra | Bytecode Version: 14
-- Time taken: 0.001021s
```

Timing excludes transport and Roblox scheduling. `IncludeHeader=false` removes the header for deterministic file comparisons.

Captures use a block comment by default:

```luau
--[[
    Upvalues
    [1] Uv1 (copy)
    [2] Uv2 (ref)
]]--
```

`CompactUpvalues` makes this one line. `IncludeUpvalueIndexes` controls indexes in either style. `IncludeUpvalueComments=false` hides the list.

`IncludeLineComments=false` hides the entire function line/lookup comment. `IncludeFunctionLocators=false` hides lookups while keeping line numbers.

Require locators need a valid module path and a proven export. Otherwise GC locators are used when enabled. GC lookups use serialized names and up to five representable constants when name/line metadata is incomplete. They depend on executor support and a matching live closure.

Recovered inlined functions receive `-- Uninlined by Astra` instead of their own line, locator, and capture comments. Nested ordinary functions keep their settings.

Locators are comments. Export analysis never requires or executes the target module.

## Live bridge configuration

Set connection settings before executing the release's matching `Decompile.luau`:

```luau
getgenv().AstraConfig = {
    Url = "ws://127.0.0.1:8877/decompile",
    Timeout = 30,
    MaxPending = 32,
    Concurrency = 8,
    ErrorMode = "silent",
}
```

| Field | Default | Meaning |
| --- | --- | --- |
| `Url` | `ws://127.0.0.1:8877/decompile` | Local server address |
| `Timeout` | 30 | Seconds to wait per call |
| `MaxPending` | 32 | Active-call limit, 1–128 |
| `MaxQueued` | 16,384 | Waiting-call limit, 0–65,536; 0 rejects calls when full |
| `Concurrency` | min(8, MaxPending) | Worker count for bulk helpers, 1–MaxPending |
| `BinaryTransport` | true | Binary ASTRA5 requests; false uses text ASTRA4 |
| `CacheBytes` | 33,554,432 | Bytecode/source cache budget, 0–256 MiB; 0 disables it |
| `ErrorMode` | `silent` | Return nil on error; `raise` raises it |
| `Options` | empty table | Output overrides applied after AstraOptions |

Reinstall to change connection, queue, transport, concurrency, timeout, or cache settings. `Options` and `ErrorMode` are read for each call.

### Requests and overrides

```luau
local Source = decompile(SomeScript)
local Snake = decompile(SomeScript, { Casing = "Snake" })
local LongerWait = decompile(SomeScript, 60, {
    IncludeHeader = false,
})
```

Each call applies internal defaults, `AstraOptions`, `AstraConfig.Options`, then its overrides. Input tables are not modified. The bridge supplies `ModulePath` and `FilterLineField` automatically.

A timeout stops waiting, but does not cancel C++ work already running. Per-request resource limits can be reduced, but cannot exceed server caps.

Volt's native `DecompilerOptions` are accepted for integrations. Supported fields are translated; `LastRequest.IgnoredNativeOptions` lists settings without an equivalent. Astra option tables use strict validation.

### Errors and status

```luau
local Bridge = getgenv().AstraBridge
local Error = Bridge.LastError
local Details = Bridge.LastRequest
```

The bridge is silent by default. Failed requests return nil; the direct `Bridge.Decompile` entry point also returns the error as a second value. Successful requests clear `LastError`.

`LastRequest` includes success/error, elapsed time, sizes, resolved options, locator metadata, cache-hit status, and protocol. `Stats` tracks requests, successes, failures, cache hits, and transferred bytes.

Status describes the last completed call. Concurrent callers should use their own returned source.

### Bulk helpers and cleanup

| Method | Result |
| --- | --- |
| `Bridge.DecompileMany(Targets, Timeout, Overrides)` | Ordered `{ Source, Error }` records |
| `Bridge.PrepareSave(Roots, Timeout, Overrides)` | Result records plus the inventoried targets |
| `Bridge.ClearCache()` | Releases cached bytecode and source |
| `Bridge.Stop()` | Disconnects, wakes waiting calls, clears the cache, and restores the previous decompiler |

`PrepareSave` accepts an Instance or an array of roots. It skips server-context Scripts. Requests may finish out of order; bulk results retain input order.

## CLI and batch controls

```powershell
.\astra.exe input.luac --casing snake --no-preserve-function-names -o output.luau
.\astra.exe input.luac --no-header --stats -o output.luau
.\astra.exe --batch --jobs 4 --output-dir output a.luac b.luac
```

`--serve --port N` starts the Windows server. `--disassemble` prints instructions. `--stats` writes timings, counts, analysis work, and fallback counts to stderr without changing generated source.

Batch mode uses one worker by default. `--jobs 0` selects up to eight hardware threads; explicit counts are 1–32. Input/output aliases and filename collisions are rejected. Existing outputs need `--overwrite`.

A failed file does not discard successful outputs. The CLI exits nonzero if any input fails. Batch mode cannot combine with server mode, disassembly, `--stats`, or `-o`.

The C++ `DecompileBatch` API returns ordered `BatchResult` entries with `Output`, `Failure`, and `Succeeded()`. Input storage must stay alive until the call returns. Invalid shared settings throw before processing; individual failures stay in their result entries.

Limits cover specific resources, not total process memory or a wall-clock deadline. More workers increase active-request memory.
