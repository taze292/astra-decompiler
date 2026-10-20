# Customization

Set defaults in `getgenv().AstraOptions`, then override individual settings per call. The `DefaultOptions` table in [Decompile.luau](../tools/Decompile.luau) lists every setting.

## Defaults and overrides

```luau
getgenv().AstraOptions = {
    Casing = "Pascal",
    Naming = "Inferred",
    CompactArrays = true,
    IndentWidth = 4,
}

local Source = decompile(SomeScript, {
    IncludeHeader = false,
})
```

Settings apply in this order: internal defaults → `AstraOptions` → `AstraConfig.Options` → per-call overrides. Omitted fields keep their defaults. Unknown names and invalid values return an error.

## Names

| Setting | Choices | Default |
| --- | --- | --- |
| `Casing` | `Pascal`, `Camel`, `Snake`, `Preserve` | `Pascal` |
| `Naming` | `Inferred`, `Debug`, `Generic` | `Inferred` |
| `PreserveFunctionNames` | Keep recovered function spellings | `true` |
| `UseTypeNames` | Prefer names such as Table1 over Value1 | `true` |
| `LocalPrefix` | Prefix for anonymous locals | `Value` |
| `ArgumentPrefix` | Prefix for anonymous parameters | `Argument` |

Inferred names use available debug names and hints from services, members, and constructors. They cannot recover erased names. Globals, member keys, strings, and locator metadata keep their identity.

Set `PreserveFunctionNames=false` to apply the selected casing to recovered function names. `MethodSyntax=true` uses colon methods with lowercase `self` where safe.

## Comments

```luau
getgenv().AstraOptions = {
    IncludeHeader = false,
    IncludeLineComments = true,
    IncludeUpvalueComments = true,
    IncludeUpvalueIndexes = true,
    CompactUpvalues = false,
}
```

- `IncludeHeader` shows the bytecode version and measured decompilation time. Disable it for deterministic file comparisons.
- `IncludeLineComments` shows function line and lookup comments.
- `IncludeUpvalueComments` shows captured variables.
- `CompactUpvalues` puts the capture list on one line.
- `IncludeUpvalueIndexes` shows indexes in either capture style.

The default capture list looks like this:

```luau
--[[
    Upvalues
    [1] Uv1 (copy)
    [2] Uv2 (ref)
]]--
```

## Function locators

```luau
getgenv().AstraOptions = {
    IncludeFunctionLocators = true,
    IncludeRequireLocators = false,
    IncludeGcLocators = true,
}
```

Locators are comments that help find a matching live function. They do not execute during decompilation.

Require locators need a valid module path and a proven export. Otherwise Astra uses GC locators when enabled. Set `IncludeFunctionLocators=false` to hide both kinds while keeping line comments.

## Arrays and spacing

```luau
getgenv().AstraOptions = {
    CompactArrays = true,
    UseTabs = false,
    IndentWidth = 4,
    GroupSpacing = true,
    BlankLinesBetweenGroups = 1,
    SpaceInsideTableBraces = true,
}
```

`CompactArrays` removes safe, consecutive array keys:

```luau
-- CompactArrays = false
local Words = { [1] = "hi", [2] = "bye", [4] = "later" }

-- CompactArrays = true
local Words = { "hi", "bye", [4] = "later" }
```

Unsafe keys remain explicit; values are never reordered.

`UseTabs` overrides space indentation. `BlankLinesBetweenGroups` accepts 0–3; `GroupSpacing=false` removes group separation. `SpaceInsideTableBraces` chooses between `{ Value }` and `{Value}`.

## Connection settings

Connection settings belong in `AstraConfig` before installing the bridge:

```luau
getgenv().AstraConfig = {
    Url = "ws://127.0.0.1:8877/decompile",
    Timeout = 30,
    ErrorMode = "silent",
}
```

Reinstall after changing connection settings. Set `ErrorMode="raise"` to raise request errors instead of returning nil. See the [full reference](OPTIONS.md#live-bridge-configuration) for queue, cache, and concurrency controls.
