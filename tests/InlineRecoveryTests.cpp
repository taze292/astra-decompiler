#include "Luau/BytecodeBuilder.h"
#include "Luau/Common.h"
#include "Luau/Compiler.h"
#include "Taze/Decompiler.hpp"
#include "lua.h"
#include "lualib.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

LUAU_FASTFLAG(LuauEmitCallFeedback)

namespace
{
enum class Callable
{
    Original,
    Replacement,
    Builtin,
    Table
};

std::string GuardedInline(Callable Which, bool Tuple, bool Match, bool InvalidRegister = false, bool Nested = false)
{
    const auto Saved = FFlag::LuauEmitCallFeedback.value;
    struct Restore
    {
        bool Value;
        ~Restore()
        {
            FFlag::LuauEmitCallFeedback.value = Value;
        }
    } Reset{Saved};
    FFlag::LuauEmitCallFeedback.value = true;
    Luau::BytecodeBuilder Builder;
    auto Child = [&](bool Replacement)
    {
        auto Id = Builder.beginFunction(1);
        Builder.setDebugFunctionName(Replacement ? Luau::BytecodeBuilder::StringRef{"Replacement", 11}
                                                 : Luau::BytecodeBuilder::StringRef{"Original", 8});
        auto Increment = Builder.addConstantNumber(Replacement ? 20 : 2);
        Builder.emitABC(LOP_ADDK, 1, 0, uint8_t(Increment));
        if (Tuple)
        {
            Builder.emitABC(LOP_LOADNIL, 2, 0, 0);
            Builder.emitAD(LOP_LOADN, 3, 4);
        }
        Builder.emitABC(LOP_RETURN, 1, Tuple ? 4 : 2, 0);
        Builder.endFunction(4, 0);
        return Id;
    };
    auto Original = Child(false);
    auto Replacement = Child(true);
    auto Main = Builder.beginFunction(0);
    auto OriginalIndex = Builder.addChildFunction(Original);
    auto ReplacementIndex = Builder.addChildFunction(Replacement);
    if (Nested)
        Builder.emitABC(LOP_GETUPVAL, 3, 0, 0);
    if (Which == Callable::Builtin || Which == Callable::Table)
    {
        auto Name = Builder.addConstantString(Which == Callable::Builtin ? Luau::BytecodeBuilder::StringRef{"tonumber", 8}
                                                                         : Luau::BytecodeBuilder::StringRef{"CallableTable", 13});
        Builder.emitABC(LOP_GETGLOBAL, 0, 0, 0);
        Builder.emitAux(unsigned(Name));
    }
    else
        Builder.emitAD(LOP_NEWCLOSURE, 0, Which == Callable::Original ? OriginalIndex : ReplacementIndex);
    Builder.emitAD(LOP_LOADN, 1, 5);
    auto Guard = Builder.emitLabel();
    Builder.emitAD(LOP_CMPPROTO, 0, 0);
    // A fresh VM assigns the first loaded prototype id 1. Only the Original
    // closure can take the specialized arm. UINT32_MAX forces generic fallback.
    Builder.emitAux(Match ? 1 : 0xffffffffu);
    auto Increment = Builder.addConstantNumber(2);
    Builder.emitABC(LOP_ADDK, 0, 1, uint8_t(Increment));
    if (Tuple)
    {
        Builder.emitABC(LOP_LOADNIL, 1, 0, 0);
        Builder.emitAD(LOP_LOADN, 2, 4);
    }
    auto SkipFallback = Builder.emitLabel();
    Builder.emitAD(LOP_JUMP, 0, 0);
    auto Fallback = Builder.emitLabel();
    Builder.emitABC(LOP_CALL, 0, 2, Tuple ? 4 : 2);
    auto Join = Builder.emitLabel();
    Builder.emitABC(LOP_RETURN, 0, Tuple ? 4 : 2, 0);
    if (!Builder.patchJumpD(Guard, Fallback) || !Builder.patchJumpD(SkipFallback, Join))
        throw std::runtime_error("Inline fixture jump exceeds encoding");
    Builder.endFunction(4, Nested ? 1 : 0);
    if (Nested)
    {
        auto Wrapper = Builder.beginFunction(0);
        Builder.emitAD(LOP_LOADN, 1, 99);
        Builder.emitAD(LOP_NEWCLOSURE, 0, Builder.addChildFunction(Main));
        Builder.emitABC(LOP_CAPTURE, LCT_VAL, 1, 0);
        Builder.emitABC(LOP_CALL, 0, 1, 0);
        Builder.emitABC(LOP_RETURN, 0, 0, 0);
        Builder.endFunction(4, 0);
        Main = Wrapper;
    }
    Builder.setMainFunction(Main);
    Builder.finalize();
    auto Bytes = Builder.getBytecode();
    if (InvalidRegister)
    {
        // Mutate after serialization so even an assertion-enabled fixture
        // compiler can create this intentionally invalid input safely.
        const uint32_t Words[] = {uint32_t(LOP_CMPPROTO) | (uint32_t(Fallback - Guard - 1) << 16), Match ? 1u : 0xffffffffu};
        std::string Needle;
        for (auto Word : Words)
            for (unsigned Shift = 0; Shift < 32; Shift += 8)
                Needle += char(Word >> Shift);
        auto Position = Bytes.find(Needle);
        if (Position == std::string::npos || Bytes.find(Needle, Position + 1) != std::string::npos)
            throw std::runtime_error("Cannot uniquely locate serialized guard");
        Bytes[Position + 1] = char(255);
    }
    return Bytes;
}

std::string Execute(const std::string &Bytes)
{
    if (Bytes.empty() || Bytes[0] == 0)
        throw std::runtime_error("Generated source failed compilation: " + Bytes.substr(1));
    std::unique_ptr<lua_State, decltype(&lua_close)> State(luaL_newstate(), lua_close);
    luaL_openlibs(State.get());
    lua_newtable(State.get());
    lua_newtable(State.get());
    lua_pushcfunction(
        State.get(),
        [](lua_State *Current)
        {
            lua_pushnumber(Current, luaL_checknumber(Current, 2) * 10);
            return 1;
        },
        "callable table");
    lua_setfield(State.get(), -2, "__call");
    lua_setmetatable(State.get(), -2);
    lua_setglobal(State.get(), "CallableTable");
    unsigned Budget = 100000;
    lua_callbacks(State.get())->userdata = &Budget;
    lua_callbacks(State.get())->interrupt = [](lua_State *Current, int Gc)
    {
        if (Gc < 0 && --*static_cast<unsigned *>(lua_callbacks(Current)->userdata) == 0)
            luaL_error(Current, "Inline recovery execution budget exceeded");
    };
    if (luau_load(State.get(), "inline-recovery", Bytes.data(), Bytes.size(), 0) || lua_pcall(State.get(), 0, LUA_MULTRET, 0))
        throw std::runtime_error(lua_tostring(State.get(), -1));
    std::string Values;
    for (int I = 1; I <= lua_gettop(State.get()); ++I)
    {
        if (lua_isnil(State.get(), I))
            Values += "nil;";
        else if (lua_isnumber(State.get(), I))
            Values += std::to_string(lua_tonumber(State.get(), I)) + ';';
        else
            throw std::runtime_error("Unexpected fixture result type");
    }
    return Values;
}
} // namespace

