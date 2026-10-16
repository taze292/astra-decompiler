# Introduction

Astra turns Luau bytecode into readable source. Download the Windows release, start the local server, and connect your executor. No compiler or build tools are needed.

## Start here

- [Install Astra](GETTING_STARTED.md): download, start the server, and install the bridge.
- [Customize output](CUSTOMIZATION.md): names, comments, arrays, and spacing.
- [Use the CLI](SETUPS.md): individual bytecode files, batches, and integrations.
- [Browse every option](OPTIONS.md): defaults, accepted values, and limits.
- [Build from source](BUILDING.md): compile Astra yourself when you need to.
- [0.5.1 reliability update](RELIABILITY_UPDATE.md): reconstruction fixes, bridge throughput, save integration, and validation.

## A local workflow

Start `Start-Decompiler.cmd` from the extracted release and execute its included `Decompile.luau` in your executor. The bridge sends bytecode to your local server and returns generated Luau. Astra does not execute the recovered source.

```luau
local Source = decompile(SomeScript, {
    Casing = "Pascal",
    IncludeHeader = true,
    CompactArrays = true,
})
```

## Make it yours

Options live in `getgenv().AstraOptions`. Edit the readable table in the bridge or replace it between requests. Each call can override individual settings, and omitted settings use the internal defaults.

See the [customization guide](CUSTOMIZATION.md) for examples.

## Need a hand?

Check the [installation troubleshooting](GETTING_STARTED.md#troubleshooting), or [join Discord](https://discord.gg/FjUbXtdCzU).
