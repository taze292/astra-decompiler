# Move to Astra

The decompiler is now called **Astra**. Rebuild from this checkout and install the current `tools/Decompile.luau` to use its readable options, array formatting, spacing controls, and updated comments. Older release executables and pinned loaders are historical artifacts; they do not acquire new behavior when the repository is renamed.

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

The installation script contains the full vertical defaults table, grouped by purpose. Change the live `AstraOptions` table to affect the next call. In `AstraConfig`, `Url`, `Timeout`, and `MaxPending` are captured at installation and require reinstallation to change; `ErrorMode` and `Options` are read again for each call. Settings from `AstraConfig.Options` override `AstraOptions`, and per-call options take final priority. See [the complete option reference](OPTIONS.md) for types, limits, aliases, and locator precedence.

The CLI continues to use readable kebab-case flags, such as `--compact-arrays true` and `--blank-lines-between-groups 2`. Boolean options need an explicit `true`/`false` value, or the corresponding `--no-key` form; the documented `--state-machine` switch is an exception. C++ uses `Astra::Options` through `<Astra/Decompiler.hpp>`.

## Output changes

The default header is:

```luau
-- Decompiled with Astra | Bytecode Version: 14
-- Time taken: 0.004497s
```

Default upvalue annotations use valid Luau line comments, lowercase capture kinds, and independent indexes:

```luau
-- Upvalues
-- [1] RunService (copy)
-- [2] Elapsed (ref)
```

`IncludeUpvalueIndexes = false` removes the indexes; `CompactUpvalues = true` selects the compact presentation. Recovered compiler-inlined functions retain only their Astra uninlining provenance comment as their own annotation.

With `CompactArrays = true`, proven consecutive array fields use implicit list entries. Gaps, unsafe writes, and expression evaluation order still constrain reconstruction; the formatter does not reorder table writes to manufacture an array. `CompactArrays = false` keeps explicit numeric indexes where the constructor is reconstructed from indexed writes.

```luau
local Greetings = { "hi", "bye", [4] = "later" }
```

Formatting settings control indentation, blank lines between statement groups, and table-brace padding. They do not change field names, strings, runtime values, or capture identities.

## GitHub repository and Pages

Rename the repository when ready, update the local remote, and choose **GitHub Actions** in **Settings → Pages**. The prepared workflow publishes documentation directly from the default branch. Follow [the Pages setup guide](GITHUB_PAGES.md) for exact commands and the expected URL.
