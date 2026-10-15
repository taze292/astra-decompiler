#include "Taze/Decompiler.hpp"
#include <algorithm>
#include <charconv>
#include <limits>

namespace Taze
{
namespace
{
unsigned Integer(std::string_view Text)
{
    unsigned Value = 0;
    auto [End, Error] = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
    if (Error != std::errc{} || End != Text.data() + Text.size())
        throw Taze::Error("Expected an unsigned decimal integer");
    return Value;
}
bool Boolean(std::string_view Text)
{
    if (Text == "true")
        return true;
    if (Text == "false")
        return false;
    throw Error("Expected true or false");
}
} // namespace

void ApplyOption(Options &S, std::string_view Key, std::string_view Value)
{
    if (Key == "encoding")
    {
        if (Value == "auto")
            S.Encoding = OpcodeEncoding::Auto;
        else if (Value == "plain")
            S.Encoding = OpcodeEncoding::Plain;
        else if (Value == "roblox")
            S.Encoding = OpcodeEncoding::Roblox;
        else
            throw Error("Encoding must be auto, plain, or roblox");
    }
    else if (Key == "casing")
    {
        if (Value == "pascal")
            S.Casing = VariableCase::Pascal;
        else if (Value == "camel")
            S.Casing = VariableCase::Camel;
        else if (Value == "snake")
            S.Casing = VariableCase::Snake;
        else if (Value == "preserve")
            S.Casing = VariableCase::Preserve;
        else
            throw Error("Casing must be pascal, camel, snake, or preserve");
    }
    else if (Key == "naming")
    {
        if (Value == "inferred")
            S.Naming = VariableNaming::Inferred;
        else if (Value == "debug")
            S.Naming = VariableNaming::Debug;
        else if (Value == "generic")
            S.Naming = VariableNaming::Generic;
        else
            throw Error("Naming must be inferred, debug, or generic");
    }
    else if (Key == "header")
        S.IncludeHeader = Boolean(Value);
    else if (Key == "lines")
        S.IncludeLineComments = Boolean(Value);
    else if (Key == "upvalues")
        S.IncludeUpvalueComments = Boolean(Value);
    else if (Key == "locators")
        S.IncludeFunctionLocators = Boolean(Value);
    else if (Key == "gc-locators")
        S.IncludeGcLocators = Boolean(Value);
    else if (Key == "require-locators")
        S.IncludeRequireLocators = Boolean(Value);
    else if (Key == "type-names")
        S.UseTypeNames = Boolean(Value);
    else if (Key == "preserve-function-names")
        S.PreserveFunctionNames = Boolean(Value);
    else if (Key == "native-captures")
        S.NativeCaptures = Boolean(Value);
    else if (Key == "method-syntax")
        S.MethodSyntax = Boolean(Value);
    else if (Key == "fallback")
        S.AllowFallback = Boolean(Value);
    else if (Key == "state-machine")
        S.ForceStateMachine = Boolean(Value);
    else if (Key == "indent")
        S.IndentWidth = Integer(Value);
    else if (Key == "optimization-passes")
        S.OptimizationPasses = Integer(Value);
    else if (Key == "max-nesting")
        S.MaxNesting = Integer(Value);
    else if (Key == "max-analysis-cells")
        S.MaxAnalysisCells = Integer(Value);
    else if (Key == "max-analysis-work")
        S.MaxAnalysisWork = Integer(Value);
    else if (Key == "max-input-bytes")
        S.MaxInputBytes = Integer(Value);
    else if (Key == "max-output-bytes")
        S.MaxOutputBytes = Integer(Value);
    else if (Key == "filter-line")
        S.FilterLineField = Value;
    else if (Key == "module-path")
        S.ModulePath = Value;
    else
        throw Error("Unknown option: " + std::string(Key));
}

void ValidateOptions(const Options &S)
{
    if (S.Encoding != OpcodeEncoding::Auto && S.Encoding != OpcodeEncoding::Plain && S.Encoding != OpcodeEncoding::Roblox)
        throw Error("Invalid opcode encoding");
    if (S.Casing != VariableCase::Pascal && S.Casing != VariableCase::Camel && S.Casing != VariableCase::Snake &&
        S.Casing != VariableCase::Preserve)
        throw Error("Invalid variable casing");
    if (S.Naming != VariableNaming::Inferred && S.Naming != VariableNaming::Debug && S.Naming != VariableNaming::Generic)
        throw Error("Invalid variable naming");
    if (S.FilterLineField != "Line" && S.FilterLineField != "StartLine")
        throw Error("Filter field must be Line or StartLine");
    if (S.ModulePath.size() > 8192 ||
        std::any_of(S.ModulePath.begin(), S.ModulePath.end(), [](unsigned char C) { return C < 32 || C >= 127; }))
        throw Error("Module path must be a single-line ASCII expression with escaped names (maximum 8192 bytes)");
    if (S.IndentWidth < 1 || S.IndentWidth > 16)
        throw Error("Indent width must be between 1 and 16");
    if (S.OptimizationPasses > 8)
        throw Error("Optimization passes must be between 0 and 8");
    if (S.MaxNesting < 1 || S.MaxNesting > 128)
        throw Error("Max nesting must be between 1 and 128");
    if (S.MaxAnalysisCells < 1 || S.MaxAnalysisCells > 64000000)
        throw Error("Max analysis cells must be between 1 and 64000000");
    if (S.MaxAnalysisWork < 1 || S.MaxAnalysisWork > 256000000)
        throw Error("Max analysis work must be between 1 and 256000000");
    constexpr std::size_t MaxBytes = 256 * 1024 * 1024;
    if (!S.MaxInputBytes || S.MaxInputBytes > MaxBytes || !S.MaxOutputBytes || S.MaxOutputBytes > MaxBytes)
        throw Error("Input/output limits must be between 1 byte and 256 MiB");
    if (S.ForceStateMachine && !S.AllowFallback)
        throw Error("Forced state machine conflicts with disabled fallback");
}
} // namespace Taze
