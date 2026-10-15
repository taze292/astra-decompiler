#include "Reconstruction.hpp"

namespace Taze::Detail
{
std::string Function::Render(const std::vector<Statement> &Nodes, unsigned Indent, std::set<unsigned> &Declared,
                             const std::set<unsigned> &Hoist)
{
    std::string Out;
    std::string Pad(Indent * E.Settings.IndentWidth, ' ');
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
    auto Separate = [&]()
    {
        if (E.Settings.GroupSpacing && !Out.empty() && !Out.ends_with("\n\n"))
            Out += '\n';
    };
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
        bool Block = S.Type == Kind::If || S.Type == Kind::While || S.Type == Kind::Repeat || S.Type == Kind::For ||
                     S.Type == Kind::Function || S.Text.find("function(") != std::string::npos;
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
                Line(std::string(E.Settings.IndentWidth, ' ') + Text);
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
        {
            if (S.Type == Kind::If)
            {
                auto Test = FormatCondition(S.Text);
                if (Test.size() + Pad.size() > 110)
                    for (const auto *Operator : {" or ", " and "})
                    {
                        auto Parts = SplitLogical(Test, Operator);
                        if (Parts.size() < 2)
                            continue;
                        Test = Join(Parts, "\n" + std::string(E.Settings.IndentWidth, ' ') + std::string(Operator).substr(1));
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
        Nodes = Region(0, unsigned(P.Code.size()));
    }
    catch (const Unstructured &Reason)
    {
        if (!E.Settings.AllowFallback)
            throw Error("Function " + std::to_string(Id) + ": structuring failed (" + Reason.what() + ")");
        E.Output.Diagnostics.push_back("Function " + std::to_string(Id) + ": state machine (" + Reason.what() + ")");
        E.Output.StateMachineFunctions++;
        Nodes = StateMachine();
    }
    for (unsigned Pass = 0; Pass < E.Settings.OptimizationPasses; ++Pass)
        if (!Optimize(Nodes))
            break;
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
    std::string Pad(Indent * E.Settings.IndentWidth, ' '), Out;
    if (E.Settings.IncludeUpvalueComments && !Upvalues.empty() && E.Settings.CompactUpvalues)
    {
        std::vector<std::string> Names;
        for (const auto &U : Upvalues)
            Names.push_back((Tokens(U.Value).empty() ? U.Name : Token(Tokens(U.Value).front())) + (U.Reference ? " (ref)" : " (copy)"));
        Out += Pad + "-- Upvalues: " + Join(Names) + "\n";
        if (E.Settings.GroupSpacing)
            Out += '\n';
    }
    else if (E.Settings.IncludeUpvalueComments && !Upvalues.empty())
    {
        Out += Pad + "--[[\n" + Pad + std::string(E.Settings.IndentWidth, ' ') + "Upvalues:\n";
        for (unsigned J = 0; J < Upvalues.size(); ++J)
            Out += Pad + std::string(E.Settings.IndentWidth, ' ') + std::to_string(J + 1) + ": " +
                   (Tokens(Upvalues[J].Value).empty() ? Upvalues[J].Name : Token(Tokens(Upvalues[J].Value).front())) + " (type \"" +
                   (Upvalues[J].Reference ? "Reference" : "Copy") + "\")\n";
        Out += Pad + "]]\n\n";
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
        Out += Pad + "local " + Token(TopSymbol) + " = 0\n";
        Declared.insert(PackSymbol);
        Declared.insert(TopSymbol);
    }
    bool FollowsLocals = !Nodes.empty() && Nodes.front().Type == Kind::Assign && !Nodes.front().Text.starts_with("function(");
    if (!Out.empty() && !Out.ends_with("\n\n") && !FollowsLocals)
        Out += '\n';
    Out += Render(Nodes, Indent, Declared, Hoist);
    return Out;
}

} // namespace Taze::Detail
