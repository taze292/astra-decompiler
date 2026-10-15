#pragma once

#include "Reconstruction.hpp"
#include "Luau/Compiler.h"

#include <string>
#include <utility>

// Exercise the final source pass directly as well as through bytecode. This
// keeps compiler constant folding from hiding duplicate keys or a final call.
template <typename CheckFunction, typename ExecuteFunction> void RunAstraOutputCases(CheckFunction Check, ExecuteFunction Execute)
{
    auto Format = [](const std::string &Source, const Astra::Options &Settings)
    {
        Astra::Chunk Empty;
        Astra::Detail::Engine Engine(Empty, Settings);
        return Engine.Resolve(Source);
    };
    const std::pair<const char *, const char *> Fixtures[] = {
        {"ordered strings", R"(local t={[1]="hi",[2]="bye"} return t[1],t[2],t[3])"},
        {"array gap", R"(local t={[1]="hi",[2]="bye",[4]="later"} return t[1],t[2],t[3],t[4])"},
        {"nil terminates prefix", R"(local t={[1]="hi",[2]=nil,[3]="later"} return t[1],t[2],t[3])"},
        {"duplicate index", R"(local t={[1]="old",[2]="two",[1]="new"} return t[1],t[2])"},
        {"dynamic collision", R"(local index=1 local t={[1]="old",[index]="new"} return t[1],t[2])"},
        {"computed collision", R"(local t={[1]="old",[1+0]="new"} return t[1],t[2])"},
        {"existing list field", R"(local t={[1]="old","new"} return t[1],t[2])"},
        {"mixed hash", R"(local t={[1]="one",[2]="two",tag="tag",[4]="four"} return t[1],t[2],t[3],t[4],t.tag)"},
        {"quoted hash", R"(local t={[1]="one",[2]="two",["tag"]="tag"} return t[1],t[2],t.tag)"},
        {"literal nil text", R"(local t={[1]="nil",[2]="-- {[1]=x, [2]=y}"} return t[1],t[2])"},
        {"nested tables", R"(local t={[1]={[1]="nested",[2]="two"},[2]=3} return t[1][1],t[1][2],t[2])"},
        {"final call arity", R"(local function f() return 2,3 end local t={[1]=1,[2]=f()} return t[1],t[2],t[3])"},
        {"final vararg arity", R"(local function f(...) local t={[1]=1,[2]=...} return t[1],t[2],t[3] end return f(2,3))"},
        {"effect order",
         R"(local log="" local function f(x) log..=x return x end local t={[1]=f("a"),[2]=f("b"),tag=f("c")} return t[1],t[2],t.tag,log)"},
        {"function body commas", R"(local t={[1]=function() local a,b=2,3 return a,b end,[2]=7} return t[1](),t[2])"},
        {"long strings", R"(local t={[1]=[==[{[1]=1,[2]=2}]==],[2]="x"} return t[1],t[2])"},
        {"line comments", "local t={\n[1]=1, -- {[1]=3}\n[2]=2\n} return t[1],t[2]"},
        {"key comment", "local t={[ -- keep this comment\n1]=1,[2]=2} return t[1],t[2]"},
        {"long comments", "local t={--[==[{[1]=3}]==]\n[1]=1,[2]=2} return t[1],t[2]"},
        {"disjoint constructors across nested function blocks", R"(
local function Make(flag)
    if flag then
        local First=({[1]=1,[2]=2})
        local Factory={[1]=function(...)
            local Nested=({[1]=3,[2]=4})
            if ... then return Nested[1],Nested[2] end
            return 0
        end,[2]=5}
        local A,B=Factory[1](true)
        return First[1]+First[2]+A+B,Factory[2]
    end
end
local X,Y=Make(true)
-- } {[1]=90,[2]=99} {
local Last=(({[1]=6,[2]=7}))[2]
return X,Y,Last,"{ ignore }"
)"},
    };
    for (const auto &[Name, Source] : Fixtures)
    {
        const auto Expected = Execute(Luau::compile(Source));
        for (bool Compact : {false, true})
            for (bool Spaces : {false, true})
            {
                Astra::Options Settings;
                Settings.CompactArrays = Compact;
                Settings.SpaceInsideTableBraces = Spaces;
                auto Output = Format(Source, Settings);
                Check(Execute(Luau::compile(Output)) == Expected, std::string("Table formatting changed ") + Name + ": " + Output);
            }
    }
    Astra::Options Settings;
    const std::string NoConstructors = "local text='{ [1]=1 }' -- {[1]=2}\nreturn text";
    Check(Format(NoConstructors, Settings) == NoConstructors, "Braces confined to literals/comments were edited");
    Check(Format("return {[1]=\"hi\",[2]=\"bye\"}", Settings) == "return { \"hi\",\"bye\" }", "Ordered array keys were not compacted");
    Check(Format("return {[1]=1,[2]=nil,[3]=3}", Settings) == "return { 1,[2]=nil,[3]=3 }",
          "Literal nil did not terminate the compact prefix");
    for (const char *Source : {"return {[1]=1,[1]=2}", "return {[1]=1,[key]=2}", "return {[1]=1,2}"})
        Check(Format(Source, Settings).find("[1]") != std::string::npos, "An unsafe table prefix was compacted");
    Settings.SpaceInsideTableBraces = false;
    Check(Format("return { [1] = \"{ spaced }\", [2] = 2 }", Settings) == "return {\"{ spaced }\", 2}",
          "Table spacing changed a string or left brace padding");

    const char *BytecodeFixtures[] = {
        R"(local t={} t[1]="hi" t[2]="bye" t[4]="later" return t[1],t[2],t[3],t[4])",
        R"(local t={} t[1]="hi" t[2]=nil t[3]="later" return t[1],t[2],t[3])",
        R"(local t; local function observe() return t and "table" or "nil" end t={} t[1]=observe() return t[1])",
        R"(local a={} local b={} a[1]=b b[1]="nested" return a[1][1])",
        R"(local t={} local log="" local function f(x) log..=x return x,x+1 end t[1]=f(1) t[2]=f(2) return t[1],t[2],t[3],log)",
        R"(local t={} local key=1 t[1]="old" t[key]="new" return t[1],t[2])",
        R"(local value=1 local function mutate() value=2 return 3 end local t={} t[1]=value t[2]=mutate() return t[1],t[2],value)",
    };
    for (const char *Source : BytecodeFixtures)
        for (int Level : {0, 1, 2})
            for (int Debug : {0, 2})
            {
                Luau::CompileOptions Compiler;
                Compiler.optimizationLevel = Level;
                Compiler.debugLevel = Debug;
                const auto Input = Luau::compile(Source, Compiler);
                const auto Expected = Execute(Input);
                for (bool Compact : {false, true})
                    for (bool Tabs : {false, true})
                    {
                        Astra::Options Options;
                        Options.CompactArrays = Compact;
                        Options.UseTabs = Tabs;
                        Options.SpaceInsideTableBraces = !Tabs;
                        Options.BlankLinesBetweenGroups = Tabs ? 2 : 0;
                        auto Actual = Astra::Decompile(Input, Options).Source;
                        Check(Execute(Luau::compile(Actual)) == Expected, "Array bytecode reconstruction changed behavior: " + Actual);
                    }
            }
    const auto Captures =
        Luau::compile("local Copy=tonumber('3') local Ref=0 local function Bump() Ref+=Copy return Ref end Bump() return Bump()",
                      Luau::CompileOptions{.optimizationLevel = 0, .debugLevel = 2});
    Settings = {};
    Settings.IncludeHeader = false;
    Settings.IncludeLineComments = false;
    auto Comments = Astra::Decompile(Captures, Settings).Source;
    Check(Comments.find("Upvalues\n") != std::string::npos && Comments.find("[1] ") != std::string::npos &&
              Comments.find("[2] ") != std::string::npos && Comments.find(" (copy)") != std::string::npos &&
              Comments.find(" (ref)") != std::string::npos && Comments.find("(type ") == std::string::npos,
          "Default upvalue comments do not use indexed copy/ref lines");
    Settings.IncludeUpvalueIndexes = false;
    Check(Astra::Decompile(Captures, Settings).Source.find("[1] ") == std::string::npos,
          "Upvalue indexes cannot be disabled independently");
    const auto Nested = Luau::compile(
        "local function f(x) if x then return function(y) if y then return '    {keep}' end return 'other' end end return f end return "
        "type(f(true))");
    Settings = {};
    Settings.IncludeHeader = false;
    Settings.IncludeLineComments = false;
    Settings.IncludeUpvalueComments = false;
    Settings.UseTabs = true;
    auto Tabs = Astra::Decompile(Nested, Settings).Source;
    Check(Tabs.find("\n\t\t") != std::string::npos && Tabs.find("\n ") == std::string::npos && Tabs.find("    {keep}") != std::string::npos,
          "Nested closures do not consistently use tabs or altered literal spacing");
    for (unsigned Blank : {0u, 1u, 2u})
    {
        Settings.BlankLinesBetweenGroups = Blank;
        auto Text = Astra::Decompile(Nested, Settings).Source;
        Check(Execute(Luau::compile(Text)) == Execute(Nested), "Blank-line spacing changed behavior");
        Check((Text.find("\n\n") != std::string::npos) == (Blank > 0), "Zero blank lines were ignored");
        Check(Text.find(std::string(Blank + 3, '\n')) == std::string::npos, "Group spacing exceeds its configured count");
        if (Blank > 0)
            Check(Text.find(std::string(Blank + 1, '\n')) != std::string::npos, "Configured group spacing was not emitted");
    }
}