int main()
{
    unsigned Checks = 0;
    try
    {
        for (auto Which : {Callable::Original, Callable::Replacement, Callable::Builtin, Callable::Table})
            for (bool Tuple : {false, true})
                for (bool Match : {false, true})
                {
                    auto Input = GuardedInline(Which, Tuple, Match);
                    auto Expected = Execute(Input);
                    auto Value = Which == Callable::Original      ? 7
                                 : Which == Callable::Replacement ? 25
                                 : Which == Callable::Builtin     ? 5
                                                                  : 50;
                    auto Known = std::to_string(double(Value)) + ';';
                    if (Tuple)
                        Known += Which == Callable::Builtin || Which == Callable::Table ? "nil;nil;" : "nil;4.000000;";
                    if (Expected != Known)
                        throw std::runtime_error("Guarded fixture did not execute expected arm: " + Expected);
                    ++Checks;
                    for (bool Machine : {false, true})
                        for (unsigned Passes : {0u, 1u, 5u})
                        {
                            Taze::Options Settings;
                            Settings.ForceStateMachine = Machine;
                            Settings.OptimizationPasses = Passes;
                            Settings.IncludeHeader = false;
                            Settings.IncludeFunctionLocators = false;
                            auto Output = Taze::Decompile(Input, Settings);
                            if (Execute(Luau::compile(Output.Source)) != Expected)
                                throw std::runtime_error("Recovered fallback changed behavior:\n" + Output.Source);
                            ++Checks;
                            if (!Machine && Output.StateMachineFunctions != 0)
                                throw std::runtime_error("Simple guarded fallback needed a state machine");
                            ++Checks;
                        }
                }
        for (bool Machine : {false, true})
            for (bool Compact : {false, true})
                for (bool Comments : {false, true})
                {
                    auto Input = GuardedInline(Callable::Original, true, true, false, true);
                    Taze::Options Settings;
                    Settings.IncludeHeader = false;
                    Settings.ForceStateMachine = Machine;
                    Settings.CompactUpvalues = Compact;
                    Settings.IncludeLineComments = Comments;
                    Settings.IncludeUpvalueComments = Comments;
                    auto Output = Taze::Decompile(Input, Settings);
                    const std::string Marker = "-- Uninlined by Taze's Decompiler";
                    auto Position = Output.Source.find(Marker);
                    if (Position == std::string::npos || Output.Source.find(Marker, Position + 1) != std::string::npos)
                        throw std::runtime_error("Recovered function must have exactly one provenance comment:\n" + Output.Source);
                    ++Checks;
                    if (Output.Source.find("Upvalues:") != std::string::npos ||
                        Output.Source.substr(Position, Output.Source.find('\n', Position) - Position) != Marker)
                        throw std::runtime_error("Recovered function retained ordinary metadata comments:\n" + Output.Source);
                    ++Checks;
                    if (Execute(Luau::compile(Output.Source)) != Execute(Input))
                        throw std::runtime_error("Recovered nested function changed behavior:\n" + Output.Source);
                    ++Checks;
                }
        // No algorithm can distinguish these source spellings from this
        // serialized information: both complete chunks are byte-identical.
        const std::string Call = "local function twice(value) return value * 2 end; "
                                 "local function run(value) return twice(value) + 1 end; return run(...)";
        const std::string Direct = "local function twice(value) return value * 2 end; "
                                   "local function run(value) return value * 2 + 1 end; return run(...)";
        for (int Debug : {0, 1})
        {
            Luau::CompileOptions Settings;
            Settings.optimizationLevel = 2;
            Settings.debugLevel = Debug;
            if (Luau::compile(Call, Settings) != Luau::compile(Direct, Settings))
                throw std::runtime_error("Static-inlining ambiguity fixture changed with this compiler");
            ++Checks;
        }
        try
        {
            Taze::Options Settings;
            Settings.Encoding = Taze::OpcodeEncoding::Plain;
            Taze::Decompile(GuardedInline(Callable::Original, false, false, true), Settings);
            throw std::runtime_error("CMPPROTO accepted an out-of-range callable register");
        }
        catch (const Taze::Error &Error)
        {
            if (std::string(Error.what()).find("Register index out of range") == std::string::npos)
                throw;
            ++Checks;
        }
        std::cout << Checks << " inline recovery checks passed\n";
        return 0;
    }
    catch (const std::exception &Error)
    {
        std::cerr << "Inline recovery failure: " << Error.what() << '\n';
        return 1;
    }
}
