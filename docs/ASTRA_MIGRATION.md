# Move to Astra

The current release is **Astra 0.5.1**. Rebuild from this checkout and install the current `tools/Decompile.luau` (bridge v9) for raw bytecode and Volt-options compatibility, concurrent requests, binary transport, and caching. Older release executables and pinned loaders do not acquire new behavior automatically. For a first installation, use [getting started](GETTING_STARTED.md).

## Build and install

```powershell
./build.ps1 -Test
./build/Release/astra.exe --serve
```

Stop an older local server before starting the new executable on the same port. For the bridge migration, start a fresh executor session or call the previous bridge's `Stop()` before installing Astra. Execute this checkout's `tools/Decompile.luau` after the server is available. The bridge installs the usual `decompile` entry point, and its own status is exposed through `getgenv().AstraBridge`.

## Readable options

Use PascalCase fields in `getgenv().AstraOptions`. A partial table overrides only selected defaults, and per-call settings override the current global settings:

```luau
getgenv().AstraOptions = {
    Casing = "Pascal",
    Naming = "Inferred",

    IncludeUpvalueComments = true,
    IncludeUpvalueIndexes = true,
    CompactUpvalues = false,

    IncludeRequireLocators = false,
    IncludeGcLocators = true,

    CompactArrays = true,
    IndentWidth = 4,
    UseTabs = false,
    GroupSpacing = true,
    BlankLinesBetweenGroups = 1,
    SpaceInsideTableBraces = true,
}

local Source = decompile(TargetScript, {
    Casing = "Snake",
    IncludeHeader = false,
})
```

The installation script contains the full vertical defaults table, grouped by purpose. Change the live `AstraOptions` table to affect the next call. In `AstraConfig`, connection settings, `Concurrency`, `BinaryTransport`, and `CacheBytes` are captured at installation and require reinstallation to change; `ErrorMode` and `Options` are read again for each call. Settings from `AstraConfig.Options` override `AstraOptions`, and per-call options take final priority. See [the complete option reference](OPTIONS.md) for types, limits, aliases, and locator precedence.

The CLI continues to use readable kebab-case flags, such as `--compact-arrays true` and `--blank-lines-between-groups 2`. Boolean options need an explicit `true`/`false` value, or the corresponding `--no-key` form; the documented `--state-machine` switch is an exception. C++ uses `Astra::Options` through `<Astra/Decompiler.hpp>`.

## Output changes

`AllowFallback` now defaults to `false`, matching the bridge and internal options. An unresolved structuring case returns an error instead of silently generating a state-machine dispatcher. Diagnostic fallback requires `AllowFallback=true`; forced state-machine output also requires that opt-in. See the [Astra 0.5 control-flow validation](OBFUSCATED_CONTROL_FLOW.md) for current reconstruction fixes.

Reinstallation preserves an existing `AstraOptions` table. If an older installation left `AllowFallback=true` in that table, set it to `false` or remove that field to adopt the new default.

The default header is:

```luau
-- Decompiled with Astra | Bytecode Version: 14
-- Time taken: 0.004497s
```

Default upvalue annotations use a Luau block comment, lowercase capture kinds, and independent indexes:

```luau
--[[
    Upvalues
    [1] RunService (copy)
    [2] Elapsed (ref)
]]--
```

`IncludeUpvalueIndexes = false` removes the indexes; `CompactUpvalues = true` selects the compact presentation. Recovered compiler-inlined functions retain only their Astra uninlining provenance comment as their own annotation.

With `CompactArrays = true`, proven consecutive array fields use implicit list entries. Gaps, unsafe writes, and expression evaluation order still constrain reconstruction; the formatter does not reorder table writes to manufacture an array. `CompactArrays = false` keeps explicit numeric indexes where the constructor is reconstructed from indexed writes.

```luau
local Greetings = { "hi", "bye", [4] = "later" }
```

Formatting settings control indentation, blank lines between statement groups, and table-brace padding. They do not change field names, strings, runtime values, or capture identities.

## GitHub repository and Pages

The repository is `taze292/astra-decompiler`. GitHub Actions publishes the Markdown documentation directly from the default branch to [the Astra docs](https://taze292.github.io/astra-decompiler/). For forks, local previews, and deployment troubleshooting, follow [the Pages guide](GITHUB_PAGES.md). No separate Pages branch is needed.
