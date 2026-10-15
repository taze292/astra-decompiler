#include "Reconstruction.hpp"
#include <chrono>

namespace Taze
{
using namespace Detail;

Result Decompile(std::string_view Bytecode, const Options &Settings)
{
    ValidateOptions(Settings);
    auto Start = std::chrono::steady_clock::now();
    auto C = ReadBytecode(Bytecode, Settings);
    auto Decoded = std::chrono::steady_clock::now();
    Engine E(C, Settings);
    Function Main(E, C.Main, {}, 0);
    auto Source = Main.Generate(true);
    if (Settings.IncludeHeader)
    {
        Source = "-- Decompiled by Taze\n-- Luau bytecode v" + std::to_string(C.Version) + " | Types v" + std::to_string(C.Types) + "\n\n" +
                 Source;
    }
    auto Reconstructed = std::chrono::steady_clock::now();
    E.Output.Source = E.Resolve(std::move(Source));
    auto Finished = std::chrono::steady_clock::now();
    if (E.Output.Source.size() > Settings.MaxOutputBytes)
        throw Error("Generated source exceeds output limit");
    E.Output.BytecodeVersion = C.Version;
    E.Output.TypeVersion = C.Types;
    E.Output.FunctionCount = C.Prototypes.size();
    E.Output.Encoding = C.Encoding;
    E.Output.Stats.DecodeMilliseconds = std::chrono::duration<double, std::milli>(Decoded - Start).count();
    E.Output.Stats.ReconstructionMilliseconds = std::chrono::duration<double, std::milli>(Reconstructed - Decoded).count();
    E.Output.Stats.FinalizationMilliseconds = std::chrono::duration<double, std::milli>(Finished - Reconstructed).count();
    E.Output.Stats.InputBytes = Bytecode.size();
    E.Output.Stats.SymbolCount = E.Symbols.size();
    for (const auto &P : C.Prototypes)
        E.Output.Stats.InstructionCount += P.Instructions.size();
    return std::move(E.Output);
}
} // namespace Taze
