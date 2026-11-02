#include "Reconstruction.hpp"

namespace Astra::Detail
{
bool Function::PredicateRegion(unsigned Pos, unsigned Limit, unsigned &LastTest, unsigned &Fallthrough, int &Target, std::string &Test)
{
    constexpr unsigned MaxPredicates = 96;
    constexpr std::size_t MaxExpression = 32768;
    // Most conditions are linear chains already handled by Region. Before
    // allocating graph maps or formatting comparisons, require a taken edge
    // that reaches another predicate group within this contiguous prefix.
    unsigned Probe = Pos, Earliest = Limit;
    bool HasInternalEdge = false;
    for (unsigned Count = 0; Probe < Limit && Count < MaxPredicates; ++Count)
    {
        unsigned First = Probe;
        while (Probe < Limit && !Conditional(P.Instructions[Probe].Op))
        {
            const auto &I = P.Instructions[Probe];
            E.Analysis.Spend();
            if (I.Op != LOP_LOADNIL && I.Op != LOP_LOADN && I.Op != LOP_LOADK && I.Op != LOP_LOADKX && !(I.Op == LOP_LOADB && !I.C))
                return false;
            ++Probe;
        }
        if (Probe >= Limit || Earliest < First)
            return false;
        if (Earliest == First)
        {
            HasInternalEdge = true;
            break;
        }
        E.Analysis.Spend();
        unsigned Taken = Index(unsigned(P.Instructions[Probe].Target()));
        if (Taken <= Probe || Taken > Limit)
            return false;
        if (Taken != Probe + 1)
            Earliest = std::min(Earliest, Taken);
        ++Probe;
    }
    if (!HasInternalEdge)
        return false;
    // A condition can be a forward DAG, rather than a linear sequence of tests
    // sharing one destination. Keep its two statement bodies outside that DAG;
    // lazy if-expressions preserve every comparison and its evaluation order.
    struct Predicate
    {
        unsigned First, Test, Taken, Next;
        std::string Condition;
    };
    std::vector<Predicate> Nodes;
    std::map<unsigned, unsigned> Destinations;
    std::set<unsigned> TakenDestinations;
    unsigned Cursor = Pos, BestCount = 0, BestEnd = 0, BestTarget = 0;
    bool Nonlinear = false;
    auto Literal = [&](const Instruction &I, std::string &Text)
    {
        if (Cells.count(I.A) || ScalarReferences.count(I.A))
            return false;
        switch (I.Op)
        {
        case LOP_LOADNIL:
            Text = "nil";
            return true;
        case LOP_LOADN:
            Text = std::to_string(I.D);
            return true;
        case LOP_LOADB:
            if (I.C)
                return false;
            Text = I.B ? "true" : "false";
            return true;
        case LOP_LOADK:
        case LOP_LOADKX:
        {
            unsigned K = I.Op == LOP_LOADK ? unsigned(I.D) : I.Aux;
            auto Tag = P.Constants[K].Tag;
            if (Tag != LBC_CONSTANT_NIL && Tag != LBC_CONSTANT_BOOLEAN && Tag != LBC_CONSTANT_NUMBER && Tag != LBC_CONSTANT_STRING)
                return false;
            Text = ConstantAt(K);
            return true;
        }
        default:
            return false;
        }
    };
    while (Cursor < Limit && Nodes.size() < MaxPredicates)
    {
        unsigned First = Cursor;
        std::map<unsigned, std::string> Literals;
        bool Valid = true;
        // O0 loads comparison constants into temporary registers. Admit only
        // a single-entry literal setup whose results die at both test exits.
        while (Cursor < Limit && !Conditional(P.Instructions[Cursor].Op))
        {
            E.Analysis.Spend();
            std::string Value;
            if (!Literal(P.Instructions[Cursor], Value))
            {
                Valid = false;
                break;
            }
            Literals[P.Instructions[Cursor].A] = std::move(Value);
            ++Cursor;
        }
        if (!Valid || Cursor >= Limit)
            break;
        for (unsigned K = First; K <= Cursor; ++K)
        {
            E.Analysis.Spend(1 + Flow.Predecessors[K].size());
            if (K == Pos)
                continue;
            for (unsigned Before : Flow.Predecessors[K])
                if (K == First ? Before < Pos || Before >= First : Before + 1 != K)
                    Valid = false;
        }
        const auto &I = P.Instructions[Cursor];
        unsigned Taken = Index(unsigned(I.Target())), Next = Cursor + 1;
        if (!Valid || Taken <= Cursor || Taken > Limit)
            break;
        for (const auto &[R, Value] : Literals)
            if (Flow.LiveIn[Taken].test(R) || Flow.LiveIn[Next].test(R))
                Valid = false;
        if (!Valid)
            break;
        std::string ConditionText = Condition(I);
        for (const auto &[R, Value] : Literals)
            Replace(ConditionText, Read(R, I.Pc), "(" + Value + ")");
        Nonlinear |= TakenDestinations.count(First) != 0;
        Destinations.erase(First);
        ++Destinations[Taken];
        ++Destinations[Next];
        if (Taken != Next)
            TakenDestinations.insert(Taken);
        Nodes.push_back({First, Cursor, Taken, Next, std::move(ConditionText)});
        Cursor = Next;
        // Leave the existing linear-chain path in charge of ordinary and/or
        // chains: only an internal taken edge needs this more general factoring.
        if (Nonlinear && Nodes.size() >= 3 && Destinations.size() == 2 && Destinations.count(Next))
        {
            BestCount = unsigned(Nodes.size());
            BestEnd = Next;
            BestTarget = Destinations.begin()->first == Next ? Destinations.rbegin()->first : Destinations.begin()->first;
        }
    }
    if (!BestCount)
        return false;
    struct Decision
    {
        std::string Condition;
        unsigned Yes, No;
    };
    // Fold diamonds while they still have node identity. Stringifying first
    // would copy a shared suffix into both branches, potentially exponentially.
    // Every fold retains the original lazy order, including metamethod calls.
    std::vector<Decision> Decisions{{"false", 0, 0}, {"true", 1, 1}};
    std::map<unsigned, unsigned> Reduced{{BestEnd, 0}, {BestTarget, 1}};
    for (unsigned K = BestCount; K-- > 0;)
    {
        const auto &Node = Nodes[K];
        auto Taken = Reduced.find(Node.Taken), Next = Reduced.find(Node.Next);
        if (Taken == Reduced.end() || Next == Reduced.end())
            return false;
        Reduced.emplace(Node.First, unsigned(Decisions.size()));
        Decisions.push_back({Node.Condition, Taken->second, Next->second});
    }
    std::vector<std::string> Expressions{"false", "true"};
    for (unsigned K = 2; K < Decisions.size(); ++K)
    {
        Decision Combined = Decisions[K];
        // Keep the original child identities while collapsing this root. An
        // eagerly rewritten child can hide the shared node its parent needs to
        // recognize an and/or diamond, even though both represent equal CFGs.
        for (;;)
        {
            const Decision *Child = nullptr;
            const char *Operator = nullptr;
            bool Invert = false;
            if (Combined.No >= 2)
            {
                const auto &Other = Decisions[Combined.No];
                if (Combined.Yes == Other.Yes)
                    Child = &Other, Operator = " or ";
                else if (Combined.Yes == Other.No)
                    Child = &Other, Operator = " and ", Invert = true;
            }
            if (!Child && Combined.Yes >= 2)
            {
                const auto &Other = Decisions[Combined.Yes];
                if (Combined.No == Other.No)
                    Child = &Other, Operator = " and ";
                else if (Combined.No == Other.Yes)
                    Child = &Other, Operator = " or ", Invert = true;
            }
            if (!Child)
                break;
            if (Combined.Condition.size() + Child->Condition.size() + 24 > MaxExpression)
                return false;
            Combined.Condition = std::string(Invert ? "not (" : "(") + Combined.Condition + ")" + Operator + "(" + Child->Condition + ")";
            Combined.Yes = Child->Yes;
            Combined.No = Child->No;
            E.Analysis.Spend(1 + Combined.Condition.size() / 64);
        }
        const auto &Node = Combined;
        const auto &Yes = Expressions[Node.Yes];
        const auto &No = Expressions[Node.No];
        if (Node.Condition.size() + Yes.size() + No.size() + 40 > MaxExpression)
            return false;
        std::string Expression;
        if (Yes == "true" && No == "false")
            Expression = "(" + Node.Condition + ")";
        else if (Yes == "false" && No == "true")
            Expression = "not (" + Node.Condition + ")";
        else if (Yes == "true")
            Expression = "(" + Node.Condition + ") or (" + No + ")";
        else if (No == "false")
            Expression = "(" + Node.Condition + ") and (" + Yes + ")";
        else if (Yes == "false")
            Expression = "not (" + Node.Condition + ") and (" + No + ")";
        else if (No == "true")
            Expression = "not (" + Node.Condition + ") or (" + Yes + ")";
        else
            Expression = "(if (" + Node.Condition + ") then (" + Yes + ") else (" + No + "))";
        // The same continuation may appear in both lazy arms, but only the arm
        // selected by the original comparison executes. Bound expansion because
        // arbitrary DAGs can need exponentially large expression trees.
        E.Analysis.Spend(1 + Expression.size() / 64);
        Expressions.push_back(std::move(Expression));
    }
    Test = std::move(Expressions[Reduced.at(Pos)]);
    LastTest = Nodes[BestCount - 1].Test;
    Fallthrough = P.Instructions[LastTest].Next;
    Target = BestTarget == P.Instructions.size() ? int(P.Code.size()) : int(P.Instructions[BestTarget].Pc);
    return true;
}
} // namespace Astra::Detail

