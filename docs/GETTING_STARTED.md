# Installation

## Download the release

1. Open the [latest GitHub release](https://github.com/taze292/astra-decompiler/releases/latest).
2. Download the **Windows x64 ZIP** from **Assets**. Choose the packaged release, not GitHub's source-code ZIP.
3. Extract the entire ZIP into a folder.

The package includes `astra.exe`, `Start-Decompiler.cmd`, and the matching `Decompile.luau` bridge. You do not need Visual Studio, CMake, Clang, or the Luau source to use it. To compile Astra yourself, see [build from source](BUILDING.md).

## Start the local server

Run **`Start-Decompiler.cmd`** from the extracted folder and leave its window open. You can also start it from PowerShell in that folder:

```powershell
.\astra.exe --serve
```

Leave it running. The default address is `ws://127.0.0.1:8877/decompile`; it accepts local connections only.

## Install the bridge

Open the included **`Decompile.luau`**, copy its contents, and execute it in your Roblox executor. Keep the bridge and executable from the same release.

Your executor needs `getscriptbytecode` and `WebSocket.connect` or `websocket.connect`. The included bridge works without downloading another script.

The script connects before installing `getgenv().decompile`. It does not print or warn.

```luau
local Source = decompile(SomeScript)
```

The target must be a script Instance with accessible bytecode, including a ModuleScript, LocalScript, or client-run Script. Decompiling a module does not require or execute it.

## Change the output

Set global defaults or pass options for one request:

```luau
getgenv().AstraOptions.Casing = "Camel"

local Source = decompile(SomeScript, {
    IncludeRequireLocators = false,
    IncludeGcLocators = true,
})
```

Missing fields use internal defaults. Start with [customization](CUSTOMIZATION.md), or browse the [full reference](OPTIONS.md).

## Troubleshooting

**`decompile` returns nil:** inspect the error without enabling bridge output:

```luau
local Error = getgenv().AstraBridge.LastError
local Request = getgenv().AstraBridge.LastRequest
```

Check that the server is running and your executor exposes the required APIs. A script may have no accessible bytecode. Unknown option names and invalid values are rejected rather than silently ignored.

**The server closes immediately:** start it with `.\astra.exe --serve` from PowerShell to read the error. If the port is already in use, close the other Astra server or [choose another port](SETUPS.md#another-local-port).

State-machine fallback is disabled by default. If Astra cannot reconstruct a function safely, `LastError` reports the failure instead of returning a dispatcher. Keep the error and a reproducible bytecode fixture when reporting a reconstruction bug.

**A large request times out:** pass a longer timeout in seconds:

```luau
local Source = decompile(SomeScript, 60, {
    IncludeHeader = false,
})
```

Timeout stops waiting; it does not cancel work already running on the server. [Processing limits](OPTIONS.md#shared-options) can be reduced per request.

**Connection settings changed:** reinstall the bridge after editing `AstraConfig.Url`, `Timeout`, or `MaxPending`. Output options can change without reinstallation.

**Restore the previous decompiler:** call `getgenv().AstraBridge.Stop()`.

For more help, [join Discord](https://discord.gg/FjUbXtdCzU).
