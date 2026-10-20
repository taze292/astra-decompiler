![Astra star logo](site/assets/astra.svg)

# Astra

**[Download](https://github.com/taze292/astra-decompiler/releases/latest)** · [Documentation](https://taze292.github.io/astra-decompiler/docs.html) · [Join Discord](https://discord.gg/cyjC3QejpG)

A fast C++20 decompiler for Luau bytecode. Use Astra through a local Roblox bridge, the command line, or a C++ library. Output supports inferred names, native captures, method syntax, compact arrays, and configurable comments and spacing.

## Set up

1. Download the **Windows x64 ZIP** from the [latest release](https://github.com/taze292/astra-decompiler/releases/latest) and extract it.
2. Run `Start-Decompiler.cmd` and leave the server open.
3. Execute the included `Decompile.luau` in your Roblox executor.
4. Call `decompile`:

```luau
local Source = decompile(SomeScript)
```

No build tools are needed. Your executor needs `getscriptbytecode` and `WebSocket.connect` or `websocket.connect`. See [installation and troubleshooting](docs/GETTING_STARTED.md), or [build from source](docs/BUILDING.md).

## Customize

Edit the vertical options table in `Decompile.luau`, or change settings at any time:

```luau
getgenv().AstraOptions = {
    Casing = "Pascal",
    Naming = "Inferred",
    IncludeRequireLocators = false,
    IncludeGcLocators = true,
    CompactUpvalues = false,
    CompactArrays = true,
    IndentWidth = 4,
    BlankLinesBetweenGroups = 1,
}

local Source = decompile(SomeScript, {
    Casing = "Snake",
    IncludeHeader = false,
})
```

Omitted fields use Astra's internal defaults. Per-call settings override the global table. Without a valid module path, require locators are omitted; enabled GC locators are used instead. Inserted state-machine fallback is disabled by default. [Customize names, comments, and spacing →](docs/CUSTOMIZATION.md)

## Other setups

```powershell
.\astra.exe input.luac -o output.luau
.\astra.exe --batch --jobs 4 --output-dir output a.luac b.luac
```

Input is raw, decompressed serialized Luau bytecode. Original comments, formatting, and stripped names cannot be recovered exactly. See [CLI, batch, and C++ setups](docs/SETUPS.md), the [complete options reference](docs/OPTIONS.md), and [compatibility notes](docs/SETUPS.md#compatibility).

## Contribute

See [build from source](docs/BUILDING.md) and the [developer guide](dev/README.md). Run `./build.ps1 -Test` before submitting changes. Licenses are listed in [third-party notices](THIRD_PARTY_NOTICES.md).
