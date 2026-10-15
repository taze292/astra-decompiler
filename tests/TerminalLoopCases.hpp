#pragma once

#include "Astra/Decompiler.hpp"
#include "Luau/Compiler.h"
#include "lua.h"
#include "lualib.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

// Bound execution with the VM interrupt instead of changing the terminal loop.
// This verifies that reconstruction preserves both the preceding branch effects
// and nontermination, without ever waiting for an infinite fixture to finish.
template <typename CheckFunction> void RunTerminalLoopCases(CheckFunction Check)
{
    auto TraceUntilLoop = [](const std::string &Bytes)
    {
        std::unique_ptr<lua_State, decltype(&lua_close)> State(luaL_newstate(), lua_close);
        luaL_openlibs(State.get());
        unsigned Budget = 64;
        lua_callbacks(State.get())->userdata = &Budget;
        lua_callbacks(State.get())->interrupt = [](lua_State *Thread, int Gc)
        {
            if (Gc < 0 && --*static_cast<unsigned *>(lua_callbacks(Thread)->userdata) == 0)
                luaL_error(Thread, "Astra terminal loop fence");
        };
        if (luau_load(State.get(), "terminal-loop", Bytes.data(), Bytes.size(), 0))
            throw std::runtime_error(lua_tostring(State.get(), -1));
        if (!lua_pcall(State.get(), 0, 0, 0))
            throw std::runtime_error("Terminal loop unexpectedly returned");
        const char *Error = lua_tostring(State.get(), -1);
        if (!Error || std::string(Error).find("Astra terminal loop fence") == std::string::npos)
            throw std::runtime_error(Error ? Error : "Terminal loop failed without a message");
        lua_getglobal(State.get(), "TerminalLoopTrace");
        const char *Trace = lua_tostring(State.get(), -1);
        if (!Trace)
            throw std::runtime_error("Terminal loop lost its preceding effects");
        return std::string(Trace);
    };

    const std::pair<const char *, const char *> Branches[] = {
        {"nested else", R"(
if X.Core then
    if X.Core.RemoteEvent then
        TerminalLoopTrace ..= "A"
        X.Core.RemoteEvent = nil
        table.clear(X.Core)
    else
        TerminalLoopTrace ..= "B"
        table.freeze(X.Core)
    end
end
)"},
        {"outer else", R"(
if X.Core then
    if X.Core.RemoteEvent then
        TerminalLoopTrace ..= "A"
        table.clear(X.Core)
    end
else
    TerminalLoopTrace ..= "B"
    table.freeze(X)
end
)"},
        {"short circuit", R"(
if X.Core and X.Core.RemoteEvent then
    TerminalLoopTrace ..= "A"
    X.Core.RemoteEvent = nil
    table.clear(X.Core)
    table.freeze(X.Core)
end
)"},
    };
    const char *Inputs[] = {"{Core={RemoteEvent=true}}", "{Core={}}", "{}"};
    for (const auto &[Name, Branch] : Branches)
        for (const char *Input : Inputs)
            for (int Optimization = 0; Optimization <= 2; ++Optimization)
                for (int Debug : {0, 2})
                {
                    const std::string Source = R"(
TerminalLoopTrace = ""
local task = {
    wait = function() TerminalLoopTrace ..= "W" end,
    spawn = function() TerminalLoopTrace ..= "S" end,
}
local X = )" + std::string(Input) + R"(
task.wait(5)
task.spawn(function()
    task.wait(5)
    while true do end
end)
)" + Branch + "\nwhile true do end\n";
                    Luau::CompileOptions Compiler;
                    Compiler.optimizationLevel = Optimization;
                    Compiler.debugLevel = Debug;
                    auto Bytes = Luau::compile(Source, Compiler);
                    const auto Expected = TraceUntilLoop(Bytes);
                    for (unsigned Passes : {0u, 4u})
                    {
                        Astra::Options Settings;
                        Settings.IncludeHeader = false;
                        Settings.AllowFallback = false;
                        Settings.OptimizationPasses = Passes;
                        auto Result = Astra::Decompile(Bytes, Settings);
                        const auto Context = std::string(Name) + " O" + std::to_string(Optimization) + " D" + std::to_string(Debug) + " P" +
                                             std::to_string(Passes);
                        // O0 can retain an equivalent literal guard such as
                        // `while not (not true)`. Assert native reconstruction
                        // here; the interrupt fence below proves it never exits.
                        Check(Result.StateMachineFunctions == 0, "Terminal loop used a state machine: " + Context);
                        Check(TraceUntilLoop(Luau::compile(Result.Source, Compiler)) == Expected,
                              "Terminal loop branch effects changed: " + Context);
                    }
                }
}
