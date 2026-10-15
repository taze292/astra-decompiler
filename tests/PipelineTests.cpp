#include "Astra/Decompiler.hpp"
#include "AstraOutputCases.hpp"
#include "Luau/Compiler.h"
#include "TerminalLoopCases.hpp"
#include "lua.h"
#include "lualib.h"
#include <functional>
#include <iostream>
#include <memory>
#include <string>

namespace
{
unsigned Passed = 0, Failed = 0;
void Check(bool Ok, const std::string &Message)
{
    if (!Ok)
        throw std::runtime_error(Message);
    ++Passed;
}
void Reject(const std::function<void()> &Action)
{
    try
    {
        Action();
    }
    catch (const Astra::Error &)
    {
        ++Passed;
        return;
    }
    throw std::runtime_error("Expected an Astra::Error");
}
std::string Execute(const std::string &Bytes)
{
    std::unique_ptr<lua_State, decltype(&lua_close)> L(luaL_newstate(), lua_close);
    luaL_openlibs(L.get());
    unsigned Budget = 100000;
    lua_callbacks(L.get())->userdata = &Budget;
    lua_callbacks(L.get())->interrupt = [](lua_State *State, int Gc)
    {
        if (Gc < 0 && --*static_cast<unsigned *>(lua_callbacks(State)->userdata) == 0)
            luaL_error(State, "Execution budget exceeded");
    };
    if (Bytes.empty() || Bytes[0] == 0)
        throw std::runtime_error("Compilation failed: " + Bytes.substr(1));
    if (luau_load(L.get(), "pipeline-test", Bytes.data(), Bytes.size(), 0) || lua_pcall(L.get(), 0, LUA_MULTRET, 0))
        throw std::runtime_error(lua_tostring(L.get(), -1));
    std::string Out;
    for (int I = 1; I <= lua_gettop(L.get()); ++I)
    {
        Out += std::to_string(lua_type(L.get(), I)) + ':';
        if (lua_isboolean(L.get(), I))
            Out += lua_toboolean(L.get(), I) ? "true" : "false";
        else if (!lua_isnil(L.get(), I))
        {
            size_t Size = 0;
            const char *Text = lua_tolstring(L.get(), I, &Size);
            if (!Text)
                throw std::runtime_error("Fixture must return scalar values");
            Out += std::to_string(Size) + ':' + std::string(Text, Size);
        }
        Out += ';';
    }
    return Out;
}
} // namespace

