![Astra](site/assets/astra.svg)

# Astra

**[Documentation](https://taze292.github.io/astra-decompiler/)** · [Getting started](docs/GETTING_STARTED.md) · [Customization](docs/CUSTOMIZATION.md) · [Options reference](docs/OPTIONS.md)

A fast C++20 decompiler for Luau bytecode. Use Astra through a local Roblox bridge, the command line, or a C++ library. Output supports inferred names, native captures, method syntax, compact arrays, and configurable comments and spacing.

## Set up

Install CMake and a C++20 toolchain. On Windows, use Visual Studio 2022 with C++ tools, the Windows SDK, and ClangCL. Put the [official Luau source](https://github.com/luau-lang/luau) in `luau/`, then build:

```powershell
./build.ps1 -Test
./build/Release/astra.exe --serve
```

Execute [tools/Decompile.luau](tools/Decompile.luau) in your executor, then call:

```luau
local Source = decompile(SomeScript)
```

The bridge needs `getscriptbytecode` and `WebSocket.connect` or `websocket.connect`. It follows [Volt's WebSocket API](https://docs.voltbz.net/docs/websocket) and runs silently. The local server must stay open. See [setup and troubleshooting](docs/GETTING_STARTED.md) for details.

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
./build/Release/astra.exe input.luac -o output.luau
./build/Release/astra.exe --batch --jobs 4 --output-dir output a.luac b.luac
```

Input is raw, decompressed serialized Luau bytecode. Original comments, formatting, and stripped names cannot be recovered exactly. See [CLI, batch, and C++ setups](docs/SETUPS.md), the [complete options reference](docs/OPTIONS.md), and [compatibility notes](docs/SETUPS.md#compatibility).

## Contribute

Run `./build.ps1 -Test` before submitting changes. The [developer guide](dev/README.md) explains the organized source tree and test tools. See the [architecture](docs/ARCHITECTURE.md), [Astra 0.5 control-flow validation](docs/OBFUSCATED_CONTROL_FLOW.md), and [third-party notices](THIRD_PARTY_NOTICES.md).

Documentation uses Markdown, a small static generator, and GitHub Pages. No npm packages or separate Pages branch are needed. [Preview or publish the docs →](docs/GITHUB_PAGES.md)
