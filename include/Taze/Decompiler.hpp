#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Taze
{
enum class OpcodeEncoding
{
    Plain,
    Roblox,
    Auto
};

enum class VariableCase { Pascal, Camel, Snake, Preserve };
enum class VariableNaming { Inferred, Debug, Generic };

struct Options
{
    OpcodeEncoding Encoding = OpcodeEncoding::Auto;
    unsigned IndentWidth = 4;
    bool IncludeHeader = true;
    bool IncludeLineComments = true;
    bool IncludeUpvalueComments = true;
    bool IncludeFunctionLocators = true;
    bool IncludeGcLocators = true;
    bool IncludeRequireLocators = true;
    VariableCase Casing = VariableCase::Pascal;
    VariableNaming Naming = VariableNaming::Inferred;
    bool UseTypeNames = true;
    bool PreserveFunctionNames = true;
    bool ForceStateMachine = false;
    bool AllowFallback = true;
    unsigned OptimizationPasses = 5;
    unsigned MaxNesting = 128;
    std::size_t MaxAnalysisCells = 16000000;
    std::string FilterLineField = "Line";
    std::string ModulePath;
    std::size_t MaxInputBytes = 64 * 1024 * 1024;
    std::size_t MaxOutputBytes = 64 * 1024 * 1024;
};

struct Statistics
{
    double DecodeMilliseconds = 0;
    double ReconstructionMilliseconds = 0;
    double FinalizationMilliseconds = 0;
    std::size_t InputBytes = 0;
    std::size_t InstructionCount = 0;
    std::size_t SymbolCount = 0;
};

struct Result
{
    std::string Source;
    std::vector<std::string> Diagnostics;
    unsigned BytecodeVersion = 0;
    unsigned TypeVersion = 0;
    std::size_t FunctionCount = 0;
    std::size_t StateMachineFunctions = 0;
    OpcodeEncoding Encoding = OpcodeEncoding::Plain;
    Statistics Stats;
};

class Error : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

Result Decompile(std::string_view Bytecode, const Options &Settings = {});
std::string Disassemble(std::string_view Bytecode, const Options &Settings = {});
// Strict, shared option handling. Keys use CLI spelling without the leading '--'.
void ApplyOption(Options &Settings, std::string_view Key, std::string_view Value);
void ValidateOptions(const Options &Settings);

struct BatchResult
{
    Result Output;
    std::string Failure;
    bool Succeeded() const { return Failure.empty(); }
};
// Inputs remain alive until this synchronous call returns. Results preserve input order.
// Workers=0 selects up to 8 hardware threads; explicit worker counts are capped at 32.
std::vector<BatchResult> DecompileBatch(std::span<const std::string_view> Inputs, const Options &Settings = {}, unsigned Workers = 1);
} // namespace Taze
