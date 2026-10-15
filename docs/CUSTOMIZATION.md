# Customization

The vertical `DefaultOptions` table in [Decompile.luau](../tools/Decompile.luau) lists every setting. Edit that table, set `getgenv().AstraOptions` before installation, or replace the global table between requests.

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

Astra starts with internal defaults, applies `AstraOptions`, then `AstraConfig.Options`, then per-call overrides. Missing fields retain their defaults. Input tables are not mutated. Unknown settings and invalid values produce an error; they are not treated as missing.

## Names

| Setting | Choices | Default |
| --- | --- | --- |
| `Casing` | `Pascal`, `Camel`, `Snake`, `Preserve` | `Pascal` |
| `Naming` | `Inferred`, `Debug`, `Generic` | `Inferred` |
| `PreserveFunctionNames` | Keep recovered function spellings | `true` |
| `UseTypeNames` | Prefer names such as Table1 over Value1 | `true` |
| `LocalPrefix` | Anonymous local prefix | `Value` |
| `ArgumentPrefix` | Anonymous parameter prefix | `Argument` |

Inferred names use available debug names and useful service, member, or constructor hints. Globals, member keys, strings, and locator metadata keep their original identity. `MethodSyntax=true` recovers methods with implicit lowercase `self` where safe.

## Comments

```luau
getgenv().AstraOptions = {
    IncludeHeader = true,
    IncludeLineComments = true,
    IncludeUpvalueComments = true,
    IncludeUpvalueIndexes = true,
    CompactUpvalues = false,
}
```

The default upvalue style is a block comment:

```luau
--[[
    Upvalues
    [1] Uv1 (copy)
    [2] Uv2 (ref)
]]--
```

`CompactUpvalues=true` makes the list one line. Indexes are independent in both styles. Recovered compiler-inlined functions receive only `-- Uninlined by Astra` as their own metadata comment.

The header is:

```luau
-- Decompiled with Astra | Bytecode Version: 14
-- Time taken: 0.004497s
```

Disable `IncludeHeader` for deterministic full-file comparisons.

## Function locators

```luau
getgenv().AstraOptions = {
    IncludeFunctionLocators = true,
    IncludeRequireLocators = false,
    IncludeGcLocators = true,
}
```

Require locators need a valid module path and a proven export. If that information is missing, Astra uses GC locators when enabled. Disabling require locators also permits GC locators for functions that could otherwise use `require`. Set `IncludeFunctionLocators=false` to hide both kinds while keeping line comments.

Locators are comments to help find a live function; they do not execute during decompilation. Missing or ambiguous metadata does not produce an invented require path.

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

`CompactArrays` shortens a proven array prefix:

```luau
-- CompactArrays = false
local Words = { [1] = "hi", [2] = "bye", [4] = "later" }

-- CompactArrays = true
local Words = { "hi", "bye", [4] = "later" }
```

Gaps, nils, dynamic keys, duplicate keys, and call/vararg results keep explicit keys wherever shortening cannot be proven equivalent. Values are not reordered.

`UseTabs` overrides space indentation. `BlankLinesBetweenGroups` accepts 0–3; `GroupSpacing=false` removes group separation. `SpaceInsideTableBraces` selects `{ Value }` or `{Value}` without changing strings or comments.

## Connection settings

Set these before installing the bridge:

```luau
getgenv().AstraConfig = {
    Url = "ws://127.0.0.1:8877/decompile",
    Timeout = 30,
    MaxPending = 32,
    ErrorMode = "silent",
}
```

Reinstall to change connection settings. `ErrorMode="raise"` raises request errors instead of returning nil. See the [complete reference](OPTIONS.md) for every default, accepted value, resource limit, and advanced option.

## Reconstruction failures

`AllowFallback=false` is the default. Astra reconstructs structured control flow and reports an error if it cannot finish safely; it does not silently replace the function with a generated state machine. Developers can set `AllowFallback=true` to inspect diagnostic fallback output. `ForceStateMachine=true` requires that explicit opt-in too.
