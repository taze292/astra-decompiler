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
bool NativeGenericLoop(const Prototype &P, const ControlFlow &Flow, unsigned Head, unsigned Tail, AnalysisBudget &Budget)
{
    const auto &I = P.Instructions[Head];
    const auto &End = P.Instructions[Tail];
    if (Tail <= Head || End.Op != LOP_FORGLOOP || End.A != I.A || End.Target() != int(I.Next))
        return false;
    // The native for statement owns its hidden iterator state. The bytecode
    // must not inspect that state or enter the body without running preparation.
    for (unsigned R = I.A; R < I.A + 3 + (End.Aux & 255); ++R)
        if (Flow.LiveIn[Tail + 1].test(R))
            return false;
    for (unsigned K = Head + 1; K <= Tail; ++K)
    {
        Budget.Spend(1 + Flow.Predecessors[K].size() + Flow.Successors[K].size());
        for (unsigned Before : Flow.Predecessors[K])
            if (Before < Head || Before > Tail)
                return false;
        for (unsigned After : Flow.Successors[K])
            if (After <= Head || After > Tail)
                for (unsigned R = I.A; R < I.A + 3; ++R)
                    if (Flow.LiveIn[After].test(R))
                        return false;
        if (K == Tail)
            continue;
        const auto &Body = P.Instructions[K];
        if (Body.Op != LOP_CLOSEUPVALS)
            for (unsigned R : Reads(Body, P))
                if (R >= I.A && R < I.A + 3)
                    return false;
        for (unsigned R : Writes(Body, P))
            if (R >= I.A && R < I.A + 3)
                return false;
    }
    return true;
}
} // namespace