int main()
{
    try
    {
        RunAstraOutputCases(Check, Execute);
        RunTerminalLoopCases(Check);
        const char *Source = R"(
local HTTPServer = 3
local user_value = 5
local String = "literal UserValue HTTPServer Value1 \031123\030"
local function mixedCaseFunction(userValue, ...)
    local totalValue = userValue + HTTPServer
    local function increment(deltaValue) totalValue += deltaValue return totalValue end
    for loopValue = 1, 4 do increment(loopValue) end
    return increment(user_value), String, select('#', ...), ...
end
local function recurse(n) if n == 0 then return 1 end return n * recurse(n-1) end
local answer, text, count, x, y = mixedCaseFunction(7, 9, nil)
local callbacks = {}
for propertyName = 1, 3 do callbacks[#callbacks+1] = function() return propertyName end end
for propertyName = 4, 6 do callbacks[#callbacks+1] = function() return propertyName end end
return answer, text, count, x, y, recurse(5), string.upper("literal"), callbacks[1](), callbacks[4]()
)";
        for (int Debug : {0, 2})
        {
            Luau::CompileOptions Compiler;
            Compiler.debugLevel = Debug;
            auto Bytes = Luau::compile(Source, Compiler);
            auto Expected = Execute(Bytes);
            for (auto Casing :
                 {Astra::VariableCase::Pascal, Astra::VariableCase::Camel, Astra::VariableCase::Snake, Astra::VariableCase::Preserve})
                for (auto Naming : {Astra::VariableNaming::Inferred, Astra::VariableNaming::Debug, Astra::VariableNaming::Generic})
                    for (bool Types : {false, true})
                        for (bool Preserve : {false, true})
                            for (unsigned Passes : {0u, 1u, 5u})
                                for (bool Machine : {false, true})
                                {
                                    Astra::Options S;
                                    S.Casing = Casing;
                                    S.Naming = Naming;
                                    S.UseTypeNames = Types;
                                    S.PreserveFunctionNames = Preserve;
                                    S.OptimizationPasses = Passes;
                                    S.ForceStateMachine = Machine;
                                    auto Actual = Astra::Decompile(Bytes, S);
                                    try
                                    {
                                        Check(Execute(Luau::compile(Actual.Source)) == Expected, "Changed execution result");
                                    }
                                    catch (...)
                                    {
                                        std::cerr << "Configuration: debug=" << Debug << " casing=" << int(Casing)
                                                  << " naming=" << int(Naming) << " types=" << Types << " preserve=" << Preserve
                                                  << " passes=" << Passes << " machine=" << Machine << '\n'
                                                  << Actual.Source << '\n';
                                        throw;
                                    }
                                }
        }
        Luau::CompileOptions Debug;
        const char *CaptureFixtures[] = {
            "local n=0; local function inc(x) n+=x end; inc(4); inc(3); return n",
            "local a; do local n=1; a=function() n+=1 return n end end; local n=40; return a(),a(),n",
            "local fs={} for i=1,4 do local n=i; fs[i]=function() n+=1 return n end end return fs[1](),fs[4](),fs[1]()",
            "local n=0; local function outer() return function() n+=1 return n end end; local f=outer(); return f(),f(),n",
            "local n=0; local f=function() n+=1 end; local x=n; f(); return x,n",
            "local t={x=3}; function t:add(n) self.x+=n return self.x end return t:add(4),t.x",
            "local t={x=3}; function t:make() local a={x=5}; function a:add(n) self.x+=n return self.x end return a:add(self.x) end return "
            "t:make()",
            "local t={x=3}; function t:make() local outer=self; local a={x=5}; function a:add(n) self.x+=n return self.x+outer.x end "
            "return a:add(2) end return t:make()"};
        for (auto Fixture : CaptureFixtures)
            for (int Level : {0, 1, 2})
                for (int Symbols : {0, 2})
                    for (bool Native : {false, true})
                        for (bool Machine : {false, true})
                        {
                            Luau::CompileOptions C;
                            C.optimizationLevel = Level;
                            C.debugLevel = Symbols;
                            auto Input = Luau::compile(Fixture, C);
                            Astra::Options O;
                            O.NativeCaptures = Native;
                            O.ForceStateMachine = Machine;
                            auto Output = Astra::Decompile(Input, O);
                            Check(Execute(Luau::compile(Output.Source)) == Execute(Input), "Capture/method regression: " + Output.Source);
                        }
        {
            auto Output = Astra::Decompile(Luau::compile(CaptureFixtures[0])).Source;
            Check(Output.find("[1] =") == std::string::npos, "Straight-line native capture still boxed");
            auto Method = Astra::Decompile(Luau::compile(CaptureFixtures[5])).Source;
            Check(Method.find(":add(") != std::string::npos && Method.find("self.") != std::string::npos,
                  "Method receiver was not made implicit");
        }
        Debug.debugLevel = 2;
        {
            auto Input = Luau::compile("local n=2; local function bump() n+=1 end; bump(); return n");
            for (bool Compact : {false, true})
                for (bool Spacing : {false, true})
                    for (bool Indexes : {false, true})
                    {
                        Astra::Options O;
                        O.CompactUpvalues = Compact;
                        O.IncludeUpvalueIndexes = Indexes;
                        O.GroupSpacing = Spacing;
                        O.InferCallNames = false;
                        O.InferMemberNames = false;
                        O.Naming = Astra::VariableNaming::Generic;
                        O.LocalPrefix = "item";
                        O.ArgumentPrefix = "arg";
                        O.Casing = Astra::VariableCase::Preserve;
                        auto Text = Astra::Decompile(Input, O).Source;
                        Check(Execute(Luau::compile(Text)) == Execute(Input), "Formatting/naming changed capture behavior");
                        Check((Text.find("-- Upvalues:") != std::string::npos) == Compact, "Capture comment style ignored");
                        Check((Text.find("[1] ") != std::string::npos) == Indexes, "Capture indexes must be independent of comment style");
                    }
            Astra::Options Parsed;
            Astra::ApplyOption(Parsed, "upvalue-indexes", "false");
            Check(!Parsed.IncludeUpvalueIndexes && !Parsed.CompactUpvalues, "Upvalue option defaults/parser ignored");
            Astra::ApplyOption(Parsed, "CompactArrays", "false");
            Astra::ApplyOption(Parsed, "UseTabs", "true");
            Astra::ApplyOption(Parsed, "BlankLinesBetweenGroups", "2");
            Astra::ApplyOption(Parsed, "SpaceInsideTableBraces", "false");
            Astra::ApplyOption(Parsed, "Casing", "PascalCase");
            Astra::ApplyOption(Parsed, "Naming", "Inferred");
            Check(!Parsed.CompactArrays && Parsed.UseTabs && Parsed.BlankLinesBetweenGroups == 2 && !Parsed.SpaceInsideTableBraces &&
                      Parsed.Casing == Astra::VariableCase::Pascal && Parsed.Naming == Astra::VariableNaming::Inferred,
                  "PascalCase option names or enum values ignored");
            Check(Astra::CanonicalOptionKey("MaxAnalysisWork") == "max-analysis-work" &&
                      Astra::CanonicalOptionKey("ModulePath") == "module-path",
                  "Canonical keys do not cover resource and reserved aliases");
            Reject([&] { Astra::ApplyOption(Parsed, "compactArrays", "true"); });
            Astra::ApplyOption(Parsed, "BlankLinesBetweenGroups", "4");
            Reject([&] { Astra::ValidateOptions(Parsed); });
            for (const char *Prefix : {"", "1bad", "bad.name", "bad-name", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})
            {
                Astra::Options O;
                O.LocalPrefix = Prefix;
                Reject([&] { Astra::Decompile(Input, O); });
            }
            Astra::Options O;
            O.ArgumentPrefix = "arg";
            O.LocalPrefix = "temp";
            O.UseTypeNames = false;
            O.Casing = Astra::VariableCase::Camel;
            auto Function = Luau::compile("return function(input) return input+1 end");
            Check(Astra::Decompile(Function, O).Source.find("arg1") != std::string::npos, "Anonymous argument prefix ignored");
        }
        auto Bytes = Luau::compile(
            "local user_value = 9; local function mixedCaseFunction() return user_value end; return mixedCaseFunction", Debug);
        Astra::Options S;
        S.Casing = Astra::VariableCase::Preserve;
        Check(Astra::Decompile(Bytes, S).Source.find("user_value") != std::string::npos, "Preserve casing lost debug spelling");
        S.Casing = Astra::VariableCase::Snake;
        S.PreserveFunctionNames = false;
        Check(Astra::Decompile(Bytes, S).Source.find("mixed_case_function") != std::string::npos, "Snake function casing missing");
        S = {};
        S.ModulePath = "game.Module";
        for (bool Gc : {false, true})
            for (bool Require : {false, true})
                for (bool Lines : {false, true})
                {
                    S.IncludeGcLocators = Gc;
                    S.IncludeRequireLocators = Require;
                    S.IncludeLineComments = Lines;
                    auto Text = Astra::Decompile(Bytes, S).Source;
                    Check((Text.find(" | require(") != std::string::npos) == (Require && Lines), "Require switch ignored");
                    Check((Text.find(" | filtergc(") != std::string::npos) == (!Require && Gc && Lines), "GC switch ignored");
                    Check(Text.find(" | \n") == std::string::npos, "Empty locator separator");
                }
        for (auto Pair : {std::pair{"indent", "2junk"},
                          {"indent", "-1"},
                          {"indent", "4294967296"},
                          {"header", "yes"},
                          {"casing", "invalid"},
                          {"typo", "true"}})
            Reject(
                [&]
                {
                    Astra::Options Settings;
                    Astra::ApplyOption(Settings, Pair.first, Pair.second);
                });
        for (auto Pair : {std::pair{"indent", "0"},
                          {"optimization-passes", "9"},
                          {"max-nesting", "129"},
                          {"max-output-bytes", "0"},
                          {"max-analysis-cells", "64000001"}})
            Reject(
                [&]
                {
                    Astra::Options Settings;
                    Astra::ApplyOption(Settings, Pair.first, Pair.second);
                    Astra::Decompile(Bytes, Settings);
                });
        S = {};
        S.MaxOutputBytes = 12;
        Reject([&] { Astra::Decompile(Bytes, S); });
        Reject([&] { Astra::Disassemble(Bytes, S); });
        S = {};
        S.MaxAnalysisCells = 1;
        Reject([&] { Astra::Decompile(Bytes, S); });
        S = {};
        S.MaxAnalysisWork = 1;
        Reject([&] { Astra::Decompile(Bytes, S); });
        S = {};
        S.ForceStateMachine = true;
        S.AllowFallback = false;
        Reject([&] { Astra::Decompile(Bytes, S); });
        Astra::Options Deterministic;
        Deterministic.IncludeHeader = false;
        auto Header = Astra::Decompile(Bytes).Source;
        Check(Header.starts_with("-- Decompiled with Astra | Bytecode Version: ") && Header.find("\n-- Time taken: ") != std::string::npos,
              "Header format differs");
        auto Result = Astra::Decompile(Bytes, Deterministic);
        Check(Result.Stats.InputBytes == Bytes.size() && Result.Stats.InstructionCount > 0 && Result.Stats.SymbolCount > 0,
              "Missing statistics");
        std::vector<std::string_view> Inputs(19, Bytes);
        Inputs[3] = "invalid";
        Inputs[11] = {};
        for (unsigned Workers : {1u, 2u, 4u, 0u, 32u})
            for (unsigned Repeat = 0; Repeat < 3; ++Repeat)
            {
                auto Results = Astra::DecompileBatch(Inputs, Deterministic, Workers);
                Check(Results.size() == Inputs.size(), "Batch lost results");
                for (std::size_t I = 0; I < Inputs.size(); ++I)
                    if (I == 3 || I == 11)
                        Check(!Results[I].Succeeded(), "Batch did not isolate malformed input");
                    else
                        Check(Results[I].Succeeded() && Results[I].Output.Source == Result.Source &&
                                  Results[I].Output.Diagnostics == Result.Diagnostics,
                              "Parallel result differs from serial result");
            }
        Check(Astra::DecompileBatch({}, {}, 4).empty(), "Empty batch failed");
        Reject([&] { Astra::DecompileBatch(Inputs, {}, 33); });
        // Deterministic byte mutations exercise validation without executing untrusted output.
        unsigned Random = 0x12345678;
        for (unsigned I = 0; I < 512; ++I)
        {
            Random = Random * 1664525u + 1013904223u;
            auto Mutated = Bytes;
            Mutated[Random % Mutated.size()] ^= char((Random >> 16) | 1);
            Astra::Options Limits;
            Limits.IncludeHeader = false;
            Limits.MaxAnalysisCells = 100000;
            Limits.MaxOutputBytes = 1024 * 1024;
            Limits.MaxNesting = 16;
            try
            {
                auto A = Astra::Decompile(Mutated, Limits);
                auto B = Astra::Decompile(Mutated, Limits);
                Check(A.Source == B.Source && A.Diagnostics == B.Diagnostics, "Mutation produced nondeterministic output");
            }
            catch (const Astra::Error &)
            {
                ++Passed;
            }
        }
    }
    catch (const std::exception &Error)
    {
        ++Failed;
        std::cerr << "FAIL pipeline: " << Error.what() << '\n';
    }
    std::cout << Passed << " pipeline checks passed, " << Failed << " failed\n";
    return Failed ? 1 : 0;
}
