#include "Reconstruction.hpp"

namespace Taze::Detail
{
bool Jump(LuauOpcode Op)
{
    return Op == LOP_JUMP || Op == LOP_JUMPBACK || Op == LOP_JUMPX;
}
bool Conditional(LuauOpcode Op)
{
    return (Op >= LOP_JUMPIF && Op <= LOP_JUMPIFNOTLT) || (Op >= LOP_JUMPXEQKNIL && Op <= LOP_JUMPXEQKS);
}

namespace
{
bool NativeGenericLoop(const Prototype &P, const ControlFlow &Flow, unsigned Head, unsigned Tail, AnalysisBudget &Budget,
                       std::string *Failure = nullptr, bool AllowDuplicateTails = true)
{
    auto Reject = [&](const std::string &Reason)
    {
        if (Failure)
            *Failure = Reason;
        return false;
    };
    const auto &I = P.Instructions[Head];
    const auto &End = P.Instructions[Tail];
    auto Destination = [&](unsigned Pc)
    {
        for (unsigned Steps = 0; Steps < 64 && Pc < P.Code.size(); ++Steps)
        {
            const auto &Edge = P.Instructions[unsigned(P.AtPc(Pc))];
            if (!Jump(Edge.Op) || Edge.Target() == int(Pc))
                break;
            Budget.Spend();
            Pc = unsigned(Edge.Target());
        }
        return Pc;
    };
    if (Tail <= Head || End.Op != LOP_FORGLOOP || End.A != I.A || End.Target() != int(I.Next))
        return Reject("noncanonical preparation/tail");
    // The native for statement owns its hidden iterator state. The bytecode
    // must not inspect that state or enter the body without running preparation.
    for (unsigned R = I.A; R < I.A + 3 + (End.Aux & 255); ++R)
        if (Flow.LiveIn[Tail + 1].test(R))
            return Reject("register " + std::to_string(R) + " live after loop at " + std::to_string(End.Next));
    for (unsigned K = Head + 1; K <= Tail; ++K)
    {
        Budget.Spend(1 + Flow.Predecessors[K].size() + Flow.Successors[K].size());
        for (unsigned Before : Flow.Predecessors[K])
            if (Before < Head || Before > Tail)
                return Reject("outside entry at " + std::to_string(P.Instructions[K].Pc));
        for (unsigned After : Flow.Successors[K])
            if (After <= Head || After > Tail)
                for (unsigned R = I.A; R < I.A + 3; ++R)
                    if (Flow.LiveIn[After].test(R))
                        return Reject("hidden register " + std::to_string(R) + " live on exit at " + std::to_string(P.Instructions[K].Pc));
        if (K == Tail)
            continue;
        const auto &Body = P.Instructions[K];
        if (AllowDuplicateTails && Body.Op == LOP_FORGLOOP && Body.A == End.A && Body.Target() == End.Target() && Body.Aux == End.Aux &&
            Destination(Body.Next) == Destination(End.Next))
            continue;
        if (Body.Op != LOP_CLOSEUPVALS)
            for (unsigned R : Reads(Body, P))
                if (R >= I.A && R < I.A + 3)
                    return Reject("hidden register " + std::to_string(R) + " read at " + std::to_string(Body.Pc));
        for (unsigned R : Writes(Body, P))
            if (R >= I.A && R < I.A + 3)
                return Reject("hidden register " + std::to_string(R) + " written at " + std::to_string(Body.Pc));
    }
    return true;
}
} // namespace

unsigned Function::JumpDestination(unsigned Pc) const
{
    for (unsigned Steps = 0; Steps < 64 && Pc < P.Code.size(); ++Steps)
    {
        const auto &I = P.Instructions[Index(Pc)];
        if (!Jump(I.Op) || I.Target() == int(Pc))
            break;
        E.Analysis.Spend();
        Pc = unsigned(I.Target());
    }
    return Pc;
}

unsigned Function::LoopTail(const Instruction &I) const
{
    auto Kind = I.Op == LOP_FORNPREP ? LOP_FORNLOOP : LOP_FORGLOOP;
    unsigned Tail = unsigned(P.Instructions.size());
    for (unsigned Before : Flow.Predecessors[Index(I.Next)])
    {
        E.Analysis.Spend();
        const auto &Candidate = P.Instructions[Before];
        if (Candidate.Op == Kind && Candidate.A == I.A && Candidate.Target() == int(I.Next))
            Tail = Tail == P.Instructions.size() ? Before : std::max(Tail, Before);
    }
    return Tail;
}

bool Function::EquivalentExit(unsigned Left, unsigned Right) const
{
    Left = JumpDestination(Left);
    Right = JumpDestination(Right);
    if (Left == Right)
        return true;
    if (Left >= P.Code.size() || Right >= P.Code.size())
        return false;
    const auto &A = P.Instructions[Index(Left)];
    const auto &B = P.Instructions[Index(Right)];
    return A.Op == LOP_RETURN && B.Op == LOP_RETURN && A.A == B.A && A.B == B.B;
}

unsigned Function::LoopContinuation(unsigned Head, unsigned Tail, unsigned Limit, std::vector<Statement> &Before)
{
    const unsigned Finish = Tail + 1;
    unsigned Join = Finish;
    for (unsigned K = Head + 1; K < Tail; ++K)
    {
        const auto &Edge = P.Instructions[K];
        if (!Jump(Edge.Op) && !Conditional(Edge.Op))
            continue;
        unsigned Target = Index(unsigned(Edge.Target()));
        if (Target <= Finish)
            continue;
        if (Target > Limit || (Join != Finish && Join != Target))
            return Finish;
        Join = Target;
    }
    if (Join == Finish)
        return Finish;
    std::bitset<256> Defaults;
    for (unsigned K = Finish; K < Join; ++K)
    {
        const auto &I = P.Instructions[K];
        E.Analysis.Spend();
        bool Literal = I.Op == LOP_LOADNIL || I.Op == LOP_LOADN || (I.Op == LOP_LOADB && !I.C);
        if (I.Op == LOP_LOADK || I.Op == LOP_LOADKX)
        {
            auto Tag = P.Constants[I.Op == LOP_LOADK ? unsigned(I.D) : I.Aux].Tag;
            Literal = Tag == LBC_CONSTANT_NIL || Tag == LBC_CONSTANT_BOOLEAN || Tag == LBC_CONSTANT_NUMBER || Tag == LBC_CONSTANT_STRING;
        }
        const auto &Prep = P.Instructions[Head];
        unsigned HiddenEnd = Prep.A + 3 + (Prep.Op == LOP_FORNPREP ? 0 : (P.Instructions[Tail].Aux & 255));
        if (!Literal || Cells.count(I.A) || ScalarReferences.count(I.A) || Flow.LiveIn[Head + 1].test(I.A) ||
            (I.A >= Prep.A && I.A < HiddenEnd))
            return Finish;
        Defaults.set(I.A);
    }
    // A default may move before the loop only when normal exhaustion cannot
    // retain a body write to it. Early-return writes must leave through Join.
    std::vector<bool> ReachesTail(Tail - Head + 1);
    std::vector<unsigned> Pending{Tail};
    while (!Pending.empty())
    {
        unsigned K = Pending.back();
        Pending.pop_back();
        if (K <= Head || K > Tail || ReachesTail[K - Head])
            continue;
        ReachesTail[K - Head] = true;
        E.Analysis.Spend(1 + Flow.Predecessors[K].size());
        if (K != Tail)
            for (unsigned R : Writes(P.Instructions[K], P))
                if (Defaults.test(R))
                    return Finish;
        Pending.insert(Pending.end(), Flow.Predecessors[K].begin(), Flow.Predecessors[K].end());
    }
    for (unsigned K = Finish; K < Join; ++K)
        Simple(P.Instructions[K], Before);
    return Join;
}

void Function::FindLoopRegions()
{
    const unsigned Count = unsigned(P.Instructions.size());
    bool HasBackedges = false, HasForLoops = false;
    for (const auto &I : P.Instructions)
    {
        HasBackedges |= (Jump(I.Op) || Conditional(I.Op)) && I.Target() <= int(I.Pc);
        HasForLoops |= I.Op == LOP_FORNPREP || I.Op == LOP_FORGPREP || I.Op == LOP_FORGPREP_NEXT || I.Op == LOP_FORGPREP_INEXT;
    }
    if (!HasBackedges && !HasForLoops)
        return;
    if (HasBackedges)
        NaturalLoopEnds.assign(Count, 0);
    LoopRegionEnds.assign(Count, 0);
    std::vector<unsigned> Mark(HasBackedges ? Count : 0, 0), Pending;
    unsigned Epoch = 0;
    for (unsigned Head = 0; Head < Count; ++Head)
    {
        const auto &I = P.Instructions[Head];
        if (I.Op == LOP_FORNPREP || I.Op == LOP_FORGPREP || I.Op == LOP_FORGPREP_NEXT || I.Op == LOP_FORGPREP_INEXT)
        {
            unsigned Tail = LoopTail(I);
            if (Tail < Count)
                LoopRegionEnds[Head] = Tail + 1;
        }
        if (!HasBackedges)
            continue;
        ++Epoch;
        Mark[Head] = Epoch;
        Pending.clear();
        for (unsigned Before : Flow.Predecessors[Head])
        {
            const auto &Back = P.Instructions[Before];
            if (Before >= Head && Flow.Dominates(Head, Before) && (Jump(Back.Op) || Conditional(Back.Op)))
                Pending.push_back(Before);
        }
        if (Pending.empty())
            continue;
        unsigned End = Head + 1;
        // Reverse closure includes late latches threaded through an earlier
        // backedge, rather than assuming the last direct predecessor is the end.
        while (!Pending.empty())
        {
            unsigned Node = Pending.back();
            Pending.pop_back();
            if (Mark[Node] == Epoch)
                continue;
            Mark[Node] = Epoch;
            // Compiler jump threading can leave unreachable predecessor stubs.
            // They do not introduce a reachable entry into the natural loop.
            if (Flow.Dominators[Node] < 0)
                continue;
            End = std::max(End, Node + 1);
            if (!Flow.Dominates(Head, Node))
                throw Unstructured("irreducible loop has an external entry");
            E.Analysis.Spend(1 + Flow.Predecessors[Node].size());
            Pending.insert(Pending.end(), Flow.Predecessors[Node].begin(), Flow.Predecessors[Node].end());
        }
        NaturalLoopEnds[Head] = End;
        LoopRegionEnds[Head] = std::max(LoopRegionEnds[Head], End);
    }
}

std::vector<Statement> Function::Region(unsigned Start, unsigned End, int BreakTarget, int ContinueTarget, unsigned Level,
                                        bool IgnoreBackedge)
{
    if (Level > 150)
        throw Unstructured("control-flow nesting too deep");
    std::vector<Statement> Out;
    unsigned Pos = Index(Start), Limit = Index(End);
    if (Pos > Limit)
        throw Unstructured("region starts after its boundary: " + std::to_string(Start) + ".." + std::to_string(End));
    const int BreakDestination = BreakTarget < 0 ? -1 : int(JumpDestination(unsigned(BreakTarget)));
    const int ContinueDestination = ContinueTarget < 0 ? -1 : int(JumpDestination(unsigned(ContinueTarget)));
    while (Pos != Limit && Pos < P.Instructions.size())
    {
        E.Analysis.Spend();
        if (RegionVisits.empty())
            RegionVisits.resize(P.Instructions.size());
        if (++RegionVisits[Pos] > 12)
            throw Unstructured("shared region exceeds duplication budget at " + std::to_string(P.Instructions[Pos].Pc) + " in " +
                               std::to_string(Start) + ".." + std::to_string(End));
        const auto &I = P.Instructions[Pos];
        auto ExitRoute = [&](int Target, std::vector<Statement> &Body)
        {
            std::vector<unsigned> Cleanup;
            for (unsigned Steps = 0; Steps < 64; ++Steps)
            {
                bool IsBreak = Target >= 0 && (Target == BreakTarget || Target == BreakDestination);
                bool IsContinue = Target >= 0 && (Target == ContinueTarget || Target == ContinueDestination);
                if (IsBreak || IsContinue)
                {
                    for (unsigned Node : Cleanup)
                        Simple(P.Instructions[Node], Body);
                    Body.push_back({IsBreak ? Kind::Break : Kind::Continue});
                    return true;
                }
                if (Target < 0 || unsigned(Target) >= P.Code.size())
                    break;
                unsigned Node = Index(unsigned(Target));
                const auto &Next = P.Instructions[Node];
                E.Analysis.Spend();
                if (Next.Op == LOP_RETURN && unsigned(Target) > End)
                {
                    for (unsigned CleanupNode : Cleanup)
                        Simple(P.Instructions[CleanupNode], Body);
                    Simple(Next, Body);
                    return true;
                }
                if (Jump(Next.Op) && Next.Target() != Target)
                    Target = Next.Target();
                else if (Next.Op == LOP_CLOSEUPVALS || Next.Op == LOP_NOP || Next.Op == LOP_COVERAGE)
                {
                    Cleanup.push_back(Node);
                    Target = int(Next.Next);
                }
                else if (unsigned(Target) >= End && !Conditional(Next.Op) && Next.Op != LOP_CMPPROTO && !(Next.Op == LOP_LOADB && Next.C) &&
                         Next.Op != LOP_FORNPREP && Next.Op != LOP_FORNLOOP && Next.Op != LOP_FORGPREP && Next.Op != LOP_FORGPREP_NEXT &&
                         Next.Op != LOP_FORGPREP_INEXT && Next.Op != LOP_FORGLOOP)
                {
                    // Shared terminal blocks can live after a containing arm.
                    // Commit their effects only after proving the entire path
                    // reaches a return, instead of extending across other arms.
                    Cleanup.push_back(Node);
                    Target = int(Next.Next);
                }
                else
                    break;
            }
            return false;
        };
        if (!IgnoreBackedge && !NaturalLoopEnds.empty() && NaturalLoopEnds[Pos])
        {
            unsigned Finish = NaturalLoopEnds[Pos];
            if (Finish > Limit)
                throw Unstructured("loop leaves its containing region");
            unsigned ExitPc = Finish == P.Instructions.size() ? unsigned(P.Code.size()) : P.Instructions[Finish].Pc;
            Statement Loop{Kind::While, {}, "true"};
            Loop.Body = Region(I.Pc, ExitPc, int(ExitPc), int(I.Pc), Level + 1, true);
            if (Loop.Body.empty() ||
                (Loop.Body.back().Type != Kind::Continue && Loop.Body.back().Type != Kind::Return && Loop.Body.back().Type != Kind::Break))
                Loop.Body.push_back({Kind::Break});
            Out.push_back(std::move(Loop));
            Pos = Finish;
            continue;
        }
        IgnoreBackedge = false;
        if ((I.Op == LOP_FORNLOOP || I.Op == LOP_FORGLOOP) && ContinueTarget >= 0)
        {
            const auto &Tail = P.Instructions[Index(unsigned(ContinueTarget))];
            if (Tail.Op == I.Op && Tail.A == I.A && Tail.Target() == I.Target() && (I.Op != LOP_FORGLOOP || Tail.Aux == I.Aux) &&
                EquivalentExit(Tail.Next, I.Next))
            {
                Out.push_back({Kind::Continue});
                break;
            }
        }
        if (I.Op == LOP_FORNPREP)
        {
            unsigned TailIndex = LoopTail(I);
            if (TailIndex >= Limit)
                throw Unstructured("numeric loop leaves region");
            const auto &Tail = P.Instructions[TailIndex];
            if (!EquivalentExit(Tail.Next, unsigned(I.Target())))
                throw Unstructured("noncanonical numeric loop");
            unsigned Finish = TailIndex + 1;
            for (unsigned R = I.A; R < I.A + 3; ++R)
                if (Flow.LiveIn[Finish].test(R))
                    throw Unstructured("numeric loop exposes its hidden state");
            for (unsigned K = Pos + 1; K < TailIndex; ++K)
            {
                const auto &Body = P.Instructions[K];
                E.Analysis.Spend(1 + Flow.Predecessors[K].size());
                for (unsigned Before : Flow.Predecessors[K])
                    if (Flow.Dominators[Before] >= 0 && (Before < Pos || Before > TailIndex))
                        throw Unstructured("numeric loop has an external entry");
                for (unsigned After : Flow.Successors[K])
                    if (After <= Pos || After > TailIndex)
                        for (unsigned R = I.A; R < I.A + 3; ++R)
                            if (Flow.LiveIn[After].test(R))
                                throw Unstructured("numeric loop exposes hidden state on an exit");
                if (Body.Op == LOP_FORNLOOP && Body.A == Tail.A && Body.Target() == Tail.Target() && EquivalentExit(Body.Next, Tail.Next))
                    continue;
                if (Body.Op != LOP_CLOSEUPVALS)
                    for (unsigned R : Reads(Body, P))
                        if (R >= I.A && R < I.A + 2)
                            throw Unstructured("numeric loop reads its hidden control state");
                for (unsigned R : Writes(Body, P))
                    if (R >= I.A && R < I.A + 3)
                        throw Unstructured("numeric loop modifies its hidden control state");
            }
            unsigned Counter = E.New("Index", Id, true, true);
            Statement Loop{Kind::For};
            unsigned Continuation = LoopContinuation(Pos, TailIndex, Limit, Out);
            Loop.Left = {Counter};
            Loop.Text = Token(Counter) + " = " + Read(I.A + 2, I.Pc) + ", " + Read(I.A, I.Pc) + ", " + Read(I.A + 1, I.Pc);
            int BreakPc = Continuation == Finish
                              ? I.Target()
                              : int(Continuation == P.Instructions.size() ? P.Code.size() : P.Instructions[Continuation].Pc);
            Loop.Body = Region(I.Next, Tail.Pc, BreakPc, int(Tail.Pc), Level + 1);
            Loop.Body.insert(Loop.Body.begin(), Assign(I.A + 2, P.Instructions[Pos + 1], Token(Counter), true));
            Out.push_back(std::move(Loop));
            Pos = Continuation;
            continue;
        }
        if (I.Op == LOP_FORGPREP || I.Op == LOP_FORGPREP_NEXT || I.Op == LOP_FORGPREP_INEXT)
        {
            unsigned Finish = LoopTail(I);
            if (Finish >= Limit)
                throw Unstructured("generic loop leaves region");
            const auto &Tail = P.Instructions[Finish];
            if (Tail.Op != LOP_FORGLOOP || Tail.A != I.A || Tail.Target() != int(I.Next))
                throw Unstructured("noncanonical generic loop");
            std::string Failure;
            if (!NativeGenericLoop(P, Flow, Pos, Finish, E.Analysis, &Failure))
                throw Unstructured("generic loop at " + std::to_string(I.Pc) + ": " + Failure);
            Statement Loop{Kind::For};
            unsigned Continuation = LoopContinuation(Pos, Finish, Limit, Out);
            std::vector<std::string> Vars;
            for (unsigned J = 0; J < (Tail.Aux & 255); ++J)
            {
                unsigned Var = E.New(J == 0 ? "Key" : J == 1 ? "Value" : "Item" + std::to_string(J + 1), Id, true, true);
                Vars.push_back(Token(Var));
                Loop.Left.push_back(Var);
            }
            Loop.Text = Join(Vars) + " in " + Read(I.A, I.Pc) + ", " + Read(I.A + 1, I.Pc) + ", " + Read(I.A + 2, I.Pc);
            int BreakPc = Continuation == Finish + 1
                              ? int(Tail.Next)
                              : int(Continuation == P.Instructions.size() ? P.Code.size() : P.Instructions[Continuation].Pc);
            Loop.Body = Region(I.Next, Tail.Pc, BreakPc, int(Tail.Pc), Level + 1);
            for (unsigned J = unsigned(Vars.size()); J-- > 0;)
                Loop.Body.insert(Loop.Body.begin(), Assign(I.A + 3 + J, P.Instructions[Pos + 1], Vars[J], true));
            Out.push_back(std::move(Loop));
            Pos = Continuation;
            continue;
        }
        if (Conditional(I.Op))
        {
            auto Target = I.Target();
            // Comparisons lowered to LOADB/skip/LOADB form a single boolean value.
            if (Pos + 2 < Limit)
            {
                const auto &False = P.Instructions[Pos + 1];
                const auto &True = P.Instructions[Pos + 2];
                if (False.Op == LOP_LOADB && True.Op == LOP_LOADB && False.A == True.A && False.B != True.B && Target == int(True.Pc) &&
                    False.Target() == int(True.Next) && !True.C)
                {
                    std::string Value = Condition(I);
                    if (!True.B)
                        Value = "not (" + Value + ")";
                    else if (I.Op == LOP_JUMPIF)
                        Value = "not not (" + Value + ")";
                    Out.push_back(Assign(False.A, False, "(" + Value + ")"));
                    Pos += 3;
                    continue;
                }
            }
            // Adjacent tests with the same destination form one short-circuit
            // condition. Reconstructing their arms independently duplicates the
            // same body once per test and can turn a small, reducible condition
            // into a state machine after exhausting the region-visit budget.
            unsigned LastTest = Pos;
            std::string Test;
            unsigned Fallthrough = I.Next;
            if (!PredicateRegion(Pos, Limit, LastTest, Fallthrough, Target, Test))
            {
                struct Predicate
                {
                    unsigned First, Test;
                    bool Invert;
                };
                std::vector<Predicate> Predicates;
                unsigned First = Pos + 1, Candidate = First;
                bool FinalInverted = false;
                unsigned BooleanJoin = Limit;
                while (Candidate < Limit && (Target <= int(I.Pc) || P.Instructions[Candidate].Pc < unsigned(Target)))
                {
                    const auto &Next = P.Instructions[Candidate];
                    const auto &Incoming = Flow.Predecessors[Candidate];
                    if (Candidate == BooleanJoin)
                    {
                        if (Incoming.size() != 2 || std::any_of(Incoming.begin(), Incoming.end(), [&](unsigned Before)
                                                                { return Before + 1 != Candidate && Before + 2 != Candidate; }))
                            break;
                    }
                    else if (Incoming.size() != 1 || Incoming[0] != Candidate - 1)
                        break;
                    if (Conditional(Next.Op))
                    {
                        // O0 materializes comparison arguments with LOADB/skip/LOADB.
                        // Keep that complete diamond inside predicate setup, then
                        // continue looking for the shared short-circuit destination.
                        if (Candidate + 2 < Limit)
                        {
                            const auto &False = P.Instructions[Candidate + 1];
                            const auto &True = P.Instructions[Candidate + 2];
                            if (False.Op == LOP_LOADB && True.Op == LOP_LOADB && False.A == True.A && False.B != True.B &&
                                Next.Target() == int(True.Pc) && False.Target() == int(True.Next) && !True.C &&
                                Flow.Predecessors[Candidate + 1] == std::vector<unsigned>{Candidate} &&
                                Flow.Predecessors[Candidate + 2] == std::vector<unsigned>{Candidate})
                            {
                                Candidate += 3;
                                BooleanJoin = Candidate;
                                continue;
                            }
                        }
                        bool Invert = Next.Target() != Target && int(Next.Next) == Target;
                        if (Next.Target() != Target && !Invert)
                            break;
                        Predicates.push_back({First, Candidate, Invert});
                        First = ++Candidate;
                        if (Invert)
                        {
                            FinalInverted = true;
                            break;
                        }
                        continue;
                    }
                    if (Jump(Next.Op) || Next.Op == LOP_RETURN || Next.Op == LOP_CMPPROTO || Next.Op == LOP_FORNPREP ||
                        Next.Op == LOP_FORNLOOP || Next.Op == LOP_FORGPREP || Next.Op == LOP_FORGPREP_NEXT ||
                        Next.Op == LOP_FORGPREP_INEXT || Next.Op == LOP_FORGLOOP || (Next.Op == LOP_LOADB && Next.C))
                        break;
                    E.Analysis.Spend();
                    ++Candidate;
                }
                // Preserve effectful predicate setup in short-circuit guards while
                // reconstructing their shared destination only once. Later ordinary
                // simplification can recover a compact and/or expression.
                if (!Predicates.empty() &&
                    std::any_of(Predicates.begin(), Predicates.end(), [](const auto &Part) { return Part.First != Part.Test; }))
                {
                    unsigned Match = E.New("Condition", Id);
                    Out.push_back({Kind::Assign, {Match}, "(" + Condition(I) + ")"});
                    for (const auto &Part : Predicates)
                    {
                        Statement Guard{Kind::If, {}, "not " + Token(Match)};
                        Guard.Body =
                            Region(P.Instructions[Part.First].Pc, P.Instructions[Part.Test].Pc, BreakTarget, ContinueTarget, Level + 1);
                        auto Value = Condition(P.Instructions[Part.Test]);
                        Guard.Body.push_back({Kind::Assign, {Match}, "(" + (Part.Invert ? Negate(Value) : Value) + ")"});
                        Out.push_back(std::move(Guard));
                        LastTest = Part.Test;
                    }
                    Test = Token(Match);
                    Fallthrough = P.Instructions[LastTest].Next;
                    if (FinalInverted)
                    {
                        Test = "not " + Test;
                        Target = P.Instructions[LastTest].Target();
                    }
                }
                else
                {
                    std::vector<std::string> Tests{Condition(I)};
                    auto FollowingTest = [&](unsigned Before) -> std::pair<unsigned, std::string>
                    {
                        std::map<unsigned, std::string> Literals;
                        for (unsigned Candidate = Before + 1; Candidate < Limit; ++Candidate)
                        {
                            const auto &Next = P.Instructions[Candidate];
                            if (Flow.Predecessors[Candidate].size() != 1 || Flow.Predecessors[Candidate][0] != Candidate - 1)
                                break;
                            if (Conditional(Next.Op))
                            {
                                auto Text = Condition(Next);
                                for (const auto &[R, Value] : Literals)
                                {
                                    if (Flow.LiveIn[Candidate + 1].test(R) || Flow.LiveIn[Index(unsigned(Next.Target()))].test(R))
                                        return {Limit, {}};
                                    Replace(Text, Read(R, Next.Pc), "(" + Value + ")");
                                }
                                return {Candidate, std::move(Text)};
                            }
                            // O0 bytecode loads comparison constants into temporary
                            // registers. Inline only dead, uncaptured literal loads;
                            // never move a call, property access, or shared local write.
                            if (Cells.count(Next.A) || ScalarReferences.count(Next.A))
                                break;
                            switch (Next.Op)
                            {
                            case LOP_LOADNIL:
                                Literals[Next.A] = "nil";
                                break;
                            case LOP_LOADB:
                                if (Next.C)
                                    return {Limit, {}};
                                Literals[Next.A] = Next.B ? "true" : "false";
                                break;
                            case LOP_LOADN:
                                Literals[Next.A] = std::to_string(Next.D);
                                break;
                            case LOP_LOADK:
                            case LOP_LOADKX:
                            {
                                unsigned K = Next.Op == LOP_LOADK ? unsigned(Next.D) : Next.Aux;
                                auto Tag = P.Constants[K].Tag;
                                if (Tag != LBC_CONSTANT_NIL && Tag != LBC_CONSTANT_BOOLEAN && Tag != LBC_CONSTANT_NUMBER &&
                                    Tag != LBC_CONSTANT_STRING)
                                    return {Limit, {}};
                                Literals[Next.A] = ConstantAt(K);
                                break;
                            }
                            default:
                                return {Limit, {}};
                            }
                            E.Analysis.Spend();
                        }
                        return {Limit, {}};
                    };
                    auto NextTest = FollowingTest(LastTest);
                    while (NextTest.first < Limit && P.Instructions[NextTest.first].Target() == Target)
                    {
                        Tests.push_back(std::move(NextTest.second));
                        LastTest = NextTest.first;
                        E.Analysis.Spend();
                        NextTest = FollowingTest(LastTest);
                    }
                    Test = Tests.size() == 1 ? Tests.front() : "(" + Join(Tests, ") or (") + ")";
                    Fallthrough = P.Instructions[LastTest].Next;
                    // Compilers commonly invert the final OR test so its true arm falls
                    // through. Include that last test as well, keeping a single copy of
                    // the body (including loops and closure definitions).
                    if (NextTest.first < Limit)
                    {
                        const auto &Next = P.Instructions[NextTest.first];
                        if (int(Next.Next) == Target)
                        {
                            Test = "not ((" + Test + ") or (not (" + NextTest.second + ")))";
                            Target = Next.Target();
                            Fallthrough = Next.Next;
                            LastTest = NextTest.first;
                            E.Analysis.Spend();
                        }
                    }
                }
            }
            Statement Branch{Kind::If, {}, std::move(Test)};
            if (!ExitRoute(Target, Branch.Body))
            {
                if (Target <= int(I.Pc))
                    throw Unstructured("conditional crosses region");
                // The lexical false arm is also a valid continuation when paths in the first
                // arm terminate early. A strict postdominator would move that continuation to
                // the function exit and duplicate the remainder of the function.
                unsigned JoinPc = unsigned(Target);
                for (unsigned Scan = LastTest + 1; Scan < Index(std::min(JoinPc, End)); ++Scan)
                {
                    const auto &Edge = P.Instructions[Scan];
                    if (Edge.Target() <= Target || Edge.Target() == BreakTarget || Edge.Target() == ContinueTarget)
                        continue;
                    const auto &Destination = P.Instructions[Index(unsigned(Edge.Target()))];
                    if (Destination.Op == LOP_RETURN)
                        continue;
                    JoinPc = std::max(JoinPc, unsigned(Edge.Target()));
                }
                int Post = Flow.PostDominators[Pos];
                if (Post >= 0 && unsigned(Post) < P.Instructions.size() &&
                    P.Instructions[unsigned(Post)].Pc >= std::max(unsigned(Target), Fallthrough))
                    JoinPc = std::min(JoinPc, P.Instructions[unsigned(Post)].Pc);
                JoinPc = std::min(JoinPc, End);
                // A join cannot bisect a loop, including a loop whose exit was
                // threaded past a jump. Expand to the physical loop boundary.
                unsigned JoinIndex = Index(JoinPc);
                for (unsigned Scan = LastTest + 1; !LoopRegionEnds.empty() && Scan < JoinIndex; ++Scan)
                    if (LoopRegionEnds[Scan] > JoinIndex)
                    {
                        JoinIndex = LoopRegionEnds[Scan];
                        if (JoinIndex > Limit)
                            throw Unstructured("branch cuts a containing loop");
                        JoinPc = JoinIndex == P.Instructions.size() ? unsigned(P.Code.size()) : P.Instructions[JoinIndex].Pc;
                    }
                if (JoinPc <= I.Pc)
                    throw Unstructured("branch rejoins a loop header at " + std::to_string(I.Pc) + " -> " + std::to_string(JoinPc) +
                                       " end " + std::to_string(End));
                Branch.Text = "not (" + Branch.Text + ")";
                Branch.Body = Region(Fallthrough, JoinPc, BreakTarget, ContinueTarget, Level + 1);
                if (JoinPc != unsigned(Target))
                    Branch.Else = Region(unsigned(Target), JoinPc, BreakTarget, ContinueTarget, Level + 1);
                Out.push_back(std::move(Branch));
                Pos = Index(JoinPc);
                continue;
            }
            Out.push_back(std::move(Branch));
            Pos = LastTest + 1;
            continue;
        }
        if (Jump(I.Op) || (I.Op == LOP_LOADB && I.C) || I.Op == LOP_CMPPROTO)
        {
            if (I.Op == LOP_LOADB)
                Simple(I, Out);
            auto Target = I.Target();
            if (ExitRoute(Target, Out))
                break;
            if (Target < int(I.Next))
                throw Unstructured("jump at " + std::to_string(I.Pc) + " -> " + std::to_string(Target) + " crosses region " +
                                   std::to_string(Start) + ".." + std::to_string(End));
            // CMPPROTO's generic fallback is always valid, independent of runtime-specific proto ids.
            Pos = Index(unsigned(Target));
            continue;
        }
        Simple(I, Out);
        if (I.Op == LOP_RETURN)
            break;
        ++Pos;
    }
    return Out;
}

std::vector<Statement> Function::StateMachine(std::string_view Reason)
{
    Dispatch = true;
    for (unsigned R : ScalarReferences)
    {
        Cells.insert(R);
        E.Symbols[Registers[R]].Cell = true;
    }
    ScalarReferences.clear();
    CountDefinitions();
    PendingMethod.clear();
    PendingReceiver.clear();
    PendingBase = 0;
    OpenExpression.clear();
    OpenExpressionBase = 0;
    OpenHint.clear();
    unsigned PcSymbol = E.New("ProgramCounter", Id, true);
    std::vector<Statement> Out;
    if (!RecoveredInline)
        Out.push_back({Kind::Raw,
                       {},
                       "-- Control flow required an explicit state machine. Function " + std::to_string(Id) + ": " + std::string(Reason)});
    Out.push_back({Kind::Assign, {PcSymbol}, "0"});
    Statement Loop{Kind::While, {}, "true"};
    std::map<unsigned, unsigned> IteratorSymbols;
    std::map<unsigned, unsigned> NativeLoops;
    for (unsigned J = 0; J < P.Instructions.size(); ++J)
    {
        const auto &I = P.Instructions[J];
        if (I.Op != LOP_FORGPREP && I.Op != LOP_FORGPREP_NEXT && I.Op != LOP_FORGPREP_INEXT)
            continue;
        unsigned TailIndex = Index(unsigned(I.Target()));
        if (NativeGenericLoop(P, Flow, J, TailIndex, E.Analysis, nullptr, false))
            NativeLoops[J] = TailIndex;
    }
    // Noncanonical bytecode may enter a loop body from outside its preparation
    // or inspect hidden iterator registers. Keep the resumable helper only for
    // those shapes; ordinary loops run as native for loops in the same coroutine.
    for (unsigned J = 0; J < P.Instructions.size(); ++J)
    {
        const auto &I = P.Instructions[J];
        if (I.Op == LOP_FORGPREP || I.Op == LOP_FORGPREP_NEXT || I.Op == LOP_FORGPREP_INEXT)
        {
            if (NativeLoops.count(J))
                continue;
            if (!IteratorSymbols.count(I.A))
            {
                unsigned S = E.New("Iterator", Id, true);
                IteratorSymbols[I.A] = S;
                Out.push_back({Kind::Raw, {}, "local " + Token(S)});
            }
        }
    }
    if (!IteratorSymbols.empty())
        E.Output.Diagnostics.push_back("Function " + std::to_string(Id) +
                                       ": noncanonical generic iteration uses a coroutine helper; generalized iterator yields and "
                                       "coroutine identity may differ");
    std::function<std::vector<Statement>(unsigned, unsigned)> States = [&](unsigned Begin, unsigned End)
    {
        std::vector<Statement> Cases;
        for (unsigned Index = Begin; Index < End; ++Index)
        {
            const auto &I = P.Instructions[Index];
            Statement Branch{Kind::If, {}, Token(PcSymbol) + " == " + std::to_string(I.Pc)};
            auto SetPc = [&](int Target) { Branch.Body.push_back({Kind::Raw, {}, Token(PcSymbol) + " = " + std::to_string(Target)}); };
            auto R = [&](unsigned Reg) { return Read(Reg, I.Pc); };
            if (Conditional(I.Op))
            {
                Statement Test{Kind::If, {}, Condition(I)};
                Test.Body.push_back({Kind::Raw, {}, Token(PcSymbol) + " = " + std::to_string(I.Target())});
                Test.Else.push_back({Kind::Raw, {}, Token(PcSymbol) + " = " + std::to_string(I.Next)});
                Branch.Body.push_back(std::move(Test));
            }
            else if (Jump(I.Op) || I.Op == LOP_CMPPROTO)
                SetPc(I.Target());
            else if (I.Op == LOP_FORNPREP || I.Op == LOP_FORNLOOP)
            {
                if (I.Op == LOP_FORNPREP)
                    for (unsigned J = 0; J < 3; ++J)
                        Branch.Body.push_back({Kind::Raw, {}, R(I.A + J) + " = tonumber(" + R(I.A + J) + ")"});
                else
                    Branch.Body.push_back({Kind::Raw, {}, R(I.A + 2) + " = " + R(I.A + 2) + " + " + R(I.A + 1)});
                std::string Condition = "(" + R(I.A + 1) + " > 0 and " + R(I.A + 2) + " <= " + R(I.A) + ") or (not (" + R(I.A + 1) +
                                        " > 0) and " + R(I.A) + " <= " + R(I.A + 2) + ")";
                Statement Test{Kind::If, {}, Condition};
                Test.Body.push_back(
                    {Kind::Raw, {}, Token(PcSymbol) + " = " + std::to_string(I.Op == LOP_FORNPREP ? int(I.Next) : I.Target())});
                Test.Else.push_back(
                    {Kind::Raw, {}, Token(PcSymbol) + " = " + std::to_string(I.Op == LOP_FORNPREP ? I.Target() : int(I.Next))});
                Branch.Body.push_back(std::move(Test));
            }
            else if (I.Op == LOP_FORGPREP || I.Op == LOP_FORGPREP_NEXT || I.Op == LOP_FORGPREP_INEXT)
            {
                const auto &Tail = P.Instructions.at(this->Index(unsigned(I.Target())));
                if (Tail.Op != LOP_FORGLOOP)
                    throw Error("Generic preparation does not target FORGLOOP");
                auto Native = NativeLoops.find(Index);
                if (Native != NativeLoops.end() && Native->second < End)
                {
                    Statement For{Kind::For};
                    std::vector<std::string> Vars;
                    for (unsigned J = 0; J < (Tail.Aux & 255); ++J)
                    {
                        auto S = E.New(J == 0 ? "Key" : J == 1 ? "Value" : "Item" + std::to_string(J + 1), Id, true, true);
                        For.Left.push_back(S);
                        Vars.push_back(Token(S));
                        For.Body.push_back({Kind::Raw, {}, R(I.A + 3 + J) + " = " + Token(S)});
                    }
                    For.Text = Join(Vars) + " in " + R(I.A) + ", " + R(I.A + 1) + ", " + R(I.A + 2);
                    For.Body.push_back({Kind::Raw, {}, Token(PcSymbol) + " = " + std::to_string(I.Next)});
                    Statement Body{Kind::While,
                                   {},
                                   Token(PcSymbol) + " >= " + std::to_string(I.Next) + " and " + Token(PcSymbol) + " < " +
                                       std::to_string(Tail.Pc)};
                    Body.Body = States(Index + 1, Native->second);
                    Body.Body.push_back({Kind::Raw, {}, "error(\"Invalid decompiled loop state\")"});
                    For.Body.push_back(std::move(Body));
                    Statement Exit{Kind::If, {}, Token(PcSymbol) + " ~= " + std::to_string(Tail.Pc)};
                    Exit.Body.push_back({Kind::Break});
                    For.Body.push_back(std::move(Exit));
                    SetPc(int(Tail.Pc));
                    Branch.Body.push_back(std::move(For));
                    Statement Complete{Kind::If, {}, Token(PcSymbol) + " == " + std::to_string(Tail.Pc)};
                    Complete.Body.push_back({Kind::Raw, {}, Token(PcSymbol) + " = " + std::to_string(Tail.Next)});
                    Branch.Body.push_back(std::move(Complete));
                    Index = Native->second;
                }
                else
                {
                    std::vector<std::string> Vars;
                    for (unsigned J = 0; J < (Tail.Aux & 255); ++J)
                        Vars.push_back("Item" + std::to_string(J + 1));
                    auto Text = Token(IteratorSymbols.at(I.A)) + " = coroutine.wrap(function()\n    for " + Join(Vars) + " in " + R(I.A) +
                                ", " + R(I.A + 1) + ", " + R(I.A + 2) + " do\n        coroutine.yield(true, " + Join(Vars) +
                                ")\n    end\n    return false\nend)";
                    Statement Prepare{Kind::If, {}, "type(" + R(I.A) + ") == \"function\""};
                    Prepare.Body.push_back({Kind::Raw, {}, Token(IteratorSymbols.at(I.A)) + " = nil"});
                    Prepare.Else.push_back({Kind::Raw, {}, std::move(Text)});
                    Branch.Body.push_back(std::move(Prepare));
                    SetPc(I.Target());
                }
            }
            else if (I.Op == LOP_FORGLOOP)
            {
                auto It = IteratorSymbols.find(I.A);
                if (It == IteratorSymbols.end())
                    throw Error("Generic loop has no preparation");
                unsigned HasNext = E.New("HasNext", Id, true);
                Branch.Body.push_back({Kind::Raw, {}, "local " + Token(HasNext)});
                std::vector<std::string> Vars{Token(HasNext)};
                for (unsigned J = 0; J < (I.Aux & 255); ++J)
                    Vars.push_back(R(I.A + 3 + J));
                Statement Advance{Kind::If, {}, Token(It->second)};
                Advance.Body.push_back({Kind::Raw, {}, Join(Vars) + " = " + Token(It->second) + "()"});
                Advance.Else.push_back({Kind::Raw,
                                        {},
                                        Join(std::vector<std::string>(Vars.begin() + 1, Vars.end())) + " = " + R(I.A) + "(" + R(I.A + 1) +
                                            ", " + R(I.A + 2) + ")"});
                Advance.Else.push_back({Kind::Raw, {}, Token(HasNext) + " = " + R(I.A + 3) + " ~= nil"});
                Branch.Body.push_back(std::move(Advance));
                Branch.Body.push_back({Kind::Raw, {}, R(I.A + 2) + " = " + R(I.A + 3)});
                Statement Test{Kind::If, {}, Token(HasNext)};
                Test.Body.push_back({Kind::Raw, {}, Token(PcSymbol) + " = " + std::to_string(I.Target())});
                Test.Else.push_back({Kind::Raw, {}, Token(PcSymbol) + " = " + std::to_string(I.Next)});
                Branch.Body.push_back(std::move(Test));
            }
            else if (I.Op == LOP_NAMECALL || I.Op == LOP_NAMECALLUDATA)
            {
                // Keep preparation and call in a single state so __namecall survives the lowering.
                Simple(I, Branch.Body);
                unsigned J = this->Index(I.Next);
                for (; J < P.Instructions.size(); ++J)
                {
                    auto Op = P.Instructions[J].Op;
                    if (Conditional(Op) || Jump(Op) || Op == LOP_RETURN)
                        throw Error("NAMECALL is not paired with CALL");
                    Simple(P.Instructions[J], Branch.Body);
                    if (Op == LOP_CALL || Op == LOP_CALLFB)
                    {
                        SetPc(int(P.Instructions[J].Next));
                        break;
                    }
                }
            }
            else
            {
                Simple(I, Branch.Body);
                if (I.Op != LOP_RETURN)
                    SetPc(I.Op == LOP_LOADB && I.C ? I.Target() : int(I.Next));
            }
            if (I.Op != LOP_RETURN)
                Branch.Body.push_back({Kind::Continue});
            Cases.push_back(std::move(Branch));
        }
        return Cases;
    };
    Loop.Body = States(0, unsigned(P.Instructions.size()));
    Loop.Body.push_back({Kind::Raw, {}, "error(\"Invalid decompiled control-flow state\")"});
    Out.push_back(std::move(Loop));
    return Out;
}

} // namespace Taze::Detail
