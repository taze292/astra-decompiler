#pragma once

#include "Luau/BytecodeBuilder.h"
#include <stdexcept>
#include <string>
#include <vector>

struct ThreadedLoopFixture
{
    std::string Name;
    std::string Bytes;
    std::string Expected;
};

inline std::vector<ThreadedLoopFixture> ThreadedLoopCases()
{
    std::vector<ThreadedLoopFixture> Result;
    auto Patch = [](Luau::BytecodeBuilder &Builder, std::size_t From, std::size_t To)
    {
        if (!Builder.patchJumpD(From, To))
            throw std::runtime_error("Threaded loop fixture jump overflow");
    };
    {
        // An early continue owns the direct JUMPBACK; the final latch jumps to
        // that instruction. Choosing only direct predecessors truncates the loop.
        Luau::BytecodeBuilder Builder;
        auto Main = Builder.beginFunction(0);
        auto Zero = Builder.addConstantNumber(0);
        auto One = Builder.addConstantNumber(1);
        auto Two = Builder.addConstantNumber(2);
        Builder.emitAD(LOP_LOADN, 0, 0); // counter
        Builder.emitAD(LOP_LOADN, 1, 0); // total
        Builder.emitAD(LOP_LOADN, 2, 5); // limit
        auto Header = Builder.emitLabel();
        auto Guard = Builder.emitLabel();
        Builder.emitAD(LOP_JUMPIFNOTLT, 0, 0);
        Builder.emitAux(2);
        Builder.emitABC(LOP_ADDK, 0, 0, std::uint8_t(One));
        Builder.emitABC(LOP_MODK, 3, 0, std::uint8_t(Two));
        auto Even = Builder.emitLabel();
        Builder.emitAD(LOP_JUMPXEQKN, 3, 0);
        Builder.emitAux(unsigned(Zero));
        auto Odd = Builder.emitLabel();
        Builder.emitAD(LOP_JUMP, 0, 0);
        auto EarlyLatch = Builder.emitLabel();
        Builder.emitAD(LOP_JUMPBACK, 0, 0);
        auto Body = Builder.emitLabel();
        Builder.emitABC(LOP_ADD, 1, 1, 0);
        auto FinalLatch = Builder.emitLabel();
        Builder.emitAD(LOP_JUMP, 0, 0);
        auto Exit = Builder.emitLabel();
        Builder.emitABC(LOP_RETURN, 0, 3, 0);
        Patch(Builder, Guard, Exit);
        Patch(Builder, Even, EarlyLatch);
        Patch(Builder, Odd, Body);
        Patch(Builder, EarlyLatch, Header);
        Patch(Builder, FinalLatch, EarlyLatch);
        Builder.endFunction(4, 0);
        Builder.setMainFunction(Main);
        Builder.finalize();
        Result.push_back({"bytecode_threaded_backedge", Builder.getBytecode(), "3:1:5;3:1:9;"});
    }
    {
        // Earlier backedges skip the final condition. A repeat statement's
        // continue evaluates its until expression and would exit on the first
        // iteration because zero is truthy in Luau.
        Luau::BytecodeBuilder Builder;
        auto Main = Builder.beginFunction(0);
        auto One = Builder.addConstantNumber(1);
        Builder.emitAD(LOP_LOADN, 0, 0); // counter
        Builder.emitAD(LOP_LOADN, 1, 0); // total
        Builder.emitAD(LOP_LOADN, 2, 0); // truthy nonboolean final predicate
        auto Header = Builder.emitLabel();
        Builder.emitABC(LOP_ADDK, 0, 0, std::uint8_t(One));
        Builder.emitABC(LOP_ADD, 1, 1, 0);
        Builder.emitAD(LOP_LOADN, 3, 5);
        auto EarlierLatch = Builder.emitLabel();
        Builder.emitAD(LOP_JUMPIFLT, 0, 0);
        Builder.emitAux(3);
        auto FinalLatch = Builder.emitLabel();
        Builder.emitAD(LOP_JUMPIFNOT, 2, 0);
        Builder.emitABC(LOP_RETURN, 0, 4, 0);
        Patch(Builder, EarlierLatch, Header);
        Patch(Builder, FinalLatch, Header);
        Builder.endFunction(4, 0);
        Builder.setMainFunction(Main);
        Builder.finalize();
        Result.push_back({"bytecode_compound_conditional_latches", Builder.getBytecode(), "3:1:5;3:2:15;3:1:0;"});
    }
    for (unsigned Variant = 0; Variant < 3; ++Variant)
    {
        // FORNPREP's exit is threaded past the lexical loop epilogue and an
        // alternate branch. FORNLOOP is therefore not immediately before it.
        Luau::BytecodeBuilder Builder;
        auto Main = Builder.beginFunction(0);
        Builder.emitAD(LOP_LOADN, 0, 0); // total, below the loop's hidden triple
        Builder.emitAD(LOP_LOADN, 1, Variant == 1 ? 0 : 3);
        Builder.emitAD(LOP_LOADN, 2, 1);
        Builder.emitAD(LOP_LOADN, 3, 1);
        Builder.emitABC(LOP_LOADB, 4, Variant == 2 ? 0 : 1, 0);
        auto Choose = Builder.emitLabel();
        Builder.emitAD(LOP_JUMPIFNOT, 4, 0);
        auto Prepare = Builder.emitLabel();
        Builder.emitAD(LOP_FORNPREP, 1, 0);
        auto Body = Builder.emitLabel();
        Builder.emitABC(LOP_ADD, 0, 0, 3);
        auto Tail = Builder.emitLabel();
        Builder.emitAD(LOP_FORNLOOP, 1, 0);
        auto Epilogue = Builder.emitLabel();
        Builder.emitAD(LOP_JUMP, 0, 0);
        auto Else = Builder.emitLabel();
        Builder.emitAD(LOP_LOADN, 0, 99);
        auto Exit = Builder.emitLabel();
        Builder.emitABC(LOP_RETURN, 0, 2, 0);
        Patch(Builder, Choose, Else);
        Patch(Builder, Prepare, Exit);
        Patch(Builder, Tail, Body);
        Patch(Builder, Epilogue, Exit);
        Builder.endFunction(5, 0);
        Builder.setMainFunction(Main);
        Builder.finalize();
        Result.push_back({"bytecode_threaded_numeric_exit_" + std::to_string(Variant), Builder.getBytecode(),
                          Variant == 2   ? "3:2:99;"
                          : Variant == 1 ? "3:1:0;"
                                         : "3:1:6;"});
    }
    for (bool Generic : {false, true})
        for (bool DistinctExit : {false, true})
        {
            Luau::BytecodeBuilder Builder;
            unsigned Iterator = 0;
            if (Generic)
            {
                Iterator = Builder.beginFunction(2);
                auto One = Builder.addConstantNumber(1);
                Builder.emitAD(LOP_LOADN, 2, 3);
                auto Done = Builder.emitLabel();
                Builder.emitAD(LOP_JUMPIFNOTLT, 1, 0);
                Builder.emitAux(2);
                Builder.emitABC(LOP_ADDK, 0, 1, std::uint8_t(One));
                Builder.emitABC(LOP_RETURN, 0, 2, 0);
                auto Exit = Builder.emitLabel();
                Builder.emitABC(LOP_LOADNIL, 0, 0, 0);
                Builder.emitABC(LOP_RETURN, 0, 2, 0);
                Patch(Builder, Done, Exit);
                Builder.endFunction(3, 0);
            }
            auto Main = Builder.beginFunction(0);
            auto Three = Builder.addConstantNumber(3);
            Builder.emitAD(LOP_LOADN, 0, 0); // total stays below iterator/call scratch
            if (Generic)
            {
                Builder.emitAD(LOP_NEWCLOSURE, 1, Builder.addChildFunction(Iterator));
                Builder.emitABC(LOP_LOADNIL, 2, 0, 0);
                Builder.emitAD(LOP_LOADN, 3, 0);
            }
            else
            {
                Builder.emitAD(LOP_LOADN, 1, 3);
                Builder.emitAD(LOP_LOADN, 2, 1);
                Builder.emitAD(LOP_LOADN, 3, 1);
            }
            auto Prepare = Builder.emitLabel();
            Builder.emitAD(Generic ? LOP_FORGPREP : LOP_FORNPREP, 1, 0);
            auto Body = Builder.emitLabel();
            auto Value = std::uint8_t(Generic ? 4 : 3);
            Builder.emitABC(LOP_ADD, 0, 0, Value);
            auto FinalIteration = Builder.emitLabel();
            Builder.emitAD(LOP_JUMPXEQKN, Value, 0);
            Builder.emitAux(unsigned(Three));
            auto OrdinaryIteration = Builder.emitLabel();
            Builder.emitAD(LOP_JUMP, 0, 0);
            auto EarlyTail = Builder.emitLabel();
            Builder.emitAD(Generic ? LOP_FORGLOOP : LOP_FORNLOOP, 1, 0);
            if (Generic)
                Builder.emitAux(1);
            // Only the irregular variant runs this effect when the early tail
            // exhausts. Replacing that tail with continue would silently lose it.
            if (DistinctExit)
                Builder.emitAD(LOP_LOADN, 0, 99);
            auto EarlyExit = Builder.emitLabel();
            Builder.emitAD(LOP_JUMP, 0, 0);
            auto Tail = Builder.emitLabel();
            Builder.emitAD(Generic ? LOP_FORGLOOP : LOP_FORNLOOP, 1, 0);
            if (Generic)
                Builder.emitAux(1);
            auto Exit = Builder.emitLabel();
            Builder.emitABC(LOP_RETURN, 0, 2, 0);
            Patch(Builder, Prepare, Generic ? Tail : Exit);
            Patch(Builder, FinalIteration, EarlyTail);
            Patch(Builder, OrdinaryIteration, Tail);
            Patch(Builder, EarlyTail, Body);
            Patch(Builder, EarlyExit, Exit);
            Patch(Builder, Tail, Body);
            Builder.endFunction(Generic ? 7 : 4, 0);
            Builder.setMainFunction(Main);
            Builder.finalize();
            std::string Name = DistinctExit ? "bytecode_distinct_" : "bytecode_threaded_";
            Name += Generic ? "generic" : "numeric";
            Name += DistinctExit ? "_tail_exit" : "_duplicate_tail";
            Result.push_back({Name, Builder.getBytecode(), DistinctExit ? "3:2:99;" : "3:1:6;"});
        }
    for (unsigned Changed = 1; Changed <= 3; ++Changed)
    {
        // Obfuscated bytecode may mutate the native numeric loop's hidden
        // limit, step, or index. A source for loop snapshots limit/step and owns
        // its index, so these cases must retain explicit register semantics.
        Luau::BytecodeBuilder Builder;
        auto Main = Builder.beginFunction(0);
        Builder.emitAD(LOP_LOADN, 0, 0);
        Builder.emitAD(LOP_LOADN, 1, 3);
        Builder.emitAD(LOP_LOADN, 2, 1);
        Builder.emitAD(LOP_LOADN, 3, 1);
        auto Prepare = Builder.emitLabel();
        Builder.emitAD(LOP_FORNPREP, 1, 0);
        auto Body = Builder.emitLabel();
        Builder.emitABC(LOP_ADD, 0, 0, 3);
        Builder.emitAD(LOP_LOADN, std::uint8_t(Changed), Changed == 1 ? 1 : Changed == 2 ? 4 : 3);
        auto Tail = Builder.emitLabel();
        Builder.emitAD(LOP_FORNLOOP, 1, 0);
        auto Exit = Builder.emitLabel();
        Builder.emitABC(LOP_RETURN, 0, 2, 0);
        Patch(Builder, Prepare, Exit);
        Patch(Builder, Tail, Body);
        Builder.endFunction(4, 0);
        Builder.setMainFunction(Main);
        Builder.finalize();
        Result.push_back({"bytecode_modified_numeric_control_" + std::to_string(Changed), Builder.getBytecode(), "3:1:1;"});
    }
    {
        Luau::BytecodeBuilder Builder;
        auto Main = Builder.beginFunction(0);
        auto One = Builder.addConstantNumber(1);
        Builder.emitAD(LOP_LOADN, 0, 0);
        Builder.emitAD(LOP_LOADN, 1, 3);
        auto Header = Builder.emitLabel();
        Builder.emitAD(LOP_JUMPIFNOTLT, 0, 0);
        Builder.emitAux(1);
        auto Body = Builder.emitLabel();
        Builder.emitABC(LOP_ADDK, 0, 0, std::uint8_t(One));
        auto Tail = Builder.emitLabel();
        Builder.emitAD(LOP_JUMPBACK, 0, 0);
        auto Exit = Builder.emitLabel();
        Builder.emitABC(LOP_RETURN, 0, 2, 0);
        // This predecessor cannot execute. Reverse natural-loop closure must
        // not grow the loop beyond its return merely because this edge exists.
        auto Dead = Builder.emitLabel();
        Builder.emitAD(LOP_JUMPBACK, 0, 0);
        Patch(Builder, Header, Exit);
        Patch(Builder, Tail, Header);
        Patch(Builder, Dead, Body);
        Builder.endFunction(2, 0);
        Builder.setMainFunction(Main);
        Builder.finalize();
        Result.push_back({"bytecode_threaded_unreachable_loop_predecessor", Builder.getBytecode(), "3:1:3;"});
    }
    for (bool Generic : {false, true})
        for (bool Match : {false, true})
            for (unsigned NormalWrite : {0u, 1u, 2u})
            {
                Luau::BytecodeBuilder Builder;
                unsigned Iterator = 0;
                if (Generic)
                {
                    Iterator = Builder.beginFunction(2);
                    auto One = Builder.addConstantNumber(1);
                    Builder.emitAD(LOP_LOADN, 2, 3);
                    auto Done = Builder.emitLabel();
                    Builder.emitAD(LOP_JUMPIFNOTLT, 1, 0);
                    Builder.emitAux(2);
                    Builder.emitABC(LOP_ADDK, 0, 1, std::uint8_t(One));
                    Builder.emitABC(LOP_RETURN, 0, 2, 0);
                    auto Exit = Builder.emitLabel();
                    Builder.emitABC(LOP_LOADNIL, 0, 0, 0);
                    Builder.emitABC(LOP_RETURN, 0, 2, 0);
                    Patch(Builder, Done, Exit);
                    Builder.endFunction(3, 0);
                }
                auto Main = Builder.beginFunction(0);
                auto Needle = Builder.addConstantNumber(Match ? 2 : 7);
                if (Generic)
                {
                    Builder.emitAD(LOP_NEWCLOSURE, 1, Builder.addChildFunction(Iterator));
                    Builder.emitABC(LOP_LOADNIL, 2, 0, 0);
                    Builder.emitAD(LOP_LOADN, 3, 0);
                }
                else
                {
                    Builder.emitAD(LOP_LOADN, 1, 3);
                    Builder.emitAD(LOP_LOADN, 2, 1);
                    Builder.emitAD(LOP_LOADN, 3, 1);
                }
                auto Prepare = Builder.emitLabel();
                Builder.emitAD(Generic ? LOP_FORGPREP : LOP_FORNPREP, 1, 0);
                auto Body = Builder.emitLabel();
                Builder.emitAD(LOP_JUMPXEQKN, std::uint8_t(Generic ? 4 : 3), 0);
                Builder.emitAux(unsigned(Needle) | (1u << 31));
                Builder.emitABC(LOP_LOADB, 0, 1, 0);
                auto Matched = Builder.emitLabel();
                Builder.emitAD(LOP_JUMP, 0, 0);
                auto NotMatched = Builder.emitLabel();
                if (NormalWrite)
                    Builder.emitABC(LOP_LOADB, 0, 1, 0);
                auto NormalBreak = Builder.emitLabel();
                if (NormalWrite == 2)
                    Builder.emitAD(LOP_JUMP, 0, 0);
                auto Tail = Builder.emitLabel();
                Builder.emitAD(Generic ? LOP_FORGLOOP : LOP_FORNLOOP, 1, 0);
                if (Generic)
                    Builder.emitAux(1);
                auto Exhausted = Builder.emitLabel();
                Builder.emitABC(LOP_LOADB, 0, 0, 0);
                auto Join = Builder.emitLabel();
                // A real continuation after the skipped default prevents the
                // early-exit branch from being reduced to a direct return alone.
                Builder.emitABC(LOP_NOT, 0, 0, 0);
                Builder.emitABC(LOP_RETURN, 0, 2, 0);
                Patch(Builder, Prepare, Generic ? Tail : Exhausted);
                Patch(Builder, Body, NotMatched);
                Patch(Builder, Matched, Join);
                Patch(Builder, Tail, Body);
                if (NormalWrite == 2)
                    Patch(Builder, NormalBreak, Exhausted);
                Builder.endFunction(Generic ? 7 : 4, 0);
                Builder.setMainFunction(Main);
                Builder.finalize();
                Result.push_back({std::string(NormalWrite ? "bytecode_modified_" : "bytecode_threaded_") +
                                      (Generic ? "generic" : "numeric") + "_skipped_default_" + (Match ? "match" : "miss") +
                                      (NormalWrite == 2 ? "_early_break" : ""),
                                  Builder.getBytecode(), Match && NormalWrite != 2 ? "1:false;" : "1:true;"});
            }
    for (bool Early : {false, true})
    {
        // A repeat header writes the same register as the exhaustion default.
        // Hoisting that default would retain 7 instead of 9 on normal exit.
        Luau::BytecodeBuilder Builder;
        auto Main = Builder.beginFunction(0);
        auto One = Builder.addConstantNumber(1);
        auto Nine = Builder.addConstantNumber(9);
        Builder.emitAD(LOP_LOADN, 0, 0);
        Builder.emitABC(LOP_LOADB, 1, Early ? 1 : 0, 0);
        Builder.emitAD(LOP_LOADN, 3, 3);
        auto Header = Builder.emitLabel();
        Builder.emitAD(LOP_LOADN, 2, 7);
        Builder.emitABC(LOP_ADDK, 0, 0, std::uint8_t(One));
        auto Choice = Builder.emitLabel();
        Builder.emitAD(LOP_JUMPIFNOT, 1, 0);
        auto EarlyExit = Builder.emitLabel();
        Builder.emitAD(LOP_JUMP, 0, 0);
        auto Check = Builder.emitLabel();
        Builder.emitAD(LOP_JUMPIFLT, 0, 0);
        Builder.emitAux(3);
        Builder.emitAD(LOP_LOADN, 2, 9);
        auto Join = Builder.emitLabel();
        Builder.emitAD(LOP_JUMPXEQKN, 2, 0);
        Builder.emitAux(unsigned(Nine) | (1u << 31));
        Builder.emitABC(LOP_ADDK, 2, 2, std::uint8_t(One));
        auto Done = Builder.emitLabel();
        Builder.emitABC(LOP_RETURN, 2, 2, 0);
        Patch(Builder, Choice, Check);
        Patch(Builder, EarlyExit, Join);
        Patch(Builder, Check, Header);
        Patch(Builder, Join, Done);
        Builder.endFunction(4, 0);
        Builder.setMainFunction(Main);
        Builder.finalize();
        Result.push_back({std::string("bytecode_threaded_natural_header_default_") + (Early ? "early" : "normal"),
                          Builder.getBytecode(), Early ? "3:1:7;" : "3:2:10;"});
    }
    return Result;
}
