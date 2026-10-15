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
            Statement Branch{Kind::If, {}, Condition(I)};
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
                for (unsigned Scan = Pos + 1; Scan < Index(std::min(JoinPc, End)); ++Scan)
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
                Branch.Body = Region(I.Next, JoinPc, BreakTarget, ContinueTarget, Level + 1);
                if (JoinPc != unsigned(Target))
                    Branch.Else = Region(unsigned(Target), JoinPc, BreakTarget, ContinueTarget, Level + 1);
                Out.push_back(std::move(Branch));
                Pos = Index(JoinPc);
                continue;
            }
            Out.push_back(std::move(Branch));
            ++Pos;
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
    PendingMethod.clear();
    unsigned PcSymbol = E.New("ProgramCounter", Id, true);
    std::vector<Statement> Out{{Kind::Raw, {}, "-- Control flow required an explicit state machine."}, {Kind::Assign, {PcSymbol}, "0"}};
    Statement Loop{Kind::While, {}, "true"};
    std::map<unsigned, unsigned> IteratorSymbols;
    // Generic iteration uses native Luau iteration inside a resumable iterator, including __iter.
    for (const auto &I : P.Instructions)
        if (I.Op == LOP_FORGPREP || I.Op == LOP_FORGPREP_NEXT || I.Op == LOP_FORGPREP_INEXT)
        {
            unsigned S = E.New("Iterator", Id, true);
            IteratorSymbols[I.A] = S;
            Out.push_back({Kind::Raw, {}, "local " + Token(S)});
        }
    for (const auto &I : P.Instructions)
    {
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
            Test.Body.push_back({Kind::Raw, {}, Token(PcSymbol) + " = " + std::to_string(I.Op == LOP_FORNPREP ? int(I.Next) : I.Target())});
            Test.Else.push_back({Kind::Raw, {}, Token(PcSymbol) + " = " + std::to_string(I.Op == LOP_FORNPREP ? I.Target() : int(I.Next))});
            Branch.Body.push_back(std::move(Test));
        }
        else if (I.Op == LOP_FORGPREP || I.Op == LOP_FORGPREP_NEXT || I.Op == LOP_FORGPREP_INEXT)
        {
            const auto &Tail = P.Instructions.at(Index(unsigned(I.Target())));
            if (Tail.Op != LOP_FORGLOOP)
                throw Error("Generic preparation does not target FORGLOOP");
            std::vector<std::string> Vars;
            for (unsigned J = 0; J < (Tail.Aux & 255); ++J)
                Vars.push_back("Item" + std::to_string(J + 1));
            auto Text = Token(IteratorSymbols.at(I.A)) + " = coroutine.wrap(function()\n    for " + Join(Vars) + " in " + R(I.A) + ", " +
                        R(I.A + 1) + ", " + R(I.A + 2) + " do\n        coroutine.yield(true, " + Join(Vars) +
                        ")\n    end\n    return false\nend)";
            Branch.Body.push_back({Kind::Raw, {}, Text});
            SetPc(I.Target());
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
            Branch.Body.push_back({Kind::Raw, {}, Join(Vars) + " = " + Token(It->second) + "()"});
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
            unsigned J = Index(I.Next);
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
        Loop.Body.push_back(std::move(Branch));
    }
    Loop.Body.push_back({Kind::Raw, {}, "error(\"Invalid decompiled control-flow state\")"});
    Out.push_back(std::move(Loop));
    return Out;
}


} // namespace Taze::Detail