namespace Astra::Detail
{
bool Function::CollapseValue(std::vector<Statement> List, unsigned Result, std::string &Value)
{
    // Inline every temporary of a straight-line value computation into the
    // final assignment. A sentinel read keeps the result alive while the
    // ordinary optimizer runs on this isolated list.
    if (E.Symbols[Result].Cell)
        return false;
    List.push_back({Kind::Raw, {}, Token(Result)});
    for (unsigned Pass = 0; Pass < 8 && Optimize(List); ++Pass)
        ;
    if (List.size() == 1 && List[0].Type == Kind::Raw)
    {
        Value = List[0].Text;
        return true;
    }
    if (List.size() == 2 && List[0].Type == Kind::Assign && List[0].Left == std::vector<unsigned>{Result} && List[1].Text == Token(Result))
    {
        Value = List[0].Text;
        return true;
    }
    return false;
}

bool Function::ShortCircuitValue(unsigned Pos, unsigned Limit, std::vector<Statement> &Out, unsigned &Resume)
{
    // `a and b or c` and `(a or b) and c` compile to a test chain that jumps
    // to the last operand, the middle operand computed into the result, and a
    // final truthiness test of that result that either keeps it or falls into
    // the last operand:
    //
    //     tests -> Else; [b -> R]; JUMPIF(NOT) R -> End; Else: [c -> R]; End:
    //
    // Each operand must collapse into a single expression; otherwise the
    // ordinary statement reconstruction handles the region.
    const auto &First = P.Instructions[Pos];
    if (First.Target() <= int(First.Pc))
        return false;
    unsigned Last = Pos;
    while (Last + 1 < Limit && Conditional(P.Instructions[Last + 1].Op) && P.Instructions[Last + 1].Target() == First.Target() &&
           Flow.Predecessors[Last + 1].size() == 1 && Flow.Predecessors[Last + 1][0] == Last)
        ++Last;
    unsigned Else = Index(unsigned(First.Target()));
    if (Else >= Limit || Else < Last + 3)
        return false;
    unsigned Test = Else - 1;
    const auto &Exit = P.Instructions[Test];
    if ((Exit.Op != LOP_JUMPIF && Exit.Op != LOP_JUMPIFNOT) || Exit.Target() <= int(Exit.Pc))
        return false;
    unsigned End = Index(unsigned(Exit.Target()));
    if (End <= Else || End > Limit)
        return false;
    const unsigned R = Exit.A;
    if (Cells.count(R) || ScalarReferences.count(R))
        return false;
    auto Straight = [&](unsigned From, unsigned To)
    {
        for (unsigned K = From; K < To; ++K)
        {
            const auto &I = P.Instructions[K];
            if (Conditional(I.Op) || Jump(I.Op) || I.Op == LOP_RETURN || (I.Op == LOP_LOADB && I.C) || I.Op == LOP_CMPPROTO ||
                I.Op == LOP_FORNPREP || I.Op == LOP_FORNLOOP || I.Op == LOP_FORGPREP || I.Op == LOP_FORGPREP_NEXT ||
                I.Op == LOP_FORGPREP_INEXT || I.Op == LOP_FORGLOOP || I.Op == LOP_CLOSEUPVALS)
                return false;
            if (Flow.Predecessors[K].size() != 1 || Flow.Predecessors[K][0] + 1 != K)
                if (K != Else)
                    return false;
            E.Analysis.Spend();
        }
        return true;
    };
    if (!Straight(Last + 1, Test) || !Straight(Else, End) || Flow.Predecessors[Test].size() != 1 || Flow.Predecessors[Test][0] + 1 != Test)
        return false;
    for (unsigned Before : Flow.Predecessors[Else])
        if ((Before < Pos || Before > Last) && Before != Test)
            return false;
    for (unsigned Before : Flow.Predecessors[End])
        if (Before != Test && Before + 1 != End)
            return false;
    // Both operands produce the result; no other value they compute survives.
    auto Produces = [&](unsigned From, unsigned To, std::initializer_list<unsigned> Exits)
    {
        bool Writes = false;
        for (unsigned K = From; K < To; ++K)
            for (unsigned W : Astra::Writes(P.Instructions[K], P))
            {
                if (W == R)
                {
                    Writes = true;
                    continue;
                }
                for (unsigned X : Exits)
                    if (Flow.LiveIn[X].test(W))
                        return false;
            }
        return Writes;
    };
    if (!Produces(Last + 1, Test, {Else, End}) || !Produces(Else, End, {End}))
        return false;
    unsigned Result = SymbolAt(R, Exit.Pc);
    if (Result != SymbolAt(R, P.Instructions[End - 1].Pc, true) || E.Symbols[Result].Cell)
        return false;
    std::vector<Statement> Middle, Alternative;
    for (unsigned K = Last + 1; K < Test; ++K)
        Simple(P.Instructions[K], Middle);
    for (unsigned K = Else; K < End; ++K)
        Simple(P.Instructions[K], Alternative);
    std::string B, C;
    if (!OpenExpression.empty() || !PendingMethod.empty() || !CollapseValue(std::move(Middle), Result, B) ||
        !CollapseValue(std::move(Alternative), Result, C))
        return false;
    std::vector<std::string> Taken;
    for (unsigned K = Pos; K <= Last; ++K)
        Taken.push_back("(" + Condition(P.Instructions[K]) + ")");
    std::string Value;
    if (Exit.Op == LOP_JUMPIF)
    {
        std::vector<std::string> Enter;
        for (const auto &Text : Taken)
            Enter.push_back("(" + Negate(Text) + ")");
        Value = "(" + Join(Enter, " and ") + " and (" + B + ") or (" + C + "))";
    }
    else
        Value = "((" + Join(Taken, " or ") + " or (" + B + ")) and (" + C + "))";
    Out.push_back(Assign(R, P.Instructions[End - 1], Value));
    Resume = End;
    return true;
}
} // namespace Astra::Detail
