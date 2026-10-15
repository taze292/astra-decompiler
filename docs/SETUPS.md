# Other setups

## Roblox bridge

Use `astra.exe --serve` and [Decompile.luau](../tools/Decompile.luau) for live script Instances. Follow [getting started](GETTING_STARTED.md). Bytecode stays between the game and your local loopback server.

To use a different local port:

```powershell
./build/Release/astra.exe --serve --port 9000
```

Before installing the bridge:

```luau
getgenv().AstraConfig = {
    Url = "ws://127.0.0.1:9000/decompile",
}
```

## Individual bytecode files

```powershell
./build/Release/astra.exe input.luac -o output.luau
./build/Release/astra.exe input.luac --casing snake --no-header -o output.luau
./build/Release/astra.exe input.luac --stats -o output.luau
./build/Release/astra.exe input.luac --disassemble -o instructions.asm
./build/Release/astra.exe --help
```

Input must be raw, decompressed serialized Luau bytecode. Source files, hex/base64 text, encrypted assets, and machine code are not accepted. Automatic decoding validates the instruction stream; `--encoding plain` and `--encoding roblox` select an encoding explicitly.

CLI options use kebab-case names. Boolean settings take `true` or `false`; `--no-key` disables them. The [reference](OPTIONS.md) maps every CLI option to its PascalCase Luau/C++ field.

## Batches

```powershell
./build/Release/astra.exe --batch --jobs 4 --output-dir output a.luac b.luac
```

Batch workers process independent files concurrently. Results retain input order, and one failed file does not discard successful results. Existing outputs require `--overwrite`; filename collisions are rejected. `--jobs 0` selects up to eight hardware threads; explicit worker counts are capped at 32.

## C++ library

Link `AstraDecompiler` and include its public header:

```cpp
#include <Astra/Decompiler.hpp>

Astra::Options Settings;
Settings.Casing = Astra::VariableCase::Snake;
Settings.IncludeHeader = false;

auto Result = Astra::Decompile(Bytecode, Settings);
auto Source = Result.Source;
```

`Bytecode` is a `std::string_view` containing serialized bytes. Invalid input or settings throw `Astra::Error`. `DecompileBatch` returns ordered results with individual failures; input storage must remain alive until it returns. See the [API reference and batch details](OPTIONS.md#cli-and-batch-controls).

## Build on other platforms

Astra's CLI and library use C++20 and CMake. The WebSocket server is Windows-only.

```sh
cmake -S . -B build -DASTRA_LUAU_DIR=/path/to/luau -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The executable's location depends on your CMake generator. `-DASTRA_BUILD_TESTS=OFF` omits the compiler/VM tests and fixture tools.

## Compatibility

Astra reads serialized Luau versions 3–14, type metadata 1–3, and the experimental version-100 envelope. See [Astra 0.5 control-flow validation](OBFUSCATED_CONTROL_FLOW.md) for the current checks and the [0.4 validation record](ASTRA_VALIDATION.md) for earlier coverage.

Original comments, formatting, stripped identifiers, and source types are not recoverable exactly. Unsupported bytecode, invalid metadata, and exceeded processing limits are reported as errors. `AllowFallback=false` is the default: a reconstruction failure returns an error instead of an inserted state machine. Developers can explicitly enable fallback for diagnosis. `--stats` reports the number of fallback functions.

Debug-stack inspection, closure identity, environment manipulation, and undefined table-order/length behavior are outside the equivalence guarantee. Compiling generated source establishes source compatibility; behavioral fixtures test additional semantics. See [architecture](ARCHITECTURE.md) and the [control-flow audit](CONTROL_FLOW_AUDIT.md) for technical details.
