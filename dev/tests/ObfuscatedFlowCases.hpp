#pragma once

#include "Luau/BytecodeBuilder.h"
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct ObfuscatedBytecodeFixture
{
    std::string Name, Bytes, Expected;
};

inline std::vector<ObfuscatedBytecodeFixture> ObfuscatedJumpIslandCases()
{
    std::vector<ObfuscatedBytecodeFixture> Result;
    for (unsigned Variant = 0; Variant < 2; ++Variant)
    {
        Luau::BytecodeBuilder Builder;
        const auto Main = Builder.beginFunction(0);
        Builder.emitABC(LOP_LOADB, 0, Variant, 0);
        const auto Skip = Builder.emitLabel();
        Builder.emitAD(LOP_JUMP, 0, 0);
        // Long branches use an island physically before the comparison. This
        // backward conditional edge is a forward branch, not a loop backedge.
        const auto Island = Builder.emitLabel();
        Builder.emitAD(LOP_JUMP, 0, 0);
        const auto Test = Builder.emitLabel();
        Builder.emitAD(LOP_JUMPIFNOT, 0, 0);
        Builder.emitAD(LOP_LOADN, 1, 10);
        const auto Join = Builder.emitLabel();
        Builder.emitAD(LOP_JUMP, 0, 0);
        const auto Other = Builder.emitLabel();
        Builder.emitAD(LOP_LOADN, 1, 20);
        const auto Exit = Builder.emitLabel();
        Builder.emitABC(LOP_RETURN, 1, 2, 0);
        if (!Builder.patchJumpD(Skip, Test) || !Builder.patchJumpD(Island, Other) || !Builder.patchJumpD(Test, Island) ||
            !Builder.patchJumpD(Join, Exit))
            throw std::runtime_error("Jump island fixture overflow");
        Builder.endFunction(2, 0);
        Builder.setMainFunction(Main);
        Builder.finalize();
        Result.push_back(
            {"bytecode_threaded_jump_island_" + std::to_string(Variant), Builder.getBytecode(), Variant ? "3:2:10;" : "3:2:20;"});
    }
    {
        Luau::BytecodeBuilder Builder;
        const auto Main = Builder.beginFunction(0);
        const auto Zero = Builder.addConstantNumber(0);
        const auto One = Builder.addConstantNumber(1);
        const auto Two = Builder.addConstantNumber(2);
        const auto Ten = Builder.addConstantNumber(10);
        const auto Twenty = Builder.addConstantNumber(20);
        Builder.emitAD(LOP_LOADN, 0, 0);
        Builder.emitAD(LOP_LOADN, 1, 0);
        Builder.emitAD(LOP_LOADN, 2, 3);
        const auto Header = Builder.emitLabel();
        Builder.emitAD(LOP_JUMPIFNOTLT, 0, 0);
        Builder.emitAux(2);
        Builder.emitABC(LOP_ADDK, 0, 0, std::uint8_t(One));
        Builder.emitABC(LOP_MODK, 3, 0, std::uint8_t(Two));
        const auto Skip = Builder.emitLabel();
        Builder.emitAD(LOP_JUMP, 0, 0);
        const auto Island = Builder.emitLabel();
        Builder.emitAD(LOP_JUMP, 0, 0);
        const auto Test = Builder.emitLabel();
        Builder.emitAD(LOP_JUMPXEQKN, 3, 0);
        Builder.emitAux(unsigned(Zero));
        Builder.emitABC(LOP_ADDK, 1, 1, std::uint8_t(Ten));
        const auto Join = Builder.emitLabel();
        Builder.emitAD(LOP_JUMP, 0, 0);
        const auto Other = Builder.emitLabel();
        Builder.emitABC(LOP_ADDK, 1, 1, std::uint8_t(Twenty));
        const auto Latch = Builder.emitLabel();
        Builder.emitAD(LOP_JUMPBACK, 0, 0);
        const auto Exit = Builder.emitLabel();
        Builder.emitABC(LOP_RETURN, 0, 3, 0);
        if (!Builder.patchJumpD(Header, Exit) || !Builder.patchJumpD(Skip, Test) || !Builder.patchJumpD(Island, Other) ||
            !Builder.patchJumpD(Test, Island) || !Builder.patchJumpD(Join, Latch) || !Builder.patchJumpD(Latch, Header))
            throw std::runtime_error("Loop jump island fixture overflow");
        Builder.endFunction(4, 0);
        Builder.setMainFunction(Main);
        Builder.finalize();
        Result.push_back({"bytecode_threaded_jump_island_in_loop", Builder.getBytecode(), "3:1:3;3:2:40;"});
    }
    for (bool Generic : {false, true})
        for (bool Match : {false, true})
            for (bool Threaded : {false, true})
            {
                Luau::BytecodeBuilder Builder;
                auto Patch = [&](std::size_t From, std::size_t To)
                {
                    if (!Builder.patchJumpD(From, To))
                        throw std::runtime_error("Loop continuation fixture overflow");
                };
                unsigned Iterator = 0;
                if (Generic)
                {
                    Iterator = Builder.beginFunction(2);
                    const auto One = Builder.addConstantNumber(1);
                    Builder.emitAD(LOP_LOADN, 2, 3);
                    const auto Done = Builder.emitLabel();
                    Builder.emitAD(LOP_JUMPIFNOTLT, 1, 0);
                    Builder.emitAux(2);
                    Builder.emitABC(LOP_ADDK, 0, 1, std::uint8_t(One));
                    Builder.emitABC(LOP_RETURN, 0, 2, 0);
                    const auto Exit = Builder.emitLabel();
                    Builder.emitABC(LOP_LOADNIL, 0, 0, 0);
                    Builder.emitABC(LOP_RETURN, 0, 2, 0);
                    Patch(Done, Exit);
                    Builder.endFunction(3, 0);
                }
                const auto Main = Builder.beginFunction(0);
                const auto One = Builder.addConstantNumber(1);
                const auto Needle = Builder.addConstantNumber(Match ? 2 : 7);
                Builder.emitAD(LOP_LOADN, 0, 99);
                if (Generic)
                {
                    Builder.emitAD(LOP_NEWCLOSURE, 2, Builder.addChildFunction(Iterator));
                    Builder.emitABC(LOP_LOADNIL, 3, 0, 0);
                    Builder.emitAD(LOP_LOADN, 4, 0);
                }
                else
                {
                    Builder.emitAD(LOP_LOADN, 2, 3);
                    Builder.emitAD(LOP_LOADN, 3, 1);
                    Builder.emitAD(LOP_LOADN, 4, 1);
                }
                const auto Prep = Builder.emitLabel();
                Builder.emitAD(Generic ? LOP_FORGPREP : LOP_FORNPREP, 2, 0);
                const auto Body = Builder.emitLabel();
                const auto Value = std::uint8_t(Generic ? 5 : 4);
                Builder.emitAD(LOP_JUMPXEQKN, Value, 0);
                Builder.emitAux(unsigned(Needle) | (1u << 31));
                Builder.emitABC(LOP_MOVE, 1, Value, 0);
                const auto Found = Builder.emitLabel();
                Builder.emitAD(LOP_JUMP, 0, 0);
                const auto Tail = Builder.emitLabel();
                Builder.emitAD(Generic ? LOP_FORGLOOP : LOP_FORNLOOP, 2, 0);
                if (Generic)
                    Builder.emitAux(1);
                const auto Exhausted = Builder.emitLabel();
                std::size_t Skip = 0;
                if (Threaded)
                {
                    Skip = Builder.emitLabel();
                    Builder.emitAD(LOP_JUMP, 0, 0);
                    // These intervening instructions belong to another path;
                    // the loop's normal exit must never inspect or hoist them.
                    Builder.emitAD(LOP_LOADN, 1, 777);
                    Builder.emitAD(LOP_LOADN, 0, 555);
                    const auto Other = Builder.emitLabel();
                    Builder.emitAD(LOP_JUMP, 0, 0);
                    const auto Default = Builder.emitLabel();
                    Builder.emitABC(LOP_MOVE, 1, 0, 0);
                    const auto Join = Builder.emitLabel();
                    Builder.emitABC(LOP_ADDK, 1, 1, std::uint8_t(One));
                    Builder.emitABC(LOP_RETURN, 1, 2, 0);
                    Patch(Skip, Default);
                    Patch(Other, Join);
                    Patch(Found, Join);
                }
                else
                {
                    Builder.emitABC(LOP_MOVE, 1, 0, 0);
                    const auto Join = Builder.emitLabel();
                    Builder.emitABC(LOP_ADDK, 1, 1, std::uint8_t(One));
                    Builder.emitABC(LOP_RETURN, 1, 2, 0);
                    Patch(Found, Join);
                }
                Patch(Prep, Generic ? Tail : Exhausted);
                Patch(Body, Tail);
                Patch(Tail, Body);
                Builder.endFunction(8, 0);
                Builder.setMainFunction(Main);
                Builder.finalize();
                Result.push_back({std::string("bytecode_threaded_") + (Generic ? "generic" : "numeric") + "_move_default_" +
                                      (Match ? "match" : "miss") + (Threaded ? "_island" : ""),
                                  Builder.getBytecode(), Match ? "3:1:3;" : "3:3:100;"});
            }
    for (bool Generic : {false, true})
        for (unsigned Variant = 0; Variant < 3; ++Variant)
        {
            Luau::BytecodeBuilder Builder;
            auto Patch = [&](std::size_t From, std::size_t To)
            {
                if (!Builder.patchJumpD(From, To))
                    throw std::runtime_error("Effectful loop continuation fixture overflow");
            };
            const auto Effect = Builder.beginFunction(0);
            const auto One = Builder.addConstantNumber(1);
            Builder.emitABC(LOP_GETUPVAL, 0, 0, 0);
            Builder.emitABC(LOP_ADDK, 0, 0, std::uint8_t(One));
            Builder.emitABC(LOP_SETUPVAL, 0, 0, 0);
            Builder.emitABC(LOP_RETURN, 0, 1, 0);
            Builder.endFunction(1, 1);
            unsigned Iterator = 0;
            if (Generic)
            {
                Iterator = Builder.beginFunction(2);
                const auto Increment = Builder.addConstantNumber(1);
                Builder.emitAD(LOP_LOADN, 2, 3);
                const auto Done = Builder.emitLabel();
                Builder.emitAD(LOP_JUMPIFNOTLT, 1, 0);
                Builder.emitAux(2);
                Builder.emitABC(LOP_ADDK, 0, 1, std::uint8_t(Increment));
                Builder.emitABC(LOP_RETURN, 0, 2, 0);
                const auto Exit = Builder.emitLabel();
                Builder.emitABC(LOP_LOADNIL, 0, 0, 0);
                Builder.emitABC(LOP_RETURN, 0, 2, 0);
                Patch(Done, Exit);
                Builder.endFunction(3, 0);
            }
            const auto Main = Builder.beginFunction(0);
            const auto Needle = Builder.addConstantNumber(Variant == 1 ? 7 : 2);
            const auto First = Builder.addConstantNumber(1);
            Builder.emitAD(LOP_LOADN, 0, 0);
            Builder.emitAD(LOP_NEWCLOSURE, 1, Builder.addChildFunction(Effect));
            Builder.emitABC(LOP_CAPTURE, LCT_REF, 0, 0);
            if (Generic)
            {
                Builder.emitAD(LOP_NEWCLOSURE, 3, Builder.addChildFunction(Iterator));
                Builder.emitABC(LOP_LOADNIL, 4, 0, 0);
                Builder.emitAD(LOP_LOADN, 5, 0);
            }
            else
            {
                Builder.emitAD(LOP_LOADN, 3, 3);
                Builder.emitAD(LOP_LOADN, 4, 1);
                Builder.emitAD(LOP_LOADN, 5, 1);
            }
            const auto Prep = Builder.emitLabel();
            Builder.emitAD(Generic ? LOP_FORGPREP : LOP_FORNPREP, 3, 0);
            const auto Body = Builder.emitLabel();
            const auto Value = std::uint8_t(Generic ? 6 : 5);
            std::size_t NormalBreak = 0;
            if (Variant == 2)
            {
                NormalBreak = Builder.emitLabel();
                Builder.emitAD(LOP_JUMPXEQKN, Value, 0);
                Builder.emitAux(unsigned(First));
            }
            const auto Compare = Builder.emitLabel();
            Builder.emitAD(LOP_JUMPXEQKN, Value, 0);
            Builder.emitAux(unsigned(Needle) | (1u << 31));
            Builder.emitABC(LOP_MOVE, 2, Value, 0);
            const auto Found = Builder.emitLabel();
            Builder.emitAD(LOP_JUMP, 0, 0);
            const auto Tail = Builder.emitLabel();
            Builder.emitAD(Generic ? LOP_FORGLOOP : LOP_FORNLOOP, 3, 0);
            if (Generic)
                Builder.emitAux(1);
            const auto Exhausted = Builder.emitLabel();
            Builder.emitABC(LOP_MOVE, 8, 1, 0);
            Builder.emitABC(LOP_CALL, 8, 1, 1);
            Builder.emitABC(LOP_LOADNIL, 2, 0, 0);
            const auto Join = Builder.emitLabel();
            Builder.emitABC(LOP_NOT, 2, 2, 0);
            Builder.emitABC(LOP_MOVE, 3, 0, 0);
            Builder.emitABC(LOP_CLOSEUPVALS, 0, 0, 0);
            Builder.emitABC(LOP_RETURN, 2, 3, 0);
            Patch(Prep, Generic ? Tail : Exhausted);
            Patch(Compare, Tail);
            Patch(Tail, Body);
            Patch(Found, Join);
            if (Variant == 2)
                Patch(NormalBreak, Exhausted);
            Builder.endFunction(10, 0);
            Builder.setMainFunction(Main);
            Builder.finalize();
            Result.push_back({std::string("bytecode_threaded_effectful_default_") + (Generic ? "generic_" : "numeric_") +
                                  (Variant == 0   ? "match"
                                   : Variant == 1 ? "miss"
                                                  : "normal_break"),
                              Builder.getBytecode(), Variant == 0 ? "1:false;3:1:0;" : "1:true;3:1:1;"});
        }
    for (bool OuterGeneric : {false, true})
        for (bool InnerGeneric : {false, true})
            for (bool Match : {false, true})
            {
                Luau::BytecodeBuilder Builder;
                auto Patch = [&](std::size_t From, std::size_t To)
                {
                    if (!Builder.patchJumpD(From, To))
                        throw std::runtime_error("Nested loop exit fixture overflow");
                };
                const auto Iterator = Builder.beginFunction(2);
                const auto One = Builder.addConstantNumber(1);
                Builder.emitAD(LOP_LOADN, 2, 3);
                const auto Done = Builder.emitLabel();
                Builder.emitAD(LOP_JUMPIFNOTLT, 1, 0);
                Builder.emitAux(2);
                Builder.emitABC(LOP_ADDK, 0, 1, std::uint8_t(One));
                Builder.emitABC(LOP_RETURN, 0, 2, 0);
                const auto End = Builder.emitLabel();
                Builder.emitABC(LOP_LOADNIL, 0, 0, 0);
                Builder.emitABC(LOP_RETURN, 0, 2, 0);
                Patch(Done, End);
                Builder.endFunction(3, 0);
                const auto Main = Builder.beginFunction(0);
                const auto Child = Builder.addChildFunction(Iterator);
                const auto OuterNeedle = Builder.addConstantNumber(Match ? 2 : 7);
                const auto InnerNeedle = Builder.addConstantNumber(2);
                auto Setup = [&](bool Generic, std::uint8_t Base)
                {
                    if (Generic)
                    {
                        Builder.emitAD(LOP_NEWCLOSURE, Base, Child);
                        Builder.emitABC(LOP_LOADNIL, Base + 1, 0, 0);
                        Builder.emitAD(LOP_LOADN, Base + 2, 0);
                    }
                    else
                    {
                        Builder.emitAD(LOP_LOADN, Base, 3);
                        Builder.emitAD(LOP_LOADN, Base + 1, 1);
                        Builder.emitAD(LOP_LOADN, Base + 2, 1);
                    }
                };
                Setup(OuterGeneric, 1);
                const auto OuterPrep = Builder.emitLabel();
                Builder.emitAD(OuterGeneric ? LOP_FORGPREP : LOP_FORNPREP, 1, 0);
                const auto OuterBody = Builder.emitLabel();
                Setup(InnerGeneric, 5);
                const auto InnerPrep = Builder.emitLabel();
                Builder.emitAD(InnerGeneric ? LOP_FORGPREP : LOP_FORNPREP, 5, 0);
                const auto InnerBody = Builder.emitLabel();
                const auto OuterTest = Builder.emitLabel();
                Builder.emitAD(LOP_JUMPXEQKN, std::uint8_t(OuterGeneric ? 4 : 3), 0);
                Builder.emitAux(unsigned(OuterNeedle) | (1u << 31));
                const auto InnerTest = Builder.emitLabel();
                Builder.emitAD(LOP_JUMPXEQKN, std::uint8_t(InnerGeneric ? 8 : 7), 0);
                Builder.emitAux(unsigned(InnerNeedle) | (1u << 31));
                Builder.emitABC(LOP_MOVE, 0, std::uint8_t(InnerGeneric ? 8 : 7), 0);
                const auto Found = Builder.emitLabel();
                Builder.emitAD(LOP_JUMP, 0, 0);
                const auto InnerTail = Builder.emitLabel();
                Builder.emitAD(InnerGeneric ? LOP_FORGLOOP : LOP_FORNLOOP, 5, 0);
                if (InnerGeneric)
                    Builder.emitAux(1);
                const auto OuterTail = Builder.emitLabel();
                Builder.emitAD(OuterGeneric ? LOP_FORGLOOP : LOP_FORNLOOP, 1, 0);
                if (OuterGeneric)
                    Builder.emitAux(1);
                const auto Exhausted = Builder.emitLabel();
                Builder.emitABC(LOP_LOADNIL, 0, 0, 0);
                const auto Join = Builder.emitLabel();
                Builder.emitABC(LOP_NOT, 0, 0, 0);
                Builder.emitABC(LOP_RETURN, 0, 2, 0);
                Patch(OuterPrep, OuterGeneric ? OuterTail : Exhausted);
                Patch(InnerPrep, InnerGeneric ? InnerTail : OuterTail);
                Patch(OuterTest, InnerTail);
                Patch(InnerTest, InnerTail);
                Patch(Found, Join);
                Patch(InnerTail, InnerBody);
                Patch(OuterTail, OuterBody);
                Builder.endFunction(10, 0);
                Builder.setMainFunction(Main);
                Builder.finalize();
                Result.push_back({std::string("bytecode_threaded_nested_exit_") + (OuterGeneric ? "generic_" : "numeric_") +
                                      (InnerGeneric ? "generic_" : "numeric_") + (Match ? "match" : "miss"),
                                  Builder.getBytecode(), Match ? "1:false;" : "1:true;"});
            }
    return Result;
}

inline std::vector<std::pair<std::string, std::string>> ObfuscatedCaptureCases()
{
    return {
        {"universal_open_captures_share_loop_identity", R"(
local saved={}
local value=0
while value<3 do
    value+=1
    saved[#saved+1]=function(delta) value+=delta return value end
end
return saved[1](10),saved[2](20),saved[3](30),value
)"},
        {"universal_closed_outer_open_inner_captures", R"(
local saved={}
for outer=1,3 do
    local value=outer*10
    local step=0
    while step<2 do
        value+=1
        saved[#saved+1]=function(delta) value+=delta return value end
        step+=1
    end
end
return saved[1](3),saved[2](0),saved[3](5),saved[4](0),saved[5](7),saved[6](0),saved[1](1)
)"},
        {"universal_literal_prefix_before_open_capture", R"(
local values={11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30}
local saved={}
local captured=nil
local step=0
while step<3 do
    captured=values[step+1]
    saved[#saved+1]=function(delta) captured+=delta return captured end
    step+=1
end
return saved[1](2),saved[2](3),saved[3](4),values[1],values[20]
)"},
    };
}
