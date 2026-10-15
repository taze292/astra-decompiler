# Getting started

## Build Astra

Clone the repository and place the [official Luau source](https://github.com/luau-lang/luau) in `luau/`. Astra needs Luau's bytecode headers to build; the test suite also uses its compiler and VM.

On Windows, install CMake and Visual Studio 2022 with C++ tools, the Windows SDK, and ClangCL. In the repository folder, run:

```powershell
./build.ps1 -Test
```

For a Luau checkout elsewhere:

```powershell
./build.ps1 -Test -LuauDirectory C:/path/to/luau
```

The executable is `build/Release/astra.exe`. Luau's source checkout is not needed at runtime. See [other platforms and the C++ API](SETUPS.md) for CMake commands.

## Start the local server

```powershell
./build/Release/astra.exe --serve
```

Leave it running. The default address is `ws://127.0.0.1:8877/decompile`; it accepts local connections only.

## Install the bridge

Execute [tools/Decompile.luau](../tools/Decompile.luau) in your Roblox executor. It requires `getscriptbytecode` and `WebSocket.connect` or `websocket.connect`. Astra follows [Volt's documented WebSocket API](https://docs.voltbz.net/docs/websocket).

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
