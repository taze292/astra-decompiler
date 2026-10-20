# Installation

Use the prebuilt Windows x64 release. No compiler or build tools are needed.

## Download the release

1. Open the [latest release](https://github.com/taze292/astra-decompiler/releases/latest).
2. Under **Assets**, download the **Windows x64 ZIP**.
3. Extract the whole ZIP into a folder.

The ZIP contains `astra.exe`, `Start-Decompiler.cmd`, and the matching `Decompile.luau`. GitHub's source-code ZIP does not contain the built executable.

## Start the local server

Run `Start-Decompiler.cmd` and leave its window open. To start it manually from the extracted folder:

```powershell
.\astra.exe --serve
```

The default address is `ws://127.0.0.1:8877/decompile`. The server accepts local connections only.

## Install the bridge

Your executor needs `getscriptbytecode` and `WebSocket.connect` or `websocket.connect`.

Open the included `Decompile.luau`, copy it, and execute it in your executor. Keep the bridge and executable from the same release.

```luau
local Source = decompile(SomeScript)
```

Pass a script Instance with accessible bytecode, or a raw bytecode string. The bridge connects before replacing `getgenv().decompile`; it does not require or execute the target module.

## Change the output

Change your defaults or override one call:

```luau
getgenv().AstraOptions.Casing = "Camel"

local Source = decompile(SomeScript, {
    IncludeHeader = false,
})
```

See [customization](CUSTOMIZATION.md) for common settings and [all options](OPTIONS.md) for the complete reference.

## Troubleshooting

### The result is nil

The bridge is silent by default. Read its error and last request:

```luau
local Bridge = getgenv().AstraBridge
local Error = Bridge.LastError
local Request = Bridge.LastRequest
```

Check that the server is running, the required executor APIs exist, and the target has accessible bytecode. Invalid option names and values also cause errors.

### The server closes immediately

Run `.\astra.exe --serve` from PowerShell to see the error. If the port is busy, close the other Astra server or [choose another port](SETUPS.md#another-local-port).

### A request times out

Pass a longer timeout in seconds:

```luau
local Source = decompile(SomeScript, 60, {
    IncludeHeader = false,
})
```

A timeout stops waiting; it does not cancel work already running on the server.

### Connection settings do not take effect

Reinstall the bridge after changing the URL, timeout, queue, transport, concurrency, or cache settings. Output options can change between calls. See [bridge configuration](OPTIONS.md#live-bridge-configuration).

### Reconstruction fails

Astra reports an error when it cannot reconstruct a function. State-machine fallback is disabled by default. Keep the error and a reproducible bytecode fixture when reporting a bug.

### Restore the previous decompiler

Call `getgenv().AstraBridge.Stop()`.

For more help, [join Discord](https://discord.gg/cyjC3QejpG).
