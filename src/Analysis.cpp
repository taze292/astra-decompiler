#include "Reconstruction.hpp"

namespace Taze::Detail
{
Function::Function(Engine &Owner, unsigned Proto, std::vector<Upvalue> Captures, unsigned Nesting)
    : E(Owner), P(E.C.Prototypes.at(Proto)), Flow(P, E.Settings.MaxAnalysisCells, E.Analysis), Id(Proto), Depth(Nesting),
      Upvalues(std::move(Captures))
{
    if (Depth > E.Settings.MaxNesting)
        throw Error("Function nesting exceeds configured limit");
    for (const auto &I : P.Instructions)
        for (auto [Kind, R] : I.Captures)
            if (Kind == LCT_REF)
                Cells.insert(R);
    for (unsigned R = 0; R < P.Stack; ++R)
    {
        std::string Name = R < P.Parameters ? "Argument" + std::to_string(R + 1) : "Value" + std::to_string(R + 1);
        bool Debug = false;
        for (const auto &L : P.Locals)
            if (L.Register == R && ((R < P.Parameters && L.Start <= 1) || Cells.count(R)))
            {
                Name = L.Name;
                Debug = true;
                break;
            }
        Registers.push_back(E.New(Name, Id, R < P.Parameters, R < P.Parameters, Cells.count(R) != 0, false, Debug));
        if (R < P.Parameters)
            Parameters.push_back(Registers.back());
    }
    for (const auto &L : P.Locals)
    {
        if (L.Start <= 1 && L.Register < P.Parameters)
        {
            LocalSymbols.push_back(Registers[L.Register]);
            LocalStarts.push_back(0);
            continue;
        }
        unsigned Start = L.Start;
        // Debug ranges start after initialization. Include the last defining instruction in this local's lifetime.
        for (auto It = P.Instructions.rbegin(); It != P.Instructions.rend(); ++It)
        {
            if (It->Pc >= L.Start)
                continue;
            auto Written = Writes(*It, P);
            if (std::find(Written.begin(), Written.end(), L.Register) != Written.end())
            {
                Start = It->Pc;
                break;
            }
        }
        for (const auto &I : P.Instructions)
            if ((I.Op == LOP_FORGPREP || I.Op == LOP_FORGPREP_NEXT || I.Op == LOP_FORGPREP_INEXT) && I.Next == L.Start &&
                L.Register >= I.A + 3)
                Start = L.Start;
        LocalStarts.push_back(Start);
        LocalSymbols.push_back(Cells.count(L.Register) ? Registers[L.Register] : E.New(L.Name, Id, true, false, false, false, true));
    }
    PackSymbol = E.New("Results", Id, true);
    TopSymbol = E.New("ResultBase", Id, true);
    VarargsSymbol = E.New("Arguments", Id, true);
    while (Upvalues.size() < P.Upvalues)
        throw Error("Main prototype has unbound upvalues");
    BuildDataflow();
    if (E.AnalyzeExports())
    {
        std::vector<unsigned> Captures;
        for (const auto &U : Upvalues)
        {
            auto Refs = Tokens(U.Value);
            Captures.push_back(!U.Reference && Refs.size() == 1 ? Refs.front() : std::numeric_limits<unsigned>::max());
        }
        ExportFrame = E.ExportGraph.Record(P, Flow, Id, ReadSymbols, WriteSymbols, Captures, Parameters);
        if (Depth == 0)
            E.MainExportFrame = ExportFrame;
    }
}

void Function::BuildDataflow()
{
    const unsigned Count = unsigned(P.Instructions.size());
    if (std::uint64_t(Count) * P.Stack > E.Settings.MaxAnalysisCells)
        throw Error("Function dataflow exceeds analysis limit");
    E.Analysis.Spend(std::size_t(Count) * P.Stack * 3);
    std::vector<unsigned> Parent;
    auto New = [&]()
    {
        auto Id = unsigned(Parent.size());
        Parent.push_back(Id);
        return Id;
    };
    std::function<unsigned(unsigned)> Find = [&](unsigned Id) -> unsigned
    {
        unsigned Root = Id;
        while (Parent[Root] != Root)
            Root = Parent[Root];
        while (Parent[Id] != Id)
        {
            auto Next = Parent[Id];
            Parent[Id] = Root;
            Id = Next;
        }
        return Root;
    };
    auto Union = [&](unsigned A, unsigned B)
    {
        A = Find(A);
        B = Find(B);
        if (A != B)
            Parent[std::max(A, B)] = std::min(A, B);
    };
    std::vector<unsigned> Initial;
    for (unsigned R = 0; R < P.Stack; ++R)
        Initial.push_back(New());
    std::vector<std::vector<std::pair<unsigned, unsigned>>> Definitions(Count);
    for (unsigned J = 0; J < Count; ++J)
        for (unsigned R : Writes(P.Instructions[J], P))
        {
            auto D = New();
            Definitions[J].emplace_back(R, D);
            if (Cells.count(R))
                Union(D, Initial[R]);
        }
    ReadSymbols.resize(Count);
    WriteSymbols.resize(Count);
    std::vector<bool> Visited(Count, false);
    ReadSymbols[0] = Initial;
    Visited[0] = true;
    std::queue<unsigned> Queue;
    Queue.push(0);
    while (!Queue.empty())
    {
        unsigned J = Queue.front();
        Queue.pop();
        auto State = ReadSymbols[J];
        for (auto [R, D] : Definitions[J])
            State[R] = D;
        WriteSymbols[J] = State;
        for (unsigned Next : Flow.Successors[J])
        {
            if (Next >= Count)
                continue;
            if (!Visited[Next])
            {
                ReadSymbols[Next] = State;
                Visited[Next] = true;
                Queue.push(Next);
            }
            else
                for (unsigned R = 0; R < P.Stack; ++R)
                    if (Flow.LiveIn[Next].test(R))
                        Union(ReadSymbols[Next][R], State[R]);
        }
    }
    constexpr unsigned Missing = std::numeric_limits<unsigned>::max();
    std::vector<unsigned> RootSymbol(Parent.size(), Missing);
    for (unsigned R = 0; R < P.Stack; ++R)
        if (R < P.Parameters || Cells.count(R))
            RootSymbol[Find(Initial[R])] = Registers[R];
    // Recover debug names from the values alive in their ranges, rather than treating a register as one permanent variable.
    for (unsigned J = 0; J < P.Locals.size(); ++J)
    {
        const auto &L = P.Locals[J];
        auto It = std::lower_bound(P.Instructions.begin(), P.Instructions.end(), L.Start,
                                   [](const Instruction &I, unsigned Pc) { return I.Pc < Pc; });
        if (It == P.Instructions.end())
            continue;
        auto Index = std::size_t(It - P.Instructions.begin());
        if (ReadSymbols[Index].empty())
            continue;
        auto Root = Find(ReadSymbols[Index][L.Register]);
        if (RootSymbol[Root] == Missing)
            RootSymbol[Root] = LocalSymbols[J];
    }
    auto Resolve = [&](unsigned Definition, unsigned R)
    {
        unsigned Root = Find(Definition);
        if (RootSymbol[Root] != Missing)
            return RootSymbol[Root];
        unsigned S = E.New("Value" + std::to_string(R + 1), Id);
        RootSymbol[Root] = S;
        return S;
    };
    for (unsigned J = 0; J < Count; ++J)
    {
        if (!Visited[J])
        {
            ReadSymbols[J] = Initial;
            WriteSymbols[J] = Initial;
        }
        for (unsigned R = 0; R < P.Stack; ++R)
        {
            ReadSymbols[J][R] = Resolve(ReadSymbols[J][R], R);
            WriteSymbols[J][R] = Resolve(WriteSymbols[J][R], R);
        }
    }

    if (!E.Settings.UseTypeNames)
        return;
    // Union possible types over all definitions, including loop/branch joins. Only
    // specialize anonymous names when every reaching definition agrees.
    std::map<unsigned, unsigned> Types;
    std::map<unsigned, std::vector<unsigned>> Copies;
    std::queue<unsigned> Pending;
    auto Add = [&](unsigned S, unsigned Mask)
    {
        if ((Types[S] | Mask) != Types[S])
        {
            Types[S] |= Mask;
            Pending.push(S);
        }
    };
    auto ConstantType = [&](unsigned K)
    {
        switch (P.Constants[K].Tag)
        {
        case LBC_CONSTANT_TABLE:
        case LBC_CONSTANT_TABLE_WITH_CONSTANTS:
            return 2u;
        case LBC_CONSTANT_CLOSURE:
            return 4u;
        case LBC_CONSTANT_STRING:
            return 8u;
        case LBC_CONSTANT_NUMBER:
            return 16u;
        case LBC_CONSTANT_BOOLEAN:
            return 32u;
        case LBC_CONSTANT_VECTOR:
            return 64u;
        case LBC_CONSTANT_INTEGER:
            return 128u;
        default:
            return 1u;
        }
    };
    for (unsigned R = 0; R < P.Stack; ++R)
        Add(Resolve(Initial[R], R), 1);
    for (unsigned J = 0; J < Count; ++J)
    {
        if (!Visited[J])
            continue;
        const auto &I = P.Instructions[J];
        for (auto R : Writes(I, P))
        {
            auto S = WriteSymbols[J][R];
            if (I.Op == LOP_MOVE)
            {
                Copies[ReadSymbols[J][I.B]].push_back(S);
                continue;
            }
            unsigned Mask = 1;
            if (R == I.A)
                switch (I.Op)
                {
                case LOP_NEWTABLE:
                    Mask = 2;
                    break;
                case LOP_NEWCLOSURE:
                case LOP_DUPCLOSURE:
                    Mask = 4;
                    break;
                case LOP_LOADK:
                case LOP_DUPTABLE:
                    Mask = ConstantType(unsigned(I.D));
                    break;
                case LOP_LOADKX:
                    Mask = ConstantType(I.Aux);
                    break;
                case LOP_LOADN:
                    Mask = 16;
                    break;
                case LOP_LOADB:
                case LOP_NOT:
                    Mask = 32;
                    break;
                default:
                    break;
                }
            Add(S, Mask);
        }
    }
    while (!Pending.empty())
    {
        auto S = Pending.front();
        Pending.pop();
        for (auto Destination : Copies[S])
            Add(Destination, Types[S]);
    }
    const std::map<unsigned, std::string> Categories = {{2, "Table"},    {4, "Function"}, {8, "String"},   {16, "Number"},
                                                        {32, "Boolean"}, {64, "Vector"},  {128, "Integer"}};
    for (const auto &[S, Mask] : Types)
        if (auto It = Categories.find(Mask); It != Categories.end())
            E.Symbols[S].Category = It->second;
    // Reference captures are represented by an actual one-element table in
    // generated source, regardless of the type of the captured value.
    for (auto &S : E.Symbols)
        if (S.Owner == Id && S.Cell)
            S.Category = "Table";
}

} // namespace Taze::Detail