std::vector<Statement> Function::Region(unsigned Start, unsigned End, int BreakTarget, int ContinueTarget, unsigned Level,
                                        bool IgnoreBackedge)
{
    if (Level > 150)
        throw Unstructured("control-flow nesting too deep");
    std::vector<Statement> Out;
    unsigned Pos = Index(Start), Limit = Index(End);
    while (Pos != Limit && Pos < P.Instructions.size())
    {
        E.Analysis.Spend();
        if (RegionVisits.empty())
            RegionVisits.resize(P.Instructions.size());
        if (++RegionVisits[Pos] > 12)
            throw Unstructured("shared region exceeds duplication budget");
        const auto &I = P.Instructions[Pos];
        // A backedge to this instruction defines a while/repeat region. Choose the outermost backedge.
        if (!IgnoreBackedge)
        {
            int Back = -1;
            for (unsigned J : Flow.Predecessors[Pos])
                if (J > Pos && J < Limit && P.Instructions[J].Target() == int(I.Pc) && Flow.Dominates(Pos, J) &&
                    (Jump(P.Instructions[J].Op) || Conditional(P.Instructions[J].Op)))
                    Back = std::max(Back, int(J));
            if (Back >= 0)
            {
                const auto &Tail = P.Instructions[unsigned(Back)];
                Statement Loop;
                Loop.Type = Kind::While;
                Loop.Text = "true";
                Loop.Body = Region(I.Pc, Tail.Pc, int(Tail.Next), int(I.Pc), Level + 1, true);
                if (Conditional(Tail.Op))
                {
                    Loop.Type = Kind::Repeat;
                    Loop.Text = "not (" + Condition(Tail) + ")";
                }
                Out.push_back(std::move(Loop));
                Pos = unsigned(Back) + 1;
                continue;
            }
        }
        IgnoreBackedge = false;
        if (I.Op == LOP_FORNPREP)
        {
            unsigned Finish = Index(unsigned(I.Target()));
            if (Finish == 0 || Finish > Limit)
                throw Unstructured("numeric loop leaves region");
            const auto &Tail = P.Instructions[Finish - 1];
            if (Tail.Op != LOP_FORNLOOP || Tail.A != I.A || Tail.Target() != int(I.Next))
                throw Unstructured("noncanonical numeric loop");
            for (unsigned R = I.A; R < I.A + 3; ++R)
                if (Flow.LiveIn[Finish].test(R))
                    throw Unstructured("numeric loop exposes its hidden state");
            unsigned Counter = E.New("Index", Id, true, true);
            Statement Loop{Kind::For};
            Loop.Left = {Counter};
            Loop.Text = Token(Counter) + " = " + Read(I.A + 2, I.Pc) + ", " + Read(I.A, I.Pc) + ", " + Read(I.A + 1, I.Pc);
            Loop.Body = Region(I.Next, Tail.Pc, I.Target(), int(Tail.Pc), Level + 1);
            Loop.Body.insert(Loop.Body.begin(), Assign(I.A + 2, P.Instructions[Pos + 1], Token(Counter), true));
            Out.push_back(std::move(Loop));
            Pos = Finish;
            continue;
        }
        if (I.Op == LOP_FORGPREP || I.Op == LOP_FORGPREP_NEXT || I.Op == LOP_FORGPREP_INEXT)
        {
            unsigned Finish = Index(unsigned(I.Target()));
            if (Finish >= Limit)
                throw Unstructured("generic loop leaves region");
            const auto &Tail = P.Instructions[Finish];
            if (Tail.Op != LOP_FORGLOOP || Tail.A != I.A || Tail.Target() != int(I.Next))
                throw Unstructured("noncanonical generic loop");
            if (!NativeGenericLoop(P, Flow, Pos, Finish, E.Analysis))
                throw Unstructured("generic loop has external entries or observable hidden state");
            Statement Loop{Kind::For};
            std::vector<std::string> Vars;
            for (unsigned J = 0; J < (Tail.Aux & 255); ++J)
            {
                unsigned Var = E.New(J == 0 ? "Key" : J == 1 ? "Value" : "Item" + std::to_string(J + 1), Id, true, true);
                Vars.push_back(Token(Var));
                Loop.Left.push_back(Var);
            }
            Loop.Text = Join(Vars) + " in " + Read(I.A, I.Pc) + ", " + Read(I.A + 1, I.Pc) + ", " + Read(I.A + 2, I.Pc);
            Loop.Body = Region(I.Next, Tail.Pc, int(Tail.Next), int(Tail.Pc), Level + 1);
            for (unsigned J = unsigned(Vars.size()); J-- > 0;)
                Loop.Body.insert(Loop.Body.begin(), Assign(I.A + 3 + J, P.Instructions[Pos + 1], Vars[J], true));
            Out.push_back(std::move(Loop));
            Pos = Finish + 1;
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
            std::string Test = Tests.size() == 1 ? Tests.front() : "(" + Join(Tests, ") or (") + ")";
            unsigned Fallthrough = P.Instructions[LastTest].Next;
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
            Statement Branch{Kind::If, {}, std::move(Test)};
            if (Target == BreakTarget)
                Branch.Body.push_back({Kind::Break});
            else if (Target == ContinueTarget)
                Branch.Body.push_back({Kind::Continue});
            else
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
                if (Post >= 0 && unsigned(Post) < P.Instructions.size())
                    JoinPc = std::min(JoinPc, P.Instructions[unsigned(Post)].Pc);
                JoinPc = std::min(JoinPc, End);
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
            if (Target == BreakTarget)
            {
                Out.push_back({Kind::Break});
                break;
            }
            if (Target == ContinueTarget)
            {
                Out.push_back({Kind::Continue});
                break;
            }
            if (Target < int(I.Next))
                throw Unstructured("jump crosses region");
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

std::vector<Statement> Function::StateMachine()
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
    std::vector<Statement> Out{{Kind::Raw, {}, "-- Control flow required an explicit state machine."}, {Kind::Assign, {PcSymbol}, "0"}};
    Statement Loop{Kind::While, {}, "true"};
    std::map<unsigned, unsigned> IteratorSymbols;
    std::map<unsigned, unsigned> NativeLoops;
    for (unsigned J = 0; J < P.Instructions.size(); ++J)
    {
        const auto &I = P.Instructions[J];
        if (I.Op != LOP_FORGPREP && I.Op != LOP_FORGPREP_NEXT && I.Op != LOP_FORGPREP_INEXT)
            continue;
        unsigned TailIndex = Index(unsigned(I.Target()));
        if (NativeGenericLoop(P, Flow, J, TailIndex, E.Analysis))
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
