#include "Astra/Decompiler.hpp"
#include "Bytecode.hpp"
#include "Luau/Compiler.h"
#include "lua.h"
#include "lualib.h"
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
unsigned Checks = 0;

void Check(bool Ok, const std::string &Message)
{
    if (!Ok)
        throw std::runtime_error(Message);
    ++Checks;
}

void Var(std::string &Out, unsigned Value)
{
    do
    {
        unsigned Byte = Value & 127;
        Value >>= 7;
        Out += char(Byte | (Value ? 128 : 0));
    } while (Value);
}

void Word(std::string &Out, uint32_t Value)
{
    for (unsigned Shift = 0; Shift < 32; Shift += 8)
        Out += char(Value >> Shift);
}

uint32_t ABC(LuauOpcode Op, unsigned A = 0, unsigned B = 0, unsigned C = 0)
{
    return unsigned(Op) | (A << 8) | (B << 16) | (C << 24);
}

uint32_t AD(LuauOpcode Op, unsigned A, int D)
{
    return unsigned(Op) | (A << 8) | (uint32_t(uint16_t(D)) << 16);
}

std::string Constant(unsigned Tag, unsigned Value)
{
    std::string Result(1, char(Tag));
    Var(Result, Value);
    return Result;
}

struct Proto
{
    unsigned Stack = 2, Upvalues = 0;
    std::vector<uint32_t> Code{ABC(LOP_RETURN, 0, 1)};
    std::vector<std::string> Constants;
};

std::string Bytecode(const std::vector<Proto> &Protos, bool Encoded, unsigned Version = 3)
{
    std::string Result(1, char(Version));
    if (Version >= 4)
        Result += '\1'; // signature type format, with no function type data
    Var(Result, 3);
    for (const std::string &Text : {std::string("Class"), std::string("Member"), std::string("tonumber")})
    {
        Var(Result, unsigned(Text.size()));
        Result += Text;
    }
    Var(Result, unsigned(Protos.size()));
    for (const auto &P : Protos)
    {
        std::string Body;
        Body += char(P.Stack);
        Body += '\0';
        Body += char(P.Upvalues);
        Body += '\0';
        if (Version >= 4)
        {
            Body += '\0'; // flags
            Var(Body, 0); // type info size
        }
        Var(Body, unsigned(P.Code.size()));
        for (unsigned Pc = 0; Pc < P.Code.size();)
        {
            uint32_t Instruction = P.Code[Pc];
            auto Op = LuauOpcode(Instruction & 255);
            Word(Body, Encoded ? ((Instruction & ~255u) | ((unsigned(Op) * 227) & 255)) : Instruction);
            for (int Aux = 1; Aux < Luau::getOpLength(Op); ++Aux)
                Word(Body, P.Code[Pc + unsigned(Aux)]);
            Pc += unsigned(Luau::getOpLength(Op));
        }
        Var(Body, unsigned(P.Constants.size()));
        for (const auto &K : P.Constants)
            Body += K;
        Var(Body, 0); // child table
        Var(Body, 0); // line
        Var(Body, 0); // name
        Body += '\0'; // line info
        Body += '\0'; // debug info
        if (Version >= 11)
            Var(Body, 0); // feedback slots
        if (Version >= 12)
            Var(Result, unsigned(Body.size()));
        Result += Body;
    }
    Var(Result, unsigned(Protos.size() - 1));
    return Result;
}

void Reject(const std::vector<Proto> &Protos, const std::string &Expected)
{
    for (bool Encoded : {false, true})
    {
        Astra::Options Settings;
        Settings.Encoding = Encoded ? Astra::OpcodeEncoding::Roblox : Astra::OpcodeEncoding::Plain;
        try
        {
            Astra::ReadBytecode(Bytecode(Protos, Encoded), Settings);
            throw std::runtime_error("Malformed bytecode was accepted: " + Expected);
        }
        catch (const Astra::Error &Failure)
        {
            Check(std::string(Failure.what()).find(Expected) != std::string::npos,
                  "Unexpected validation error: " + std::string(Failure.what()));
        }
    }
}

