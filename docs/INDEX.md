# Introduction

Astra turns Luau bytecode into readable source. Use it through the Windows bridge, the command line, or the C++ library.

## Start here

- [Install Astra](GETTING_STARTED.md) Download the release, start the server, and connect your executor.
- [Customize output](CUSTOMIZATION.md) Choose names, comments, arrays, and spacing.
- [CLI and integrations](SETUPS.md) Work with bytecode files, batches, or the C++ library.
- [Options reference](OPTIONS.md) Find every setting, default, and processing limit.
- [Build from source](BUILDING.md) Compile Astra or embed it in your own tools.
- [Troubleshooting](GETTING_STARTED.md#troubleshooting) Fix connection errors, timeouts, and failed requests.

## Your first decompilation

After [installing the bridge](GETTING_STARTED.md#install-the-bridge), pass a script with accessible bytecode:

```luau
local Source = decompile(SomeScript)
```

Change one request without changing your defaults:

```luau
local Source = decompile(SomeScript, {
    Casing = "Snake",
    IncludeHeader = false,
})
```

Astra reads bytecode; it does not execute the recovered source. Original comments, formatting, and stripped names cannot be recovered exactly.

## Need a hand?

Read the [troubleshooting guide](GETTING_STARTED.md#troubleshooting), or [join Discord](https://discord.gg/cyjC3QejpG).
