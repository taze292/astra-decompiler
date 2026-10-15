#pragma once

#include "Astra/Decompiler.hpp"
#include "Luau/Compiler.h"

#include <string>

template <typename CheckFunction, typename ExecuteFunction> void RunUpvalueBlockCases(CheckFunction Check, ExecuteFunction Execute)
{
    const char *Source = R"(
local Copy = tonumber("3")
local Ref = 0
local function Bump()
    local function Add()
        Ref += Copy
        return Ref
    end
    return Add()
end
Bump()
return Bump()
)";
    for (int Debug : {0, 2})
    {
        Luau::CompileOptions Compiler;
        Compiler.optimizationLevel = 0;
        Compiler.debugLevel = Debug;
        const auto Input = Luau::compile(Source, Compiler);
        const auto Expected = Execute(Input);
        for (bool Tabs : {false, true})
            for (bool Indexes : {false, true})
                for (bool Compact : {false, true})
                {
                    Astra::Options Settings;
                    Settings.IncludeHeader = false;
                    Settings.IncludeLineComments = false;
                    Settings.IncludeUpvalueIndexes = Indexes;
                    Settings.CompactUpvalues = Compact;
                    Settings.UseTabs = Tabs;
                    Settings.IndentWidth = 2;
                    const auto Output = Astra::Decompile(Input, Settings).Source;
                    Check(Execute(Luau::compile(Output)) == Expected, "Upvalue comment formatting changed executable source");
                    Check((Output.find("-- Upvalues:") != std::string::npos) == Compact,
                          "Compact upvalue comments did not retain their independent style");
                    Check((Output.find("[1] ") != std::string::npos) == Indexes && (Output.find("[2] ") != std::string::npos) == Indexes,
                          "Upvalue indexes did not obey their independent option");
                    Check(Output.find(" (copy)") != std::string::npos && Output.find(" (ref)") != std::string::npos,
                          "Upvalue capture modes were lost");
                    if (!Compact)
                    {
                        const std::string Pad = Tabs ? "\t" : "  ";
                        Check(Output.find(Pad + "--[[\n" + Pad + Pad + "Upvalues\n") != std::string::npos &&
                                  Output.find("\n" + Pad + "]]--\n") != std::string::npos,
                              "Multiline upvalue comments do not use indented block delimiters");
                        Check(Output.find("-- [1] ") == std::string::npos,
                              "Multiline upvalue comments retained line-comment prefixes inside the block");
                        Check(Output.find(Pad + Pad + "--[[\n" + Pad + Pad + Pad + "Upvalues\n") != std::string::npos,
                              "Nested upvalue comment content did not follow its enclosing function indentation");
                    }
                    Settings.IncludeUpvalueComments = false;
                    const auto WithoutComments = Astra::Decompile(Input, Settings).Source;
                    Check(WithoutComments.find("Upvalues") == std::string::npos && Execute(Luau::compile(WithoutComments)) == Expected,
                          "Disabled upvalue comments leaked into the source or changed behavior");
                }
    }
}