double Execute(const std::string &Bytes)
{
    Check(!Bytes.empty() && Bytes[0] != 0, "Fixture failed compilation");
    std::unique_ptr<lua_State, decltype(&lua_close)> State(luaL_newstate(), lua_close);
    luaL_openlibs(State.get());
    if (luau_load(State.get(), "core-validation", Bytes.data(), Bytes.size(), 0) || lua_pcall(State.get(), 0, 1, 0))
        throw std::runtime_error(lua_tostring(State.get(), -1));
    return lua_tonumber(State.get(), -1);
}
} // namespace

int main()
{
    try
    {
        for (auto Op : {LOP_JUMP, LOP_JUMPBACK, LOP_JUMPIF, LOP_JUMPIFNOT})
        {
            Proto P;
            P.Code.insert(P.Code.begin(), AD(Op, 0, -2)); // PC -1 collides with the old no-target sentinel.
            Reject({P}, "Jump target is outside code");
        }
        {
            Proto P;
            P.Code.insert(P.Code.begin(), unsigned(LOP_JUMPX) | (uint32_t(-2) << 8));
            Reject({P}, "Jump target is outside code");
        }
        for (auto Op : {LOP_FASTCALL1, LOP_FASTCALL2, LOP_FASTCALL2K, LOP_FASTCALL3})
        {
            Proto P;
            P.Constants = {std::string(1, char(LBC_CONSTANT_NIL))};
            const bool Aux = Luau::getOpLength(Op) == 2;
            P.Code = {ABC(Op, LBF_MATH_ABS, 255, Aux ? 1 : 0)};
            if (Aux)
                P.Code.push_back(0);
            P.Code.push_back(ABC(LOP_CALL, 0, 1, 1));
            P.Code.push_back(ABC(LOP_RETURN, 0, 1));
            Reject({P}, "Register index out of range");
            if (Op == LOP_FASTCALL2 || Op == LOP_FASTCALL3)
            {
                P.Code[0] = ABC(Op, LBF_MATH_ABS, 0, 1);
                P.Code[1] = Op == LOP_FASTCALL3 ? 255u << 8 : 255;
                Reject({P}, "Register index out of range");
            }
        }
        {
            Proto P;
            P.Code = {ABC(LOP_FASTCALL2K, LBF_MATH_ABS, 0, 1), 7, ABC(LOP_CALL, 0, 1, 1), ABC(LOP_RETURN, 0, 1)};
            Reject({P}, "Constant index out of range");
        }
        {
            Proto P;
            P.Constants = {std::string(1, char(LBC_CONSTANT_NIL))};
            P.Code.insert(P.Code.begin(), AD(LOP_DUPTABLE, 0, 0));
            Reject({P}, "DUPTABLE needs a table template");
            P.Code = {AD(LOP_GETIMPORT, 0, 0), 1u << 30, ABC(LOP_RETURN, 0, 1)};
            Reject({P}, "GETIMPORT needs an import constant");
        }
        {
            Proto P;
            P.Constants = {Constant(LBC_CONSTANT_STRING, 1)};
            std::string Import(1, char(LBC_CONSTANT_IMPORT));
            Word(Import, (1u << 30) | (9u << 20));
            P.Constants.push_back(Import);
            Reject({P}, "invalid import constant path");
            P.Constants.back() = std::string(1, char(LBC_CONSTANT_IMPORT));
            Word(P.Constants.back(), 0);
            Reject({P}, "empty import constant path");
        }
        {
            Proto P;
            std::string Import(1, char(LBC_CONSTANT_IMPORT));
            Word(Import, (1u << 30) | (1u << 20));
            P.Constants = {Import, Constant(LBC_CONSTANT_STRING, 3)};
            P.Code = {AD(LOP_GETIMPORT, 0, 0), (1u << 30) | (1u << 20), AD(LOP_LOADN, 1, 27),
                      ABC(LOP_CALL, 0, 2, 2), ABC(LOP_RETURN, 0, 2)};
            for (unsigned Version = 3; Version <= 14; ++Version)
            {
                // A forward import is legal: failed load-time pre-resolution
                // leaves a nil cache and GETIMPORT then resolves its AUX path.
                Check(Execute(Bytecode({P}, false, Version)) == 27, "Legacy VM rejected a forward import");
                for (bool Encoded : {false, true})
                {
                    auto Output = Astra::Decompile(Bytecode({P}, Encoded, Version));
                    Check(Execute(Luau::compile(Output.Source)) == 27, "Legacy forward import changed behavior");
                }
            }
        }
        {
            Proto Child;
            Child.Upvalues = 1;
            Proto Parent;
            Parent.Constants = {Constant(LBC_CONSTANT_CLOSURE, 0)};
            Parent.Code = {AD(LOP_DUPCLOSURE, 0, 0), ABC(LOP_CAPTURE, LCT_REF, 1), ABC(LOP_RETURN, 0, 1)};
            Reject({Child, Parent}, "DUPCLOSURE cannot capture a reference");
        }
        {
            Proto P;
            P.Constants = {Constant(LBC_CONSTANT_STRING, 2)};
            P.Code = {ABC(LOP_NEWCLASSMEMBER, 0, 0, 255), 0, ABC(LOP_RETURN, 0, 1)};
            Reject({P}, "Register index out of range");
            P.Code = {ABC(LOP_NEWCLASS, 0, 255), 0, ABC(LOP_RETURN, 0, 1)};
            Reject({P}, "NEWCLASS needs a class shape");
            P.Constants = {Constant(LBC_CONSTANT_STRING, 1), Constant(LBC_CONSTANT_CLASS_SHAPE, 0)};
            P.Constants.back() += std::string(2, '\0');
            P.Code[0] = ABC(LOP_NEWCLASS, 0, 254);
            P.Code[1] = 1;
            Reject({P}, "Register index out of range");
            P.Constants.back() = Constant(LBC_CONSTANT_CLASS_SHAPE, 7) + std::string(2, '\0');
            Reject({P}, "invalid class name constant");
            P.Constants.back() = Constant(LBC_CONSTANT_CLASS_SHAPE, 0);
            Var(P.Constants.back(), 1);
            Var(P.Constants.back(), 0);
            Var(P.Constants.back(), 7);
            Reject({P}, "invalid class member name constant");
        }

        // Repeated short scopes produce many debug locals while keeping stack
        // pressure low. Validate value lifetimes and generic-loop outputs after
        // the same registers have already held thousands of earlier locals.
        std::string Source = "local total = 0\n";
        for (unsigned I = 1; I <= 2048; ++I)
            Source += "do local value = " + std::to_string(I) + "; total += value end\n";
        Source += "for key, value in ipairs({3, 7, 9}) do total += key * value end\nreturn total";
        for (int Optimization : {0, 1, 2})
        {
            Luau::CompileOptions Compiler;
            Compiler.optimizationLevel = Optimization;
            Compiler.debugLevel = 2;
            auto Input = Luau::compile(Source, Compiler);
            for (bool Native : {false, true})
            {
                Astra::Options Settings;
                Settings.NativeCaptures = Native;
                // Retain the large symbol set once to cover repeated debug
                // names and collision suffixes during source finalization.
                if (Optimization == 0 && !Native)
                    Settings.OptimizationPasses = 0;
                auto Output = Astra::Decompile(Input, Settings);
                Check(Execute(Luau::compile(Output.Source)) == Execute(Input), "Scoped debug local values changed");
            }
        }
        std::cout << Checks << " core validation checks passed\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "Core validation failure: " << Failure.what() << '\n';
        return 1;
    }
}
