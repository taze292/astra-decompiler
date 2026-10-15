# Build from source

Most users can use the [prebuilt Windows release](https://github.com/taze292/astra-decompiler/releases/latest). Build manually if you want to change Astra, embed its C++ library, or use the CLI on another platform.

## Get the source

Clone [Astra](https://github.com/taze292/astra-decompiler), then put the [official Luau source](https://github.com/luau-lang/luau) in its `luau/` folder:

```sh
git clone https://github.com/taze292/astra-decompiler.git
cd astra-decompiler
git clone https://github.com/luau-lang/luau.git luau
```

The Luau checkout supplies bytecode headers. Tests also use its compiler and VM. It is not needed to run a built executable.

## Windows

Install CMake and Visual Studio 2022 with the C++ tools, Windows SDK, and ClangCL. From the repository folder:

```powershell
./build.ps1 -Test
./build/Release/astra.exe --serve
```

Execute this checkout's [tools/Decompile.luau](../tools/Decompile.luau) in your executor. The [installation guide](GETTING_STARTED.md#install-the-bridge) explains how to use it.

For a Luau checkout elsewhere:

```powershell
./build.ps1 -Test -LuauDirectory C:/path/to/luau
```

## Linux and macOS

Use CMake 3.20 or newer and a C++20 compiler:

```sh
cmake -S . -B build -DASTRA_LUAU_DIR=./luau -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/astra input.luac -o output.luau
```

The CLI and library are portable; the WebSocket server is Windows-only. Executable paths depend on the CMake generator. Add `-DASTRA_BUILD_TESTS=OFF` to the configure command to omit compiler/VM tests and fixture tools.

## C++ integration

Link the `AstraDecompiler` target and include `<Astra/Decompiler.hpp>`. See [CLI and integrations](SETUPS.md#c-library) for a minimal API example and the [options reference](OPTIONS.md) for settings.
