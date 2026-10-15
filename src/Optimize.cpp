#include "Reconstruction.hpp"

namespace Taze::Detail
{
bool Function::Optimize(std::vector<Statement> &Nodes)
{
    bool Changed = false;
    std::function<bool(const Statement &, const Statement &)> Equal = [&](const auto &A, const auto &B)
    {
        return A.Type == B.Type && A.Left == B.Left && A.Text == B.Text && A.Body.size() == B.Body.size() &&
               A.Else.size() == B.Else.size() && std::equal(A.Body.begin(), A.Body.end(), B.Body.begin(), Equal) &&
               std::equal(A.Else.begin(), A.Else.end(), B.Else.begin(), Equal);
    };
    auto Same = [&](const auto &A, const auto &B) { return A.size() == B.size() && std::equal(A.begin(), A.end(), B.begin(), Equal); };
    std::function<void(std::vector<Statement> &)> Structure = [&](auto &List)
    {
        for (auto &S : List)
        {
            Structure(S.Body);
            Structure(S.Else);
            if (S.Type == Kind::While && S.Text == "true" && !S.Body.empty())
            {
                const auto &Guard = S.Body.front();
                if (Guard.Type == Kind::If && Guard.Else.empty() && Guard.Body.size() == 1 && Guard.Body[0].Type == Kind::Break)
                {
                    S.Text = CleanCondition(Negate(Guard.Text));
                    Changed = true;
                    S.Body.erase(S.Body.begin());
                }
            }
            if (S.Type != Kind::If)
                continue;
            auto Clean = CleanCondition(S.Text);
            Changed |= Clean != S.Text;
            S.Text = std::move(Clean);
            if (S.Body.empty() && !S.Else.empty())
            {
                S.Text = CleanCondition(Negate(S.Text));
                Changed = true;
                S.Body.swap(S.Else);
            }
            if (S.Body.size() == 1 && S.Body[0].Type == Kind::If && Same(S.Else, S.Body[0].Else))
            {
                Changed = true;
                auto Inner = std::move(S.Body[0]);
                S.Text = "(" + S.Text + ") and (" + Inner.Text + ")";
                S.Body = std::move(Inner.Body);
            }
            if (S.Body.size() == 1 && S.Body[0].Type == Kind::If && !S.Else.empty())
            {
                S.Text = CleanCondition(Negate(S.Text));
                Changed = true;
                S.Body.swap(S.Else);
            }
            if (S.Else.size() == 1 && S.Else[0].Type == Kind::If && Same(S.Body, S.Else[0].Body))
            {
                Changed = true;
                auto Inner = std::move(S.Else[0]);
                S.Text = "(" + S.Text + ") or (" + Inner.Text + ")";
                S.Else = std::move(Inner.Else);
            }
        }
        for (std::size_t N = 0; N < List.size(); ++N)
        {
            auto &S = List[N];
            if (S.Type != Kind::If)
                continue;
            std::vector<Statement> Tail;
            while (!S.Body.empty() && !S.Else.empty() && Equal(S.Body.back(), S.Else.back()))
            {
                Tail.push_back(std::move(S.Body.back()));
                S.Body.pop_back();
                S.Else.pop_back();
            }
            if (!Tail.empty())
            {
                std::reverse(Tail.begin(), Tail.end());
                Changed = true;
                List.insert(List.begin() + std::ptrdiff_t(N + 1), Tail.begin(), Tail.end());
            }
        }
        for (std::size_t N = 0; N < List.size(); ++N)
        {
            auto &S = List[N];
            if (S.Type != Kind::If || S.Body.size() != 1 || S.Body[0].Type != Kind::Assign || S.Body[0].Left.size() != 1)
                continue;
            auto U = S.Body[0].Left[0];
            if (S.Else.empty() && N && List[N - 1].Type == Kind::Assign && List[N - 1].Left == S.Body[0].Left &&
                S.Body[0].Text.find(Token(U)) == std::string::npos)
            {
                auto Test = CleanCondition(S.Text);
                if (Test == Token(U) || Test == "not " + Token(U))
                {
                    auto &Previous = List[N - 1];
                    Previous.Text =
                        "(" + Unwrap(Previous.Text) + ")" + (Test == Token(U) ? " and " : " or ") + "(" + Unwrap(S.Body[0].Text) + ")";
                    Previous.Pure = false;
                    Previous.Call = false;
                    Changed = true;
                    List.erase(List.begin() + std::ptrdiff_t(N--));
                    continue;
                }
            }
            if (S.Else.size() == 1 && S.Else[0].Type == Kind::Assign && S.Else[0].Left == S.Body[0].Left &&
                (S.Else[0].Text == "false" || S.Else[0].Text == "nil" || S.Body[0].Text == "false" || S.Body[0].Text == "nil") &&
                S.Body[0].Text.find('\n') == std::string::npos && S.Else[0].Text.find('\n') == std::string::npos)
            {
                Statement Value = S.Body[0];
                Value.Text = "(if " + CleanCondition(S.Text) + " then " + Unwrap(Value.Text) + " else " + Unwrap(S.Else[0].Text) + ")";
                Value.Pure = false;
                Value.Call = false;
                Changed = true;
                S = std::move(Value);
            }
        }
    };
    Structure(Nodes);
    std::unordered_map<unsigned, unsigned> Reads, WritesCount;
    std::function<void(const std::vector<Statement> &)> Count = [&](const auto &List)
    {
        for (const auto &S : List)
        {
            for (auto Id : Tokens(S.Text))
                Reads[Id]++;
            for (auto Id : S.Left)
                WritesCount[Id]++;
            Count(S.Body);
            Count(S.Else);
        }
    };
    Count(Nodes);
    std::function<void(std::vector<Statement> &)> Apply = [&](std::vector<Statement> &Nodes)
    {
        for (auto &S : Nodes)
        {
            Apply(S.Body);
            Apply(S.Else);
        }
        for (std::size_t N = 0; N < Nodes.size();)
        {
            auto &S = Nodes[N];
            // A temporary that merely copies a stable local can be replaced at
            // every straight-line use, even when the copy is read more than once.
            if (S.Type == Kind::Assign && S.Left.size() == 1 && !E.Symbols[S.Left[0]].Keep && WritesCount[S.Left[0]] == 1)
            {
                auto From = Tokens(S.Text);
                if (From.size() == 1 && S.Text == Token(From.front()) && From.front() != S.Left[0] && !E.Symbols[From.front()].Cell &&
                    !E.Symbols[From.front()].ReferenceAlias)
                {
                    bool Initialized = E.Symbols[From.front()].Parameter && WritesCount[From.front()] == 0;
                    if (WritesCount[From.front()] == 1)
                        for (std::size_t J = 0; J < N; ++J)
                            Initialized |= std::find(Nodes[J].Left.begin(), Nodes[J].Left.end(), From.front()) != Nodes[J].Left.end();
                    unsigned Uses = 0;
                    std::size_t End = N + 1;
                    for (; Initialized && End < Nodes.size(); ++End)
                    {
                        const auto &Later = Nodes[End];
                        if (!Later.Body.empty() || !Later.Else.empty() || Later.Type == Kind::While || Later.Type == Kind::Repeat ||
                            Later.Type == Kind::For || Later.Type == Kind::Function || Later.Text.find('\n') != std::string::npos)
                            break;
                        auto References = Tokens(Later.Text);
                        Uses += unsigned(std::count(References.begin(), References.end(), S.Left[0]));
                    }
                    if (Initialized && Uses && Uses == Reads[S.Left[0]])
                    {
                        for (std::size_t J = N + 1; J < End; ++J)
                            Replace(Nodes[J].Text, Token(S.Left[0]), Token(From.front()));
                        Changed = true;
                        Nodes.erase(Nodes.begin() + std::ptrdiff_t(N));
                        continue;
                    }
                }
            }
            if (S.Type == Kind::Assign && S.Left.size() == 3 && N + 1 < Nodes.size() && Nodes[N + 1].Type == Kind::For)
            {
                auto &Loop = Nodes[N + 1];
                std::vector<std::string> Vars;
                bool Once = true;
                for (auto U : S.Left)
                {
                    Vars.push_back(Token(U));
                    Once &= Reads[U] == 1 && WritesCount[U] == 1;
                }
                auto Suffix = " in " + Join(Vars);
                if (Once && Loop.Text.ends_with(Suffix))
                {
                    Loop.Text.replace(Loop.Text.size() - Suffix.size(), Suffix.size(), " in " + S.Text);
                    Changed = true;
                    Nodes.erase(Nodes.begin() + std::ptrdiff_t(N));
                    continue;
                }
            }
            // Recover a method declaration from the closure and its table installation.
            if (S.Type == Kind::Assign && S.Left.size() == 1 && S.Text.starts_with("function(") && Reads[S.Left[0]] == 1 &&
                N + 1 < Nodes.size() && Nodes[N + 1].Type == Kind::Raw)
            {
                auto &Next = Nodes[N + 1];
                auto Suffix = " = " + Token(S.Left[0]);
                if (Next.Text.ends_with(Suffix))
                {
                    auto Target = Next.Text.substr(0, Next.Text.size() - Suffix.size());
                    if (Target.find('.') != std::string::npos && Target.find('[') == std::string::npos && PrefixExpression(Target))
                    {
                        auto Args = Tokens(S.Text.substr(9, S.Text.find(')') - 9));
                        if (E.Settings.MethodSyntax && !Args.empty() && !E.GlobalNames.count("self"))
                        {
                            auto First = Args.front();
                            const auto &Parameter = E.Symbols[First];
                            bool Self = Parameter.Parameter && (Parameter.Original == "self" || Parameter.Original == "Self");
                            if (Self)
                                for (auto U : Tokens(S.Text))
                                    if (U != First && E.Symbols[U].Owner != Parameter.Owner &&
                                        (E.Symbols[U].Original == "self" || E.Symbols[U].Original == "Self"))
                                        Self = false; // Do not shadow a captured outer receiver.
                            if (Self)
                            {
                                E.Symbols[First].ImplicitSelf = true;
                                Target[Target.rfind('.')] = ':';
                                auto Length = Token(First).size();
                                if (S.Text.substr(9 + Length, 2) == ", ")
                                    Length += 2;
                                S.Text.erase(9, Length);
                            }
                        }
                        S.Text = "function " + Target + S.Text.substr(8);
                        S.Type = Kind::Function;
                        S.Left.clear();
                        Changed = true;
                        Nodes.erase(Nodes.begin() + std::ptrdiff_t(N + 1));
                    }
                }
            }
            if (S.Type == Kind::Assign && S.Left.size() == 1 && !E.Symbols[S.Left[0]].Keep && WritesCount[S.Left[0]] == 1)
            {
                auto Id = S.Left[0];
                if (!Reads[Id] && S.Pure)
                {
                    Changed = true;
                    Nodes.erase(Nodes.begin() + std::ptrdiff_t(N));
                    continue;
                }
                if (Reads[Id] == 1 && S.Pure && S.Text.find('\n') == std::string::npos)
                {
                    bool Stable = true;
                    for (auto Dependency : Tokens(S.Text))
                        if (E.Symbols[Dependency].Cell || E.Symbols[Dependency].ReferenceAlias || WritesCount[Dependency] > 1)
                            Stable = false;
                    if (Stable)
                    {
                        bool Removed = false;
                        for (std::size_t J = N + 1; J < Nodes.size(); ++J)
                        {
                            if (!Nodes[J].Body.empty() || !Nodes[J].Else.empty() || Nodes[J].Type == Kind::Repeat ||
                                Nodes[J].Type == Kind::While)
                                break;
                            auto Uses = Tokens(Nodes[J].Text);
                            if (std::count(Uses.begin(), Uses.end(), Id) == 1)
                            {
                                auto Replacement = S.Text;
                                auto Position = Nodes[J].Text.find(Token(Id)) + Token(Id).size();
                                if (Position < Nodes[J].Text.size() &&
                                    std::string(".:[").find(Nodes[J].Text[Position]) != std::string::npos &&
                                    !PrefixExpression(Replacement) && (Replacement.empty() || Replacement.front() != '('))
                                    Replacement = "(" + Replacement + ")";
                                Replace(Nodes[J].Text, Token(Id), Replacement);
                                Changed = true;
                                Nodes.erase(Nodes.begin() + std::ptrdiff_t(N));
                                Removed = true;
                                break;
                            }
                        }
                        if (Removed)
                        {
                            if (N)
                                --N;
                            continue;
                        }
                    }
                }
                if (N + 1 < Nodes.size() && Reads[Id] == 1 && S.Text.find('\n') == std::string::npos &&
                    Nodes[N + 1].Text.find(Token(Id)) != std::string::npos)
                {
                    auto &Next = Nodes[N + 1];
                    auto Uses = Tokens(Next.Text);
                    auto UsePosition = Next.Text.find(Token(Id));
                    auto BeforeUse = Next.Text.substr(0, UsePosition);
                    bool CanReorder = S.Pure;
                    for (auto Dependency : Tokens(S.Text))
                        if (E.Symbols[Dependency].Cell || E.Symbols[Dependency].ReferenceAlias)
                            CanReorder = false;
                    bool Ordered = CanReorder || BeforeUse.find_first_of(")[.:+-*/%^#") == std::string::npos;
                    // Reading a native captured local has no effects. It can move
                    // past evaluation of bare locals/literals before the first
                    // arithmetic operator is applied, but never across a property,
                    // call, or completed parenthesized expression (metamethods).
                    if (!Ordered)
                    {
                        auto ValueTokens = Tokens(S.Text);
                        if (ValueTokens.size() == 1 && S.Text == Token(ValueTokens.front()) && !E.Symbols[ValueTokens.front()].Cell &&
                            BeforeUse.find_first_of(")[.:") == std::string::npos)
                            Ordered = true;
                    }
                    auto PrefixSymbols = Tokens(BeforeUse);
                    // Moving an effectful expression past reads of immutable
                    // locals and numeric literals preserves evaluation order.
                    // Reject completed operations/property access/calls in the
                    // prefix; they may invoke metamethods or mutate captures.
                    if (!Ordered)
                    {
                        auto PrefixText = BeforeUse;
                        bool StablePrefix = true;
                        for (auto U : PrefixSymbols)
                        {
                            const auto &Symbol = E.Symbols[U];
                            StablePrefix &= !Symbol.Cell && !Symbol.ReferenceAlias && WritesCount[U] <= 1;
                            Replace(PrefixText, Token(U), "0");
                        }
                        if (StablePrefix && PrefixText.find_first_not_of(" (=,+-*/%^0123456789") == std::string::npos)
                            Ordered = true;
                    }
                    if (PrefixSymbols.size() == 1 && !E.Symbols[PrefixSymbols[0]].Cell && BeforeUse.starts_with(Token(PrefixSymbols[0])))
                    {
                        auto Tail = BeforeUse.substr(Token(PrefixSymbols[0]).size());
                        // Assigning a constant field on a stable local has no effect before the RHS is evaluated.
                        if (Tail.starts_with('.') && Tail.ends_with(" = ") && Identifier(Tail.substr(1, Tail.size() - 4)))
                            Ordered = true;
                        // Luau evaluates method arguments before NAMECALL; the receiver here is already a stable local.
                        if (Tail.starts_with(':') && Tail.ends_with('(') && Identifier(Tail.substr(1, Tail.size() - 2)))
                            Ordered = true;
                    }
                    // Adjacent substitution cannot cross a side effect. Do not change a call's arity by inlining a multi-result call.
                    if (Ordered && std::count(Uses.begin(), Uses.end(), Id) == 1 && Next.Type != Kind::While && Next.Type != Kind::Repeat &&
                        Next.Type != Kind::Function)
                    {
                        std::string Replacement = S.Text;
                        if (S.Call)
                            Replacement = "(" + Replacement + ")";
                        auto Position = Next.Text.find(Token(Id)) + Token(Id).size();
                        if (Position < Next.Text.size() && std::string(".:[").find(Next.Text[Position]) != std::string::npos &&
                            !PrefixExpression(Replacement) && (Replacement.empty() || Replacement.front() != '('))
                            Replacement = "(" + Replacement + ")";
                        Replace(Next.Text, Token(Id), Replacement);
                        Next.Pure = Next.Pure && S.Pure;
                        Next.Call = Next.Call || S.Call;
                        Changed = true;
                        Nodes.erase(Nodes.begin() + std::ptrdiff_t(N));
                        if (N)
                            --N;
                        continue;
                    }
                }
            }
            ++N;
        }
    };
    Apply(Nodes);
    std::function<void(std::vector<Statement> &)> Tables = [&](auto &List)
    {
        for (auto &S : List)
        {
            Tables(S.Body);
            Tables(S.Else);
        }
        for (std::size_t N = 0; N < List.size(); ++N)
        {
            auto &S = List[N];
            if (S.Type != Kind::Assign || S.Left.size() != 1 || S.Text != "{}")
                continue;
            auto Table = Token(S.Left[0]);
            std::vector<std::string> Fields;
            for (std::size_t J = N + 1; J < List.size();)
            {
                // Independent empty table allocations may be interleaved with
                // the first writes to this table.
                if (List[J].Type == Kind::Assign && List[J].Left.size() == 1 && List[J].Text == "{}")
                {
                    ++J;
                    continue;
                }
                if (List[J].Type != Kind::Raw)
                    break;
                auto Text = List[J].Text;
                if (!Text.starts_with(Table))
                    break;
                auto Equal = Text.find(" = ");
                if (Equal == std::string::npos || Text.find('\n') != std::string::npos)
                    break;
                auto Right = Text.substr(Equal + 3);
                if (Right.find(Table) != std::string::npos)
                    break;
                auto Key = Text.substr(Table.size(), Equal - Table.size());
                if (J > N + 1 && (!Tokens(Key).empty() || !Tokens(Right).empty() ||
                                  std::any_of(Right.begin(), Right.end(), [](unsigned char C) { return std::isalpha(C); })))
                    break;
                if (Key.starts_with('.'))
                    Key.erase(0, 1);
                else if (!(Key.starts_with('[') && Key.ends_with(']')))
                    break;
                Fields.push_back(Key + " = " + Right);
                Changed = true;
                List.erase(List.begin() + std::ptrdiff_t(J));
            }
            if (Fields.size() > 3)
            {
                S.Text = "{\n";
                for (const auto &F : Fields)
                    S.Text += std::string(E.Settings.IndentWidth, ' ') + F + ",\n";
                S.Text += "}";
            }
            else if (!Fields.empty())
                S.Text = "{ " + Join(Fields) + " }";
        }
    };
    Tables(Nodes);
    return Changed;
}

} // namespace Taze::Detail
