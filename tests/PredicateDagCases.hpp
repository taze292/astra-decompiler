#pragma once

#include "Luau/BytecodeBuilder.h"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

struct PredicateDagFixture
{
    std::string Name;
    std::string Bytes;
    std::string Expected;
};

// Run the integrated VM round-trip corpus with:
//   build/Release/astra-control-flow-tests.exe bytecode_predicate_dag_
// The independent oracle walks graph edges directly; it does not repeat any
// decompiler expression-rewrite rules. __eq records every comparison visit, so
// evaluating a predicate twice or in a different order changes the result.
inline std::vector<PredicateDagFixture> PredicateDagCases()
{
    std::vector<PredicateDagFixture> Result;
    std::uint32_t Seed = 0x5791b;
    auto Random = [&]() { return Seed = Seed * 1664525u + 1013904223u; };
    auto Patch = [](Luau::BytecodeBuilder &Builder, std::size_t From, std::size_t To)
    {
        if (!Builder.patchJumpD(From, To))
            throw std::runtime_error("Predicate DAG fixture jump overflow");
    };
    for (unsigned Graph = 0; Graph < 12; ++Graph)
    {
        unsigned Count = 4 + Graph % 4;
        std::vector<unsigned> Taken;
        for (unsigned I = 0; I < Count; ++I)
            Taken.push_back(I + 1 + Random() % (Count + 1 - I));
        // Exercise a real internal taken edge and both statement destinations.
        Taken[0] = 2;
        Taken[Count - 1] = Count + 1;
        unsigned All = (1u << Count) - 1;
        std::vector<unsigned> Masks{0, All, Random() & All, Random() & All};
        for (unsigned Variant = 0; Variant < Masks.size(); ++Variant)
        {
            unsigned Mask = Masks[Variant], Destination = 0;
            std::uint64_t Trace = 0;
            while (Destination < Count)
            {
                Trace = Trace * 11 + Destination + 1;
                Destination = Mask & (1u << Destination) ? Taken[Destination] : Destination + 1;
            }
            Luau::BytecodeBuilder Builder;
            auto Equal = Builder.beginFunction(2);
            auto Id = Builder.addConstantString({"id", 2});
            auto Flag = Builder.addConstantString({"flag", 4});
            auto Eleven = Builder.addConstantNumber(11);
            Builder.emitABC(LOP_GETUPVAL, 2, 0, 0);
            Builder.emitABC(LOP_MULK, 2, 2, std::uint8_t(Eleven));
            Builder.emitABC(LOP_GETTABLEKS, 3, 0, 0);
            Builder.emitAux(unsigned(Id));
            Builder.emitABC(LOP_ADD, 2, 2, 3);
            Builder.emitABC(LOP_SETUPVAL, 2, 0, 0);
            Builder.emitABC(LOP_GETTABLEKS, 0, 0, 0);
            Builder.emitAux(unsigned(Flag));
            Builder.emitABC(LOP_RETURN, 0, 2, 0);
            Builder.endFunction(4, 1);

            auto Main = Builder.beginFunction(0);
            auto EqKey = Builder.addConstantString({"__eq", 4});
            auto IdKey = Builder.addConstantString({"id", 2});
            auto FlagKey = Builder.addConstantString({"flag", 4});
            auto SetMetatable = Builder.addConstantString({"setmetatable", 12});
            auto Scratch = std::uint8_t(Count + 3);
            auto Table = [&](std::uint8_t Register)
            {
                Builder.emitABC(LOP_NEWTABLE, Register, 0, 0);
                Builder.emitAux(0);
            };
            auto SetMeta = [&](std::uint8_t Register)
            {
                Builder.emitABC(LOP_GETGLOBAL, Scratch, 0, 0);
                Builder.emitAux(unsigned(SetMetatable));
                Builder.emitABC(LOP_MOVE, Scratch + 1, Register, 0);
                Builder.emitABC(LOP_MOVE, Scratch + 2, 1, 0);
                Builder.emitABC(LOP_CALL, Scratch, 3, 1);
            };
            Builder.emitAD(LOP_LOADN, 0, 0); // shared comparison trace, below call scratch
            Table(1);
            Builder.emitAD(LOP_NEWCLOSURE, Scratch, Builder.addChildFunction(Equal));
            Builder.emitABC(LOP_CAPTURE, LCT_REF, 0, 0);
            Builder.emitABC(LOP_SETTABLEKS, Scratch, 1, 0);
            Builder.emitAux(unsigned(EqKey));
            Table(2);
            SetMeta(2);
            for (unsigned I = 0; I < Count; ++I)
            {
                auto Object = std::uint8_t(I + 3);
                Table(Object);
                Builder.emitAD(LOP_LOADN, Scratch, std::int16_t(I + 1));
                Builder.emitABC(LOP_SETTABLEKS, Scratch, Object, 0);
                Builder.emitAux(unsigned(IdKey));
                Builder.emitABC(LOP_LOADB, Scratch, (Mask >> I) & 1, 0);
                Builder.emitABC(LOP_SETTABLEKS, Scratch, Object, 0);
                Builder.emitAux(unsigned(FlagKey));
                SetMeta(Object);
            }
            std::vector<std::size_t> Labels;
            for (unsigned I = 0; I < Count; ++I)
            {
                Labels.push_back(Builder.emitLabel());
                Builder.emitAD(LOP_JUMPIFEQ, std::uint8_t(I + 3), 0);
                Builder.emitAux(2);
            }
            Labels.push_back(Builder.emitLabel());
            Builder.emitABC(LOP_LOADB, 1, 0, 0);
            auto JoinFalse = Builder.emitLabel();
            Builder.emitAD(LOP_JUMP, 0, 0);
            Labels.push_back(Builder.emitLabel());
            Builder.emitABC(LOP_LOADB, 1, 1, 0);
            auto Join = Builder.emitLabel();
            Builder.emitABC(LOP_CLOSEUPVALS, 0, 0, 0);
            Builder.emitABC(LOP_RETURN, 0, 3, 0);
            for (unsigned I = 0; I < Count; ++I)
                Patch(Builder, Labels[I], Labels[Taken[I]]);
            Patch(Builder, JoinFalse, Join);
            Builder.endFunction(Scratch + 3, 0);
            Builder.setMainFunction(Main);
            Builder.finalize();
            std::string Value = std::to_string(Trace);
            Result.push_back({"bytecode_predicate_dag_" + std::to_string(Graph) + "_" + std::to_string(Variant), Builder.getBytecode(),
                              "3:" + std::to_string(Value.size()) + ":" + Value + (Destination == Count ? ";1:false;" : ";1:true;")});
        }
    }
    return Result;
}
