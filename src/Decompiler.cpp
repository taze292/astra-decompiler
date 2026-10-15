#include "Reconstruction.hpp"
#include <chrono>
#include <iomanip>
#include <locale>

namespace Astra
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
    auto Reconstructed = std::chrono::steady_clock::now();
    E.Output.Source = E.Resolve(std::move(Source));
    auto Finished = std::chrono::steady_clock::now();
    if (Settings.IncludeHeader)
    {
        std::ostringstream Header;
        Header.imbue(std::locale::classic());
        Header << "-- Decompiled with Astra | Bytecode Version: " << C.Version << "\n-- Time taken: " << std::fixed << std::setprecision(6)
               << std::chrono::duration<double>(Finished - Start).count() << "s\n\n";
        E.Output.Source.insert(0, Header.str());
    }
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
    E.Output.Stats.AnalysisWork = Settings.MaxAnalysisWork - E.Analysis.Remaining;
    for (const auto &P : C.Prototypes)
        E.Output.Stats.InstructionCount += P.Instructions.size();
    return std::move(E.Output);
}
} // namespace Astra
