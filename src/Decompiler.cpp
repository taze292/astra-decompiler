#include "Reconstruction.hpp"

namespace Taze
{
using namespace Detail;

Result Decompile(std::string_view Bytecode, const Options &Settings)
{
    if (Settings.FilterLineField != "Line" && Settings.FilterLineField != "StartLine")
        throw Error("Filter field must be Line or StartLine");
    if (Settings.ModulePath.size() > 8192 ||
        std::any_of(Settings.ModulePath.begin(), Settings.ModulePath.end(), [](unsigned char C) { return C < 32 || C >= 127; }))
        throw Error("Module path must be a single-line ASCII expression with escaped names (maximum 8192 bytes)");
    if (Settings.IndentWidth < 1 || Settings.IndentWidth > 16)
        throw Error("Indent width must be between 1 and 16");
    auto C = ReadBytecode(Bytecode, Settings);
    Engine E(C, Settings);
    Function Main(E, C.Main, {}, 0);
    auto Source = Main.Generate(true);
    if (Settings.IncludeHeader)
    {
        Source = "-- Decompiled by Taze\n-- Luau bytecode v" + std::to_string(C.Version) + " | Types v" + std::to_string(C.Types) + "\n\n" +
                 Source;
    }
    E.Output.Source = E.Resolve(std::move(Source));
    if (E.Output.Source.size() > Settings.MaxOutputBytes)
        throw Error("Generated source exceeds output limit");
    E.Output.BytecodeVersion = C.Version;
    E.Output.TypeVersion = C.Types;
    E.Output.FunctionCount = C.Prototypes.size();
    E.Output.Encoding = C.Encoding;
    return std::move(E.Output);
}
} // namespace Taze
