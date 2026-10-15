#pragma once

#include "Luau/BytecodeBuilder.h"
#include <stdexcept>
#include <string>
#include <vector>

struct NumericCoercionFixture
{
    std::string Name;
    std::string Bytes;
    std::string Expected;
};

inline std::vector<NumericCoercionFixture> NumericCoercionCases()
{
    std::vector<NumericCoercionFixture> Result;
    for (unsigned Observed = 1; Observed <= 3; ++Observed)
    {
        // FORNPREP converts its hidden limit, step and index in place. Reading
        // those registers in the body must observe numbers, even though their
        // initial values were numeric strings. A source for loop only exposes
        // the normalized index; limit/step locals retain their original types.
        Luau::BytecodeBuilder Builder;
        auto Main = Builder.beginFunction(0);
        auto Three = Builder.addConstantString({"3", 1});
        auto One = Builder.addConstantString({"1", 1});
        Builder.emitABC(LOP_LOADNIL, 0, 0, 0);
        Builder.emitAD(LOP_LOADK, 1, Three);
        Builder.emitAD(LOP_LOADK, 2, One);
        Builder.emitAD(LOP_LOADK, 3, One);
        auto Prepare = Builder.emitLabel();
        Builder.emitAD(LOP_FORNPREP, 1, 0);
        auto Body = Builder.emitLabel();
        Builder.emitABC(LOP_MOVE, 0, std::uint8_t(Observed), 0);
        auto Tail = Builder.emitLabel();
        Builder.emitAD(LOP_FORNLOOP, 1, 0);
        auto Exit = Builder.emitLabel();
        Builder.emitABC(LOP_RETURN, 0, 2, 0);
        if (!Builder.patchJumpD(Prepare, Exit) || !Builder.patchJumpD(Tail, Body))
            throw std::runtime_error("Numeric coercion fixture jump overflow");
        Builder.endFunction(4, 0);
        Builder.setMainFunction(Main);
        Builder.finalize();
        Result.push_back(
            {"bytecode_numeric_coercion_read_" + std::to_string(Observed), Builder.getBytecode(), Observed == 2 ? "3:1:1;" : "3:1:3;"});
    }
    for (unsigned Observed = 1; Observed <= 3; ++Observed)
    {
        // The ordinary exit does not read control state, but a separate return
        // reached from the body does. Checking only the tail's LiveIn successor
        // misses this observable in-place coercion on a nonstandard loop exit.
        Luau::BytecodeBuilder Builder;
        auto Main = Builder.beginFunction(0);
        auto Three = Builder.addConstantString({"3", 1});
        auto One = Builder.addConstantString({"1", 1});
        Builder.emitABC(LOP_LOADB, 0, 1, 0);
        Builder.emitAD(LOP_LOADK, 1, Three);
        Builder.emitAD(LOP_LOADK, 2, One);
        Builder.emitAD(LOP_LOADK, 3, One);
        auto Prepare = Builder.emitLabel();
        Builder.emitAD(LOP_FORNPREP, 1, 0);
        auto Body = Builder.emitLabel();
        Builder.emitAD(LOP_JUMPIF, 0, 0);
        auto Tail = Builder.emitLabel();
        Builder.emitAD(LOP_FORNLOOP, 1, 0);
        auto Exit = Builder.emitLabel();
        Builder.emitABC(LOP_RETURN, 0, 2, 0);
        auto OtherExit = Builder.emitLabel();
        Builder.emitABC(LOP_RETURN, std::uint8_t(Observed), 2, 0);
        if (!Builder.patchJumpD(Prepare, Exit) || !Builder.patchJumpD(Tail, Body) || !Builder.patchJumpD(Body, OtherExit))
            throw std::runtime_error("Numeric coercion exit fixture jump overflow");
        Builder.endFunction(4, 0);
        Builder.setMainFunction(Main);
        Builder.finalize();
        Result.push_back(
            {"bytecode_numeric_coercion_exit_" + std::to_string(Observed), Builder.getBytecode(), Observed == 1 ? "3:1:3;" : "3:1:1;"});
    }
    return Result;
}
