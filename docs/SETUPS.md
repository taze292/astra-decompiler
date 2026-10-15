# CLI and integrations

Run these commands from the extracted release folder. If you built Astra yourself, use your build's executable path instead.

## Another local port

To use a different local port:

```powershell
.\astra.exe --serve --port 9000
```

Before installing the bridge:

```luau
getgenv().AstraConfig = {
    Url = "ws://127.0.0.1:9000/decompile",
}
```

## Individual bytecode files

```powershell
.\astra.exe input.luac -o output.luau
.\astra.exe input.luac --casing snake --no-header -o output.luau
.\astra.exe input.luac --stats -o output.luau
.\astra.exe input.luac --disassemble -o instructions.asm
.\astra.exe --help
```

Input must be raw, decompressed serialized Luau bytecode. Source files, hex/base64 text, encrypted assets, and machine code are not accepted. Automatic decoding validates the instruction stream; `--encoding plain` and `--encoding roblox` select an encoding explicitly.

CLI options use kebab-case names. Boolean settings take `true` or `false`; `--no-key` disables them. The [reference](OPTIONS.md) maps every CLI option to its PascalCase Luau/C++ field.

## Batches

```powershell
.\astra.exe --batch --jobs 4 --output-dir output a.luac b.luac
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

## Compatibility

Astra reads serialized Luau versions 3–14, type metadata 1–3, and the experimental version-100 envelope.

Original comments, formatting, stripped identifiers, and source types are not recoverable exactly. Unsupported bytecode, invalid metadata, and exceeded processing limits are reported as errors. `AllowFallback=false` is the default: a reconstruction failure returns an error instead of an inserted state machine. Developers can explicitly enable fallback for diagnosis. `--stats` reports the number of fallback functions.

Debug-stack inspection, closure identity, environment manipulation, and undefined table-order/length behavior are outside the equivalence guarantee. Review recovered code before relying on its behavior.
