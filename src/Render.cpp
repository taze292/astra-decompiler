#include "Reconstruction.hpp"

namespace Astra::Detail
{
namespace
{
std::string Indentation(const Options &Settings, unsigned Level = 1)
{
    return std::string(Settings.UseTabs ? Level : Level * Settings.IndentWidth, Settings.UseTabs ? '\t' : ' ');
}

void SeparateGroups(std::string &Text, const Options &Settings)
{
    if (!Settings.GroupSpacing || Text.empty())
        return;
    unsigned Newlines = 0;
    for (auto I = Text.size(); I && Text[I - 1] == '\n'; --I)
        ++Newlines;
    if (Newlines <= Settings.BlankLinesBetweenGroups)
        Text.append(Settings.BlankLinesBetweenGroups + 1 - Newlines, '\n');
}
} // namespace

std::string Function::Render(const std::vector<Statement> &Nodes, unsigned Indent, std::set<unsigned> &Declared,
                             const std::set<unsigned> &Hoist)
{
    std::string Out;
    std::string Pad = Indentation(E.Settings, Indent);
    auto Line = [&](const std::string &Text)
    {
        std::size_t Start = 0;
        do
        {
            auto End = Text.find('\n', Start);
            auto Part = Text.substr(Start, End == std::string::npos ? End : End - Start);
            Out += (Part.find_first_not_of(" \t\r") == std::string::npos ? "" : Pad + Part) + '\n';
            if (End == std::string::npos)
                break;
            Start = End + 1;
        } while (Start < Text.size());
    };
    auto Separate = [&]() { SeparateGroups(Out, E.Settings); };
    enum class Group
    {
        None,
        Locals,
        Services,
        Modules,
        Table,
        Writes,
        Calls,
        Exit,
        Block
    };
    Group Previous = Group::None;
    std::string PreviousReceiver;
    if (auto Found = ScopedDeclarations.find(&Nodes); Found != ScopedDeclarations.end())
    {
        std::vector<std::string> Names;
        for (auto Id : Found->second)
            if (Declared.insert(Id).second)
                Names.push_back(Token(Id));
        if (!Names.empty())
        {
            Line("local " + Join(Names));
            Separate();
        }
    }
    for (std::size_t N = 0; N < Nodes.size(); ++N)
    {
        const auto &S = Nodes[N];
        bool Block = S.Type == Kind::Scope || S.Type == Kind::If || S.Type == Kind::While || S.Type == Kind::Repeat ||
                     S.Type == Kind::For || S.Type == Kind::Function || S.Text.find("function(") != std::string::npos;
        Group Current = Group::Calls;
        std::string Receiver;
        if (Block)
            Current = Group::Block;
        else if (S.Type == Kind::Assign)
        {
            bool New = std::any_of(S.Left.begin(), S.Left.end(), [&](auto U) { return !Declared.count(U); });
            auto Text = Unwrap(S.Text);
            Current = New ? Group::Locals : Group::Writes;
            if (New && Text.find(":GetService(") != std::string::npos)
                Current = Group::Services;
            else if (New && Text.starts_with("require("))
                Current = Group::Modules;
            else if (Text.starts_with('{') && Text.find('\n') != std::string::npos)
                Current = Group::Table;
        }
        else if (S.Type == Kind::Return || S.Type == Kind::Break || S.Type == Kind::Continue)
            Current = Group::Exit;
        else if (S.Type == Kind::Raw)
        {
            auto Assignment = S.Text.find(" = ");
            Current = Assignment != std::string::npos && PrefixExpression(S.Text.substr(0, Assignment)) ? Group::Writes : Group::Calls;
            auto References = Tokens(S.Text);
            if (!References.empty() && S.Text.starts_with(Token(References.front())))
                Receiver = Token(References.front());
        }
        bool Boundary =
            Block || Previous == Group::Block || Current == Group::Exit || Previous == Group::Exit || Current == Group::Table ||
            Previous == Group::Table ||
            ((Current != Previous) && (Current == Group::Locals || Previous == Group::Locals || Current == Group::Services ||
                                       Previous == Group::Services || Current == Group::Modules || Previous == Group::Modules)) ||
            (!Receiver.empty() && !PreviousReceiver.empty() && Receiver != PreviousReceiver);
        if (Previous != Group::None && Boundary)
            Separate();
        switch (S.Type)
        {
        case Kind::Assign:
        {
            std::vector<std::string> Names, New;
            for (auto Id : S.Left)
            {
                Names.push_back(Token(Id));
                if (!Declared.count(Id))
                    New.push_back(Token(Id));
            }
            bool AllNew = New.size() == Names.size();
            if (!AllNew && !New.empty())
                Line("local " + Join(New));
            if (S.Left.size() == 1 && S.Text.starts_with("function("))
            {
                std::string FunctionText = (AllNew ? "local function " : "function ") + Names[0] + S.Text.substr(8);
                Line(FunctionText);
            }
            else
                Line((AllNew ? "local " : "") + Join(Names) + " = " + CleanExpression(S.Left.size() == 1 ? Unwrap(S.Text) : S.Text));
            for (auto Id : S.Left)
                Declared.insert(Id);
            break;
        }
        case Kind::Raw:
        {
            auto Text = CleanExpression(S.Text);
            auto Equal = Text.find(" = ");
            if (Equal != std::string::npos && PrefixExpression(Text.substr(0, Equal)))
                Text = Text.substr(0, Equal + 3) + CleanExpression(Unwrap(Text.substr(Equal + 3)));
            // A lexical block separates a parenthesized call without a statement semicolon.
            if (Text.starts_with('('))
            {
                Line("do");
                Line(Indentation(E.Settings) + Text);
                Line("end");
            }
            else
                Line(Text);
            break;
        }
        case Kind::Function:
            Line(S.Text);
            break;
        case Kind::Return:
            Line(S.Text.empty() ? "return" : "return " + CleanExpression(S.Text));
            break;
        case Kind::Break:
            Line("break");
            break;
        case Kind::Continue:
            Line("continue");
            break;
        case Kind::If:
        case Kind::While:
        case Kind::Repeat:
        case Kind::For:
        case Kind::Scope:
        {
            if (S.Type == Kind::Scope)
                Line("do");
            if (S.Type == Kind::If)
            {
                auto Test = FormatCondition(S.Text);
                if (Test.size() + Pad.size() > 110)
                    for (const auto *Operator : {" or ", " and "})
                    {
                        auto Parts = SplitLogical(Test, Operator);
                        if (Parts.size() < 2)
                            continue;
                        Test = Join(Parts, "\n" + Indentation(E.Settings) + std::string(Operator).substr(1));
                        break;
                    }
                Line("if " + Test + " then");
            }
            if (S.Type == Kind::While)
                Line("while " + FormatCondition(S.Text) + " do");
            if (S.Type == Kind::Repeat)
                Line("repeat");
            if (S.Type == Kind::For)
                Line("for " + S.Text + " do");
            auto InnerDeclared = Declared;
            for (auto Id : S.Left)
                InnerDeclared.insert(Id);
            Out += Render(S.Body, Indent + 1, InnerDeclared, Hoist);
            if (!S.Else.empty())
            {
                InnerDeclared = Declared;
                if (S.Type == Kind::If && S.Else.size() == 1 && S.Else.front().Type == Kind::If && ScopedDeclarations[&S.Else].empty())
                {
                    auto ElseText = Render(S.Else, Indent, InnerDeclared, Hoist);
                    ElseText.replace(Pad.size(), 2, "elseif");
                    Out += ElseText;
                    break; // The flattened elseif already supplies the final end.
                }
                Line("else");
                Out += Render(S.Else, Indent + 1, InnerDeclared, Hoist);
            }
            Line(S.Type == Kind::Repeat ? "until " + FormatCondition(S.Text) : "end");
            break;
        }
        default:
            throw Error("Internal: unsupported source statement");
        }
        if (S.Type == Kind::Return || S.Type == Kind::Break || S.Type == Kind::Continue)
            break;
        Previous = Current;
        PreviousReceiver = Receiver;
        if ((Block || Current == Group::Table) && N + 1 < Nodes.size())
            Separate();
    }
    E.Budget(Out.size());
    return Out;
}

std::string Function::Generate(bool Main)
{
    std::vector<Statement> Nodes;
    try
    {
        if (E.Settings.ForceStateMachine)
            throw Unstructured("requested by caller");
        FindLoopRegions();
        Nodes = Region(0, unsigned(P.Code.size()));
    }
    catch (const Unstructured &Reason)
    {
        if (!E.Settings.AllowFallback)
            throw Error("Function " + std::to_string(Id) + ": structuring failed (" + Reason.what() + ")");
        E.Output.Diagnostics.push_back("Function " + std::to_string(Id) + ": state machine (" + Reason.what() + ")");
        E.Output.StateMachineFunctions++;
        Nodes = StateMachine(Reason.what());
    }
    if (!NeedsDynamicTop)
    {
        const auto Assignment = Token(TopSymbol) + " = ";
        std::function<void(std::vector<Statement> &)> RemoveTop = [&](auto &List)
        {
            std::erase_if(List, [&](const auto &S) { return S.Type == Kind::Raw && S.Text.starts_with(Assignment); });
            for (auto &S : List)
            {
                RemoveTop(S.Body);
                RemoveTop(S.Else);
            }
        };
        RemoveTop(Nodes);
    }
    for (unsigned Pass = 0; Pass < E.Settings.OptimizationPasses; ++Pass)
        if (!Optimize(Nodes))
            break;
    // Shared-tail reconstruction can leave duplicate terminators. Luau requires
    // return/break/continue to finish their lexical statement list, so discard
    // unreachable suffixes even when optimizer passes are disabled.
    unsigned DeclarationCount =
        unsigned(Parameters.size() + Cells.size()) + unsigned(NeedsPack) + unsigned(NeedsPack && NeedsDynamicTop) + unsigned(NeedsVarargs);
    std::function<void(std::vector<Statement> &)> PruneUnreachable = [&](auto &List)
    {
        for (std::size_t N = 0; N < List.size(); ++N)
        {
            auto &S = List[N];
            DeclarationCount += unsigned(S.Left.size());
            PruneUnreachable(S.Body);
            PruneUnreachable(S.Else);
            if (S.Type == Kind::Return || S.Type == Kind::Break || S.Type == Kind::Continue)
            {
                List.resize(N + 1);
                break;
            }
        }
    };
    PruneUnreachable(Nodes);
    const bool LimitLocalPressure = Dispatch || DeclarationCount >= 96;
    // Reaching definitions can merge dead stores from several branches. Their
    // write count exceeds one, so expression optimization intentionally keeps
    // them; nevertheless a pure RHS with no observed destination is removable.
    // Resolve transitively with a worklist rather than leaving hundreds of
    // unused phi locals active in a large parser or generated data function.
    if (LimitLocalPressure)
    {
        std::unordered_map<unsigned, unsigned> SourceReads;
        std::unordered_map<unsigned, std::vector<Statement *>> PureDefinitions;
        std::queue<Statement *> DeadCandidates;
        std::function<void(std::vector<Statement> &)> CountSourceReads = [&](auto &List)
        {
            for (auto &S : List)
            {
                for (auto U : Tokens(S.Text))
                    ++SourceReads[U];
                if (S.Type == Kind::Assign && S.Pure && !S.Left.empty())
                {
                    DeadCandidates.push(&S);
                    for (auto U : S.Left)
                        PureDefinitions[U].push_back(&S);
                }
                CountSourceReads(S.Body);
                CountSourceReads(S.Else);
            }
        };
        CountSourceReads(Nodes);
        std::set<const Statement *> DeadStores;
        while (!DeadCandidates.empty())
        {
            auto *S = DeadCandidates.front();
            DeadCandidates.pop();
            if (DeadStores.count(S) || std::any_of(S->Left.begin(), S->Left.end(), [&](auto U) { return SourceReads[U] != 0; }))
                continue;
            DeadStores.insert(S);
            E.Analysis.Spend();
            for (auto U : Tokens(S->Text))
                if (--SourceReads[U] == 0)
                    for (auto *Definition : PureDefinitions[U])
                        DeadCandidates.push(Definition);
        }
        if (!DeadStores.empty())
        {
            std::function<void(std::vector<Statement> &)> RemoveDeadStores = [&](auto &List)
            {
                for (auto &S : List)
                {
                    RemoveDeadStores(S.Body);
                    RemoveDeadStores(S.Else);
                }
                std::erase_if(List, [&](const auto &S) { return DeadStores.count(&S) != 0; });
            };
            RemoveDeadStores(Nodes);
        }
    }
    // Remove the compiler's implicit empty return at the end of a chunk/function.
    if (!Nodes.empty() && Nodes.back().Type == Kind::Return && Nodes.back().Text.empty())
        Nodes.pop_back();

    using Scope = const std::vector<Statement> *;
    std::map<unsigned, std::vector<Scope>> Occurrences;
    std::set<unsigned> Referenced;
    std::vector<Scope> Path;
    auto Occur = [&](unsigned U)
    {
        Referenced.insert(U);
        auto [It, Inserted] = Occurrences.emplace(U, Path);
        if (!Inserted)
        {
            auto &Common = It->second;
            std::size_t N = 0;
            while (N < Common.size() && N < Path.size() && Common[N] == Path[N])
                ++N;
            Common.resize(N);
        }
    };
    std::function<void(const std::vector<Statement> &)> Inspect = [&](const auto &List)
    {
        Path.push_back(&List);
        for (const auto &S : List)
        {
            for (auto U : Tokens(S.Text))
                Occur(U);
            for (auto U : S.Left)
                Occur(U);
            Inspect(S.Body);
            Inspect(S.Else);
        }
        Path.pop_back();
    };
    Inspect(Nodes);

    // Bytecode reuses registers after a value's last use, but unrelated SSA
    // values were previously rendered as locals lasting until function exit.
    // Large data modules can therefore exhaust Luau's 200-local limit despite
    // having only a handful of live bytecode registers. Restore lexical scopes
    // before declaration placement, so captured values remain ordinary locals
    // and no register-file table or renaming/reuse changes their identity.
    if (LimitLocalPressure)
    {
        std::map<Scope, std::set<unsigned>> ScopeLocals;
        unsigned Permanent =
            unsigned(Parameters.size()) + unsigned(NeedsPack) + unsigned(NeedsPack && NeedsDynamicTop) + unsigned(NeedsVarargs);
        for (auto U : Referenced)
        {
            const auto &Symbol = E.Symbols[U];
            if (Symbol.Owner != Id || Symbol.Parameter || U == PackSymbol || U == TopSymbol || U == VarargsSymbol)
                continue;
            if (Symbol.Cell)
                ++Permanent; // Reference-cell allocation is intentionally function-wide.
            else
                ScopeLocals[Occurrences[U].back()].insert(U);
        }
        bool ScopedLifetimes = false;
        std::function<void(std::vector<Statement> &, unsigned)> ScopeLifetimes = [&](auto &List, unsigned Inherited)
        {
            if (List.empty())
                return;
            const auto &Locals = ScopeLocals[&List];
            unsigned Pressure = Inherited + unsigned(Locals.size());
            // Analyze children before moving their containing statements, since the
            // occurrence map refers to the original statement-vector addresses.
            for (auto &S : List)
            {
                ScopeLifetimes(S.Body, Pressure + (S.Type == Kind::For ? unsigned(S.Left.size()) : 0));
                ScopeLifetimes(S.Else, Pressure);
            }
            if (Pressure <= 128 || Locals.empty() || List.size() < 2)
                return;
            struct Lifetime
            {
                std::size_t First, Last;
            };
            std::map<unsigned, Lifetime> Lifetimes;
            std::function<void(const Statement &, std::size_t)> InspectLifetime = [&](const auto &S, std::size_t Position)
            {
                auto Use = [&](unsigned U)
                {
                    if (!Locals.count(U))
                        return;
                    auto [It, New] = Lifetimes.emplace(U, Lifetime{Position, Position});
                    if (!New)
                        It->second.Last = Position;
                };
                for (auto U : Tokens(S.Text))
                    Use(U);
                for (auto U : S.Left)
                    Use(U);
                for (const auto &Child : S.Body)
                    InspectLifetime(Child, Position);
                for (const auto &Child : S.Else)
                    InspectLifetime(Child, Position);
            };
            for (std::size_t N = 0; N < List.size(); ++N)
                InspectLifetime(List[N], N);
            std::vector<std::size_t> End(List.size());
            std::iota(End.begin(), End.end(), 0);
            std::vector<bool> Starts(List.size());
            for (const auto &[U, Life] : Lifetimes)
            {
                End[Life.First] = std::max(End[Life.First], Life.Last);
                Starts[Life.First] = true;
            }
            // Close an interval over every local introduced inside it. No local
            // declaration can escape its new scope. Right-to-left memoization skips
            // already closed intervals rather than repeatedly scanning long chains.
            for (std::size_t N = List.size(); N-- > 0;)
                for (std::size_t J = N + 1; J <= End[N];)
                {
                    E.Analysis.Spend();
                    End[N] = std::max(End[N], End[J]);
                    J = End[J] + 1;
                }
            std::function<std::vector<Statement>(std::size_t, std::size_t, unsigned)> Partition =
                [&](std::size_t Begin, std::size_t Limit, unsigned Nesting)
            {
                std::vector<Statement> Result;
                Result.reserve(std::min<std::size_t>(Limit - Begin, 128));
                for (std::size_t N = Begin; N < Limit;)
                {
                    E.Analysis.Spend();
                    if (Starts[N] && End[N] + 1 < Limit && Nesting < 32)
                    {
                        Statement ScopeNode{Kind::Scope};
                        ScopeNode.Body = Partition(N, End[N] + 1, Nesting + 1);
                        Result.push_back(std::move(ScopeNode));
                        ScopedLifetimes = true;
                        N = End[N] + 1;
                    }
                    else
                        Result.push_back(std::move(List[N++]));
                }
                return Result;
            };
            List = Partition(0, List.size(), 0);
            // Moving statements also changes descendant vector addresses even
            // when partitioning emits no additional lexical scope.
            ScopedLifetimes = true;
        };
        ScopeLifetimes(Nodes, Permanent);
        if (ScopedLifetimes)
        {
            // The new vectors are real lexical scopes. Recompute declaration LCAs
            // after every move, before retaining any vector address for rendering.
            Occurrences.clear();
            Referenced.clear();
            Inspect(Nodes);
        }
        // Crossing SSA lifetimes need not share one lexical scope. A long chain
        // such as a = initial; b = step(a); c = step(b) otherwise closes into a
        // single interval containing every temporary. Partition bounded statement
        // chunks and let declaration LCAs export only the values crossing their
        // boundaries. Repeating this builds a shallow scope hierarchy for very
        // large chains without changing evaluation order or captured identities.
        for (unsigned Round = 0; Round < 8; ++Round)
        {
            ScopeLocals.clear();
            for (auto U : Referenced)
            {
                const auto &Symbol = E.Symbols[U];
                if (Symbol.Owner == Id && !Symbol.Parameter && !Symbol.Cell && U != PackSymbol && U != TopSymbol && U != VarargsSymbol)
                    ScopeLocals[Occurrences[U].back()].insert(U);
            }
            bool Partitioned = false;
            std::function<void(std::vector<Statement> &, unsigned)> PartitionPressure = [&](auto &List, unsigned Inherited)
            {
                if (List.empty())
                    return;
                const auto &Locals = ScopeLocals[&List];
                unsigned Pressure = Inherited + unsigned(Locals.size());
                for (auto &S : List)
                {
                    PartitionPressure(S.Body, Pressure + (S.Type == Kind::For ? unsigned(S.Left.size()) : 0));
                    PartitionPressure(S.Else, Pressure);
                }
                if (Pressure <= 128 || Locals.size() < 32 || List.size() < 2)
                    return;
                const std::size_t ChunkSize = std::min<std::size_t>(32, (List.size() + 1) / 2);
                std::map<unsigned, std::size_t> FirstChunk;
                std::set<unsigned> Exported;
                std::function<void(const Statement &, std::size_t)> InspectChunk = [&](const auto &S, std::size_t Chunk)
                {
                    auto Use = [&](unsigned U)
                    {
                        if (!Locals.count(U))
                            return;
                        auto [It, New] = FirstChunk.emplace(U, Chunk);
                        if (!New && It->second != Chunk)
                            Exported.insert(U);
                    };
                    for (auto U : Tokens(S.Text))
                        Use(U);
                    for (auto U : S.Left)
                        Use(U);
                    for (const auto &Child : S.Body)
                        InspectChunk(Child, Chunk);
                    for (const auto &Child : S.Else)
                        InspectChunk(Child, Chunk);
                };
                for (std::size_t N = 0; N < List.size(); ++N)
                {
                    E.Analysis.Spend();
                    InspectChunk(List[N], N / ChunkSize);
                }
                if (Exported.size() == Locals.size())
                    return;
                std::vector<Statement> Result;
                Result.reserve((List.size() + ChunkSize - 1) / ChunkSize);
                for (std::size_t N = 0; N < List.size();)
                {
                    Statement Chunk{Kind::Scope};
                    const auto End = std::min(N + ChunkSize, List.size());
                    Chunk.Body.reserve(End - N);
                    while (N < End)
                        Chunk.Body.push_back(std::move(List[N++]));
                    Result.push_back(std::move(Chunk));
                }
                List = std::move(Result);
                Partitioned = true;
            };
            PartitionPressure(Nodes, Permanent);
            if (!Partitioned)
                break;
            Occurrences.clear();
            Referenced.clear();
            Inspect(Nodes);
        }
    }
    std::function<bool(const Statement &, unsigned)> Contains = [&](const auto &S, unsigned U)
    {
        if (S.Text.find(Token(U)) != std::string::npos || std::find(S.Left.begin(), S.Left.end(), U) != S.Left.end())
            return true;
        for (const auto &Child : S.Body)
            if (Contains(Child, U))
                return true;
        for (const auto &Child : S.Else)
            if (Contains(Child, U))
                return true;
        return false;
    };
    std::set<unsigned> Hoist, Declared;
    for (auto U : Parameters)
        Declared.insert(U);
    for (auto U : Referenced)
    {
        const auto &S = E.Symbols[U];
        if (S.Owner != Id || S.Parameter)
            continue;
        if (S.Cell)
        {
            Hoist.insert(U);
            continue;
        }
        const auto *Scope = Occurrences[U].back();
        for (const auto &First : *Scope)
        {
            if (!Contains(First, U))
                continue;
            bool Defines = std::find(First.Left.begin(), First.Left.end(), U) != First.Left.end();
            if (!(Defines && (First.Type == Kind::For || (First.Type == Kind::Assign && (First.Text.find(Token(U)) == std::string::npos ||
                                                                                         First.Text.starts_with("function("))))))
                ScopedDeclarations[Scope].insert(U);
            break;
        }
    }
    Hoist.insert(ScopedDeclarations[&Nodes].begin(), ScopedDeclarations[&Nodes].end());
    if (Dispatch)
        for (auto U : Registers)
            if (Referenced.count(U) && !E.Symbols[U].Parameter)
                Hoist.insert(U);
    unsigned Indent = Main ? 0 : 1;
    std::string Pad = Indentation(E.Settings, Indent), Out;
    if (Main && RecoveredInline)
        Out += Pad + "-- Uninlined by Astra\n";
    if (!RecoveredInline && E.Settings.IncludeUpvalueComments && !Upvalues.empty() && E.Settings.CompactUpvalues)
    {
        std::vector<std::string> Names;
        for (unsigned J = 0; J < Upvalues.size(); ++J)
        {
            const auto &U = Upvalues[J];
            Names.push_back((E.Settings.IncludeUpvalueIndexes ? "[" + std::to_string(J + 1) + "] " : "") +
                            (Tokens(U.Value).empty() ? U.Name : Token(Tokens(U.Value).front())) + (U.Reference ? " (ref)" : " (copy)"));
        }
        Out += Pad + "-- Upvalues: " + Join(Names) + "\n";
        SeparateGroups(Out, E.Settings);
    }
    else if (!RecoveredInline && E.Settings.IncludeUpvalueComments && !Upvalues.empty())
    {
        Out += Pad + "-- Upvalues\n";
        for (unsigned J = 0; J < Upvalues.size(); ++J)
            Out += Pad + "-- " + (E.Settings.IncludeUpvalueIndexes ? "[" + std::to_string(J + 1) + "] " : "") +
                   (Tokens(Upvalues[J].Value).empty() ? Upvalues[J].Name : Token(Tokens(Upvalues[J].Value).front())) +
                   (Upvalues[J].Reference ? " (ref)\n" : " (copy)\n");
        SeparateGroups(Out, E.Settings);
    }
    std::vector<std::string> Hoisted;
    for (auto U : Hoist)
    {
        if (U == PackSymbol || U == TopSymbol || U == VarargsSymbol)
            continue;
        if (E.Symbols[U].Cell)
            Out += Pad + "local " + Token(U) + " = {}\n";
        else
            Hoisted.push_back(Token(U));
        Declared.insert(U);
    }
    for (std::size_t N = 0; N < Hoisted.size(); N += 8)
        Out += Pad + "local " +
               Join(std::vector<std::string>(Hoisted.begin() + std::ptrdiff_t(N),
                                             Hoisted.begin() + std::ptrdiff_t(std::min(N + 8, Hoisted.size())))) +
               "\n";
    for (unsigned R : Cells)
        if (R < P.Parameters)
            Out += Pad + Token(Registers[R]) + " = { " + Token(Registers[R]) + " }\n";
    if (NeedsVarargs)
    {
        if (!P.Vararg)
            throw Error("GETVARARGS in a non-variadic prototype");
        Out += Pad + "local " + Token(VarargsSymbol) + " = table.pack(...)\n";
        Declared.insert(VarargsSymbol);
    }
    if (NeedsPack)
    {
        Out += Pad + "local " + Token(PackSymbol) + " = { n = 0 }\n";
        if (NeedsDynamicTop)
            Out += Pad + "local " + Token(TopSymbol) + " = 0\n";
        Declared.insert(PackSymbol);
        Declared.insert(TopSymbol);
    }
    bool FollowsLocals = !Nodes.empty() && Nodes.front().Type == Kind::Assign && !Nodes.front().Text.starts_with("function(");
    if (!FollowsLocals)
        SeparateGroups(Out, E.Settings);
    Out += Render(Nodes, Indent, Declared, Hoist);
    return Out;
}

} // namespace Astra::Detail
