# Introduction

Astra turns Luau bytecode into readable source. Run it locally through a Roblox executor, use the CLI for files, or integrate the C++ library.

## Start here

- [Set up Astra](GETTING_STARTED.md): build, start the server, and install the bridge.
- [Customize output](CUSTOMIZATION.md): names, comments, arrays, and spacing.
- [Choose a setup](SETUPS.md): Roblox, individual files, batches, or C++.
- [Browse every option](OPTIONS.md): defaults, accepted values, and limits.

## A local workflow

Start `astra.exe --serve`, execute `tools/Decompile.luau`, then call `decompile(SomeScript)`. The bridge sends bytecode to your loopback server and returns generated Luau. Neither the bridge nor the server executes the decompiled source.

```luau
local Source = decompile(SomeScript, {
    Casing = "Pascal",
    IncludeHeader = true,
    CompactArrays = true,
})
```

## Make it yours

Options live in `getgenv().AstraOptions`. Edit the readable table in the bridge or replace it between requests. Each call can override individual settings, and omitted settings use the internal defaults.

See the [customization guide](CUSTOMIZATION.md) for examples. If you are upgrading from an older bridge, read the [migration notes](ASTRA_MIGRATION.md).

## Working on Astra

The [developer guide](../dev/README.md) explains the source tree and test commands. The [architecture](ARCHITECTURE.md) describes the pipeline, and [Astra 0.5 control-flow validation](OBFUSCATED_CONTROL_FLOW.md) records the current fixes and measurements. Earlier performance and design reports remain under Developer notes as dated snapshots.
