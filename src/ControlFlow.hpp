#pragma once

#include "Bytecode.hpp"
#include <bitset>

namespace Taze
{
struct AnalysisBudget
{
    std::size_t Remaining;
    void Spend(std::size_t Amount = 1)
    {
        if (Amount > Remaining) throw Error("Analysis exceeds configured work limit");
        Remaining -= Amount;
    }
};
// Instruction indices are graph nodes; the final node is the common function exit.
struct ControlFlow
{
    std::vector<std::vector<unsigned>> Successors, Predecessors;
    std::vector<int> Dominators, PostDominators;
    std::vector<std::bitset<256>> LiveIn;
    std::vector<unsigned> Blocks;
    std::vector<unsigned> DomEnter, DomExit;

    explicit ControlFlow(const Prototype &Proto, std::size_t MaxCells, AnalysisBudget &Budget);
    bool Dominates(unsigned Header, unsigned Node) const;
};

std::vector<unsigned> Reads(const Instruction &I, const Prototype &P);
} // namespace Taze
