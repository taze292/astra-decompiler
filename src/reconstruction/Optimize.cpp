#include "Reconstruction.hpp"
#include <charconv>

namespace Astra::Detail
{
namespace
{
// Statements rewritten in place around a moving cursor. The free gap follows
// the cursor, so erasing at the cursor or just after it moves O(1) elements
// rather than every later statement of a long (generated data) function.
// References to the cursor statement and to later statements stay valid while
// later statements are erased.
class GapList
{
  public:
    struct Position
    {
        std::size_t Index;
    };
    explicit GapList(std::vector<Statement> &Source) : Items(std::move(Source)), Start(Items.size()), End(Items.size()) {}
    std::size_t size() const
    {
        return Items.size() - (End - Start);
    }
    Statement &operator[](std::size_t I)
    {
        return Items[I < Start ? I : I + (End - Start)];
    }
    Position begin() const
    {
        return {0};
    }
    friend Position operator+(Position P, std::ptrdiff_t Offset)
    {
        return {P.Index + std::size_t(Offset)};
    }
    // Keep statements before Index in place.
    void Focus(std::size_t Index)
    {
        Move(std::min(Index, size()));
    }
    void erase(Position P)
    {
        Move(P.Index);
        Items[End++] = Statement{};
    }
    std::vector<Statement> Release()
    {
        Move(size());
        Items.resize(Start);
        return std::move(Items);
    }

  private:
    std::vector<Statement> Items;
    std::size_t Start, End;
    void Move(std::size_t Index)
    {
        while (Start > Index)
            Items[--End] = std::move(Items[--Start]);
        while (Start < Index)
            Items[Start++] = std::move(Items[End++]);
    }
};
} // namespace

bool Function::Optimize(std::vector<Statement> &Nodes)
{
    bool Changed = false;
    auto Literal = [](const std::string &Text)
    {
        if (Text == "nil" || Text == "true" || Text == "false" || Text == "{}")
            return true;
        if (!Text.empty() && (Text.front() == '\'' || Text.front() == '"'))
        {
            std::size_t End = 1;
            while (End < Text.size())
            {
                if (Text[End] == '\\')
                    End += 2;
                else if (Text[End++] == Text.front())
                    return End == Text.size();
            }
            return false;
        }
        if (Text.empty() || !(std::isdigit(static_cast<unsigned char>(Text.front())) ||
                              (Text.front() == '-' && Text.size() > 1 && std::isdigit(static_cast<unsigned char>(Text[1])))))
            return false;
        double Number = 0;
        auto [End, Error] = std::from_chars(Text.data(), Text.data() + Text.size(), Number);
        return Error == std::errc{} && End == Text.data() + Text.size();
    };
    std::function<bool(const Statement &, const Statement &)> Equal = [&](const auto &A, const auto &B)
    {
        return A.Type == B.Type && A.Left == B.Left && A.Text == B.Text && A.Lifetime == B.Lifetime && A.Body.size() == B.Body.size() &&
               A.Else.size() == B.Else.size() && std::equal(A.Body.begin(), A.Body.end(), B.Body.begin(), Equal) &&
               std::equal(A.Else.begin(), A.Else.end(), B.Else.begin(), Equal);
    };
    auto Same = [&](const auto &A, const auto &B) { return A.size() == B.size() && std::equal(A.begin(), A.end(), B.begin(), Equal); };
    std::function<bool(const Statement &, unsigned)> Mentions = [&](const Statement &S, unsigned U)
    {
        if (S.Text.find(Token(U)) != std::string::npos || std::find(S.Left.begin(), S.Left.end(), U) != S.Left.end())
            return true;
        return std::any_of(S.Body.begin(), S.Body.end(), [&](const Statement &C) { return Mentions(C, U); }) ||
               std::any_of(S.Else.begin(), S.Else.end(), [&](const Statement &C) { return Mentions(C, U); });
    };
    // Tests separated by effectful setup are threaded through a skip flag:
    //
    //     Condition = x0
    //     if not Condition then setup1; Condition = x1 end
    //     if not Condition then body end
    //
    // Without an else arm this is exactly a nest of ifs, with no duplication:
    // `if not x0 then setup1; if not x1 then body end end`.
    auto NestFlags = [&](std::vector<Statement> &List)
    {
        for (std::size_t N = 0; N < List.size(); ++N)
        {
            const auto &Init = List[N];
            if (Init.Type != Kind::Assign || Init.Left.size() != 1)
                continue;
            const unsigned Flag = Init.Left[0];
            const auto &Symbol = E.Symbols[Flag];
            if (Symbol.Original != "Condition" || Symbol.Debug || Symbol.Cell || Symbol.Captured || Init.Text.find(Token(Flag)) != std::string::npos)
                continue;
            const auto Skip = "not " + Token(Flag);
            std::vector<std::string> Values{Init.Text};
            std::size_t J = N + 1;
            for (; J < List.size(); ++J)
            {
                const auto &Guard = List[J];
                if (Guard.Type != Kind::If || !Guard.Else.empty() || CleanCondition(Guard.Text) != Skip || Guard.Body.empty())
                    break;
                const auto &Update = Guard.Body.back();
                if (Update.Type != Kind::Assign || Update.Left != std::vector<unsigned>{Flag} || Update.Text.find(Token(Flag)) != std::string::npos ||
                    std::any_of(Guard.Body.begin(), Guard.Body.end() - 1, [&](const Statement &C) { return Mentions(C, Flag); }))
                    break;
                // The final branch is a guard whose body does not update the flag.
                Values.push_back(Update.Text);
            }
            if (J == N + 1 || J >= List.size())
                continue;
            auto &Final = List[J];
            if (Final.Type != Kind::If || !Final.Else.empty() || CleanCondition(Final.Text) != Skip ||
                std::any_of(Final.Body.begin(), Final.Body.end(), [&](const Statement &C) { return Mentions(C, Flag); }) ||
                std::any_of(List.begin() + std::ptrdiff_t(J + 1), List.end(), [&](const Statement &C) { return Mentions(C, Flag); }))
                continue;
            Statement Current = std::move(Final);
            Current.Text = Negate(Values.back());
            for (std::size_t G = J; G-- > N + 1;)
            {
                Statement Outer{Kind::If, {}, Negate(Values[G - N - 1])};
                Outer.Body = std::move(List[G].Body);
                Outer.Body.pop_back();
                Outer.Body.push_back(std::move(Current));
                Current = std::move(Outer);
            }
            List[N] = std::move(Current);
            List.erase(List.begin() + std::ptrdiff_t(N + 1), List.begin() + std::ptrdiff_t(J + 1));
            Changed = true;
        }
    };
    std::function<void(std::vector<Statement> &)> Structure = [&](auto &List)
    {
        NestFlags(List);
        for (auto &S : List)
        {
            Structure(S.Body);
            Structure(S.Else);
            if ((S.Type == Kind::While || S.Type == Kind::For || S.Type == Kind::Repeat) && !S.Body.empty() &&
                S.Body.back().Type == Kind::Continue)
            {
                S.Body.pop_back();
                Changed = true;
            }
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
                        "((" + Unwrap(Previous.Text) + ")" + (Test == Token(U) ? " and " : " or ") + "(" + Unwrap(S.Body[0].Text) + "))";
                    Previous.Pure = false;
                    Previous.Call = false;
                    Changed = true;
                    List.erase(List.begin() + std::ptrdiff_t(N--));
                    continue;
                }
            }
            // `if c then x = a else x = b end` and `x = if c then a else b`
            // compile identically; prefer the expression while it stays short.
            auto Condition = CleanCondition(S.Text);
            if (S.Else.size() == 1 && S.Else[0].Type == Kind::Assign && S.Else[0].Left == S.Body[0].Left &&
                S.Body[0].Text.find('\n') == std::string::npos && S.Else[0].Text.find('\n') == std::string::npos &&
                Condition.find('\n') == std::string::npos &&
                Condition.size() + S.Body[0].Text.size() + S.Else[0].Text.size() <= 110)
            {
                Statement Value = S.Body[0];
                auto Yes = Unwrap(Value.Text), No = Unwrap(S.Else[0].Text);
                // With a value that is always truthy, `c and a or b` is the same choice.
                bool Truthy = Literal(Yes) && Yes != "nil" && Yes != "false" && Yes != "{}";
                // For a boolean condition `c and true or x` is just `c or x`.
                if (Yes == "true" && BooleanExpression(Condition))
                    Value.Text = "((" + Condition + ") or (" + No + "))";
                else
                    Value.Text = Truthy ? "((" + Condition + ") and (" + Yes + ") or (" + No + "))"
                                        : "(if " + Condition + " then " + Yes + " else " + No + ")";
                Value.Pure = false;
                Value.Call = false;
                Changed = true;
                S = std::move(Value);
            }
        }
    };
    Structure(Nodes);
    std::unordered_map<unsigned, unsigned> Reads, WritesCount;
    std::set<unsigned> LoopVariables;
    std::function<void(const std::vector<Statement> &)> Count = [&](const auto &List)
    {
        for (const auto &S : List)
        {
            for (auto Id : Tokens(S.Text))
                Reads[Id]++;
            for (auto Id : S.Left)
                WritesCount[Id]++;
            if (S.Type == Kind::For)
                LoopVariables.insert(S.Left.begin(), S.Left.end());
            Count(S.Body);
            Count(S.Else);
        }
    };
    Count(Nodes);
    // Length of the longest import path spelled at At, or 0.
    auto ImportAt = [&](const std::string &Text, std::size_t At)
    {
        std::size_t Longest = 0;
        for (const auto &Path : ImportPaths)
        {
            auto End = At + Path.size();
            if (Path.size() > Longest && Text.compare(At, Path.size(), Path) == 0 &&
                (End == Text.size() || !(std::isalnum(static_cast<unsigned char>(Text[End])) || Text[End] == '_')))
                Longest = Path.size();
        }
        return Longest;
    };
    std::function<void(std::vector<Statement> &)> Apply = [&](std::vector<Statement> &Source)
    {
        for (auto &S : Source)
        {
            Apply(S.Body);
            Apply(S.Else);
        }
        GapList Nodes(Source);
        for (std::size_t N = 0; N < Nodes.size();)
        {
            Nodes.Focus(N + 1);
            auto &S = Nodes[N];
            // A binding that merely copies a stable local can be replaced at
            // every later use, even when the copy is read more than once or
            // captured: both names hold the same value for the copy's lifetime.
            if (S.Type == Kind::Assign && S.Left.size() == 1 && WritesCount[S.Left[0]] == 1)
            {
                const auto &Copy = E.Symbols[S.Left[0]];
                bool Replaceable = !Copy.Keep || (Copy.Captured && !Copy.Debug && !Copy.Preserve && !Copy.Parameter);
                auto From = Tokens(S.Text);
                if (Replaceable && From.size() == 1 && S.Text == Token(From.front()) && From.front() != S.Left[0] &&
                    !E.Symbols[From.front()].Cell && !E.Symbols[From.front()].ReferenceAlias && !Copy.Cell && !Copy.ReferenceAlias)
                {
                    bool Initialized = (E.Symbols[From.front()].Parameter && WritesCount[From.front()] == 0) ||
                                       (LoopVariables.count(From.front()) && WritesCount[From.front()] == 1);
                    if (WritesCount[From.front()] == 1)
                        for (std::size_t J = 0; J < N; ++J)
                            Initialized |= std::find(Nodes[J].Left.begin(), Nodes[J].Left.end(), From.front()) != Nodes[J].Left.end();
                    const auto Expected = Reads[S.Left[0]];
                    const auto Name = Token(S.Left[0]);
                    std::function<unsigned(const Statement &)> Count = [&](const Statement &Later)
                    {
                        auto Total = Occurrences(Later.Text, Name, Expected + 1);
                        for (const auto &Child : Later.Body)
                            Total += Count(Child);
                        for (const auto &Child : Later.Else)
                            Total += Count(Child);
                        return Total;
                    };
                    // Every read is found once the count reaches the total.
                    unsigned Uses = 0;
                    std::size_t LastUse = N;
                    for (std::size_t J = N + 1; Initialized && Uses < Expected && J < Nodes.size(); ++J)
                        if (auto Found = Count(Nodes[J]))
                        {
                            Uses += Found;
                            LastUse = J;
                        }
                    if (Initialized && Uses && Uses == Expected)
                    {
                        const auto Old = Token(S.Left[0]), New = Token(From.front());
                        const auto Target = S.Left[0], Source = From.front();
                        std::function<void(Statement &)> Rewrite = [&](Statement &Later)
                        {
                            Replace(Later.Text, Old, New);
                            std::replace(Later.Lifetime.begin(), Later.Lifetime.end(), Target, Source);
                            for (auto &Child : Later.Body)
                                Rewrite(Child);
                            for (auto &Child : Later.Else)
                                Rewrite(Child);
                        };
                        for (std::size_t J = N + 1; J <= LastUse; ++J)
                            Rewrite(Nodes[J]);
                        // The original copy read its source once; its expanded
                        // uses now read it Uses times. Later rewrites in this
                        // pass can revisit the source, so stale counts could
                        // remove its definition while other uses still exist.
                        Reads[Source] += Uses - 1;
                        E.Symbols[Source].Captured = E.Symbols[Source].Captured || Copy.Captured;
                        E.Symbols[Source].Keep = E.Symbols[Source].Keep || Copy.Captured;
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
            // `x = value; return x` returns the value directly. Shared exit
            // paths assign the same variable in several places, but nothing
            // can observe a plain local once its function has returned.
            if (S.Type == Kind::Assign && S.Left.size() == 1 && N + 1 < Nodes.size() && Nodes[N + 1].Type == Kind::Return &&
                Nodes[N + 1].Text == Token(S.Left[0]))
            {
                const auto &Value = E.Symbols[S.Left[0]];
                if (!Value.Cell && !Value.ReferenceAlias && !Value.Captured && !Value.Debug && !Value.Preserve && !Value.Parameter &&
                    NestingDepth(S.Text) < MaxInlineNesting)
                {
                    auto &Exit = Nodes[N + 1];
                    Exit.Text = S.Call && CallExpression(Unwrap(S.Text)) ? "(" + Unwrap(S.Text) + ")" : S.Text;
                    --WritesCount[S.Left[0]];
                    --Reads[S.Left[0]];
                    Changed = true;
                    Nodes.erase(Nodes.begin() + std::ptrdiff_t(N));
                    if (N)
                        --N;
                    continue;
                }
            }
            if (S.Type == Kind::Assign && S.Left.size() == 1 && WritesCount[S.Left[0]] == 1 && Reads[S.Left[0]] == 1)
            {
                const auto Id = S.Left[0];
                const auto &Info = E.Symbols[Id];
                bool Plain = !Info.Cell && !Info.ReferenceAlias && !Info.Parameter;
                // Luau can compute a new local in a temporary and copy it into a
                // register freed by a dead local. Bind the value to the copy
                // directly, keeping the more descriptive of the two names.
                if (Plain && N + 1 < Nodes.size() && Nodes[N + 1].Type == Kind::Assign && Nodes[N + 1].Left.size() == 1 &&
                    Nodes[N + 1].Text == Token(Id) && Nodes[N + 1].Left[0] != Id)
                {
                    auto &Copy = Nodes[N + 1];
                    auto &Target = E.Symbols[Copy.Left[0]];
                    auto Generic = [](const Symbol &Value)
                    {
                        const auto &Name = Value.Original;
                        return !Value.Debug && !Value.Preserve && !Value.Parameter && Name.starts_with("Value") &&
                               std::all_of(Name.begin() + 5, Name.end(), [](unsigned char C) { return std::isdigit(C); });
                    };
                    if (Generic(Target) && !Generic(Info))
                    {
                        E.Rename(Copy.Left[0], Id);
                        Target.Keep = Target.Keep || Info.Keep;
                    }
                    Copy.Text = std::move(S.Text);
                    Copy.Pure = S.Pure;
                    Copy.Call = S.Call;
                    Changed = true;
                    Nodes.erase(Nodes.begin() + std::ptrdiff_t(N));
                    continue;
                }
                // An anonymous closure used once becomes a function expression
                // at that use. Creation has no effects and every captured value
                // is a single-definition binding, so moving it later within the
                // same statement list cannot change what the closure observes.
                if (Plain && !Info.Captured && !Info.Preserve && !Info.Debug && S.Text.starts_with("function("))
                {
                    std::size_t J = N + 1;
                    for (; J < Nodes.size(); ++J)
                    {
                        const auto &Later = Nodes[J];
                        if (Later.Text.find(Token(Id)) != std::string::npos)
                            break;
                        std::function<bool(const std::vector<Statement> &)> Nested = [&](const auto &List)
                        {
                            return std::any_of(List.begin(), List.end(), [&](const Statement &Child)
                                               { return Child.Text.find(Token(Id)) != std::string::npos || Nested(Child.Body) || Nested(Child.Else); });
                        };
                        if (Nested(Later.Body) || Nested(Later.Else))
                        {
                            J = Nodes.size();
                            break;
                        }
                    }
                    // A called or indexed closure stays a named local function.
                    auto Operand = [&](const Statement &Use)
                    {
                        auto Position = Use.Text.find(Token(Id)) + Token(Id).size();
                        return Position >= Use.Text.size() || std::string(".:[(").find(Use.Text[Position]) == std::string::npos;
                    };
                    if (J < Nodes.size() && (Nodes[J].Type == Kind::Assign || Nodes[J].Type == Kind::Raw || Nodes[J].Type == Kind::Return) &&
                        Operand(Nodes[J]))
                    {
                        auto &Use = Nodes[J];
                        Replace(Use.Text, Token(Id), S.Text);
                        Use.Pure = false;
                        Changed = true;
                        Nodes.erase(Nodes.begin() + std::ptrdiff_t(N));
                        continue;
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
                            if (Occurrences(Nodes[J].Text, Token(Id), 2) == 1)
                            {
                                if (NestingDepth(S.Text) + NestingDepth(Nodes[J].Text) >= MaxInlineNesting)
                                    break;
                                auto Replacement = S.Text;
                                auto Position = Nodes[J].Text.find(Token(Id)) + Token(Id).size();
                                if (Position < Nodes[J].Text.size() &&
                                    std::string(".:[(").find(Nodes[J].Text[Position]) != std::string::npos &&
                                    !PrefixExpression(Replacement) &&
                                    !(Replacement.starts_with('(') && Closing(Replacement, 0) == Replacement.size() - 1))
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
                // Luau loads a builtin's function after its arguments. Constant
                // and import loads read nothing the RHS can change, so the
                // RHS may move past them to reach its consumer.
                // The skipped load must target a plain temporary the RHS does not
                // read: a store to a captured variable could be observed by a call.
                auto Skippable = [&](const Statement &Load)
                {
                    // SETLIST stores array items into a table still under
                    // construction; nothing else can observe that table yet.
                    if (Load.Type == Kind::Raw && Load.ListItem && !Load.Expand)
                    {
                        auto Open = Load.Text.find('[');
                        auto Close = Open == std::string::npos ? Open : Closing(Load.Text, Open);
                        if (Close == std::string::npos || Load.Text.compare(Close, 4, "] = ") != 0 ||
                            S.Text.find(Load.Text.substr(0, Open)) != std::string::npos)
                            return false;
                        auto Value = Load.Text.substr(Close + 4);
                        auto Stored = Tokens(Value);
                        return Literal(Value) || (Stored.size() == 1 && Value == Token(Stored[0]) && !E.Symbols[Stored[0]].Cell &&
                                                  !E.Symbols[Stored[0]].ReferenceAlias && Stored[0] != Id);
                    }
                    if (Load.Type != Kind::Assign || !Load.Pure || !Load.Body.empty() || Load.Text.find('\x1f') != std::string::npos)
                        return false;
                    return std::none_of(Load.Left.begin(), Load.Left.end(), [&](unsigned U)
                                        { return E.Symbols[U].Cell || E.Symbols[U].ReferenceAlias || WritesCount[U] != 1 ||
                                                 S.Text.find(Token(U)) != std::string::npos; });
                };
                std::size_t Consumer = N + 1;
                while (Consumer < Nodes.size() && Skippable(Nodes[Consumer]))
                    ++Consumer;
                if (Consumer < Nodes.size() && Reads[Id] == 1 && (S.Text.find('\n') == std::string::npos || S.Constructor) &&
                    Nodes[Consumer].Text.find(Token(Id)) != std::string::npos)
                {
                    auto &Next = Nodes[Consumer];
                    auto Uses = Tokens(Next.Text);
                    auto UsePosition = Next.Text.find(Token(Id));
                    auto BeforeUse = Next.Text.substr(0, UsePosition);
                    bool CanReorder = S.Pure;
                    for (auto Dependency : Tokens(S.Text))
                        if (E.Symbols[Dependency].Cell || E.Symbols[Dependency].ReferenceAlias)
                            CanReorder = false;
                    auto PrefixSymbols = Tokens(BeforeUse);
                    // A substitution moves the RHS after everything preceding
                    // its use. Earlier locals must be immutable if the RHS can
                    // mutate captures. Completed operations can invoke
                    // metamethods, and and/or/if can skip the RHS entirely.
                    // Inspect syntax rather than a character blacklist: a bare
                    // global read or `local and value` has no punctuation.
                    // A plain assignment target is a binding, not a value read.
                    // Even a mutable capture is assigned only after the RHS has
                    // finished; field/index targets still need the checks below.
                    bool ScalarAssignment = Next.Type == Kind::Raw && PrefixSymbols.size() == 1 && !E.Symbols[PrefixSymbols[0]].Cell &&
                                            BeforeUse == Token(PrefixSymbols[0]) + " = ";
                    bool Ordered = CanReorder || ScalarAssignment;
                    if (!Ordered)
                    {
                        Ordered = true;
                        for (auto U : PrefixSymbols)
                        {
                            const auto &Symbol = E.Symbols[U];
                            Ordered &= !Symbol.Cell && (S.Pure || (!Symbol.ReferenceAlias && WritesCount[U] <= 1));
                        }
                        // Everything before the use now runs before the RHS. The
                        // consuming operation itself (call, method lookup, store,
                        // index, arithmetic) still runs after all its operands, so
                        // its own syntax is safe. Reject any completed read that
                        // an earlier inlining placed there: a call, a field or
                        // index read, a global, or a short-circuit operator.
                        auto After = [&](std::size_t Close, std::string_view Text)
                        { return Close != std::string::npos && BeforeUse.compare(Close + 1, Text.size(), Text) == 0; };
                        for (std::size_t P = 0; Ordered && P < BeforeUse.size();)
                        {
                            auto C = static_cast<unsigned char>(BeforeUse[P]);
                            if (C == '\x1f')
                                P = BeforeUse.find('\x1e', P) + 1;
                            else if (C == '\'' || C == '"')
                            {
                                auto Quote = C;
                                ++P;
                                while (P < BeforeUse.size() && BeforeUse[P] != Quote)
                                {
                                    if (BeforeUse[P] == '\\')
                                        ++P;
                                    ++P;
                                }
                                Ordered = P < BeforeUse.size();
                                ++P;
                            }
                            else if (std::isdigit(C))
                            {
                                // Constants are emitted as decimal/hex numeric
                                // literals. Their evaluation has no effects.
                                ++P;
                                while (P < BeforeUse.size() &&
                                       (std::isalnum(static_cast<unsigned char>(BeforeUse[P])) || BeforeUse[P] == '.'))
                                    ++P;
                            }
                            else if (C == '.' && BeforeUse.compare(P, 2, "..") == 0)
                                P += 2;
                            else if (C == '.' || C == ':')
                            {
                                auto Start = ++P;
                                while (P < BeforeUse.size() && (std::isalnum(static_cast<unsigned char>(BeforeUse[P])) || BeforeUse[P] == '_'))
                                    ++P;
                                // `x.name = ` is a store target; `x:name(` looks the
                                // method up only after its arguments are evaluated.
                                if (C == '.')
                                    Ordered = P > Start && BeforeUse.compare(P, 3, " = ") == 0;
                                else
                                    Ordered = P > Start && P < BeforeUse.size() && BeforeUse[P] == '(' &&
                                              Closing(BeforeUse, P) == std::string::npos;
                            }
                            else if (C == '[')
                            {
                                // An open bracket contains the use; a closed one must be a store target.
                                auto Close = Closing(BeforeUse, P);
                                Ordered = Close == std::string::npos || After(Close, " = ");
                                ++P;
                            }
                            else if (C == '(')
                            {
                                // A call or grouping that closes before the use already ran.
                                auto Before = P;
                                while (Before && BeforeUse[Before - 1] == ' ')
                                    --Before;
                                auto Word = Before;
                                while (Word && (std::isalnum(static_cast<unsigned char>(BeforeUse[Word - 1])) || BeforeUse[Word - 1] == '_'))
                                    --Word;
                                bool Call = Before && (BeforeUse[Before - 1] == '\x1e' || BeforeUse[Before - 1] == ')' ||
                                                       BeforeUse[Before - 1] == ']' ||
                                                       (Word < Before && Identifier(BeforeUse.substr(Word, Before - Word))));
                                Ordered = !Call || Closing(BeforeUse, P) == std::string::npos;
                                ++P;
                            }
                            else if (auto Import = (std::isalpha(C) || C == '_') ? ImportAt(BeforeUse, P) : 0)
                                // An import path is a constant load.
                                P += Import;
                            else if (std::isalpha(C) || C == '_')
                            {
                                auto Start = P++;
                                while (P < BeforeUse.size() &&
                                       (std::isalnum(static_cast<unsigned char>(BeforeUse[P])) || BeforeUse[P] == '_'))
                                    ++P;
                                auto Word = BeforeUse.substr(Start, P - Start);
                                Ordered = Word == "return" || Word == "not" || Word == "nil" || Word == "true" || Word == "false";
                            }
                            else if (std::isspace(C) || std::string_view(",+-*/%^#<>=~{}]").find(char(C)) != std::string_view::npos)
                                ++P;
                            else
                                Ordered = false;
                        }
                    }
                    if (PrefixSymbols.size() == 1 && !E.Symbols[PrefixSymbols[0]].Cell && !E.Symbols[PrefixSymbols[0]].ReferenceAlias &&
                        WritesCount[PrefixSymbols[0]] <= 1 && BeforeUse.starts_with(Token(PrefixSymbols[0])))
                    {
                        auto Tail = BeforeUse.substr(Token(PrefixSymbols[0]).size());
                        // Assigning a constant field on a stable local has no effect before the RHS is evaluated.
                        if (Tail.starts_with('.') && Tail.ends_with(" = ") && Identifier(Tail.substr(1, Tail.size() - 4)))
                            Ordered = true;
                        if (Tail.starts_with('[') && Tail.ends_with("] = ") && Closing(Tail, 0) == Tail.size() - 4 &&
                            Literal(Tail.substr(1, Tail.size() - 5)))
                            Ordered = true;
                        // Luau evaluates method arguments before NAMECALL; the receiver here is already a stable local.
                        if (Tail.starts_with(':') && Tail.ends_with('(') && Identifier(Tail.substr(1, Tail.size() - 2)))
                            Ordered = true;
                    }
                    // Adjacent substitution cannot cross a side effect. Do not change a call's arity by inlining a multi-result call.
                    // A table constructor is not indexed or called in place: `({}).x`.
                    if (S.Constructor && UsePosition + Token(Id).size() < Next.Text.size() &&
                        std::string(".:[(").find(Next.Text[UsePosition + Token(Id).size()]) != std::string::npos)
                        Ordered = false;
                    if (Ordered && NestingDepth(S.Text) + NestingDepth(Next.Text) >= MaxInlineNesting)
                        Ordered = false;
                    if (Ordered && std::count(Uses.begin(), Uses.end(), Id) == 1 && Next.Type != Kind::While && Next.Type != Kind::Repeat &&
                        Next.Type != Kind::Function)
                    {
                        std::string Replacement = S.Text;
                        auto Begin = Next.Text.find(Token(Id));
                        auto Position = Begin + Token(Id).size();
                        bool ListAtEnd = Next.Type == Kind::Return || (Next.Type == Kind::Assign && Next.Left.size() > 1) ||
                                         (Next.Type == Kind::Raw && SplitLogical(BeforeUse, " = ").size() > 1 &&
                                          SplitLogical(BeforeUse.substr(0, BeforeUse.find(" = ")), ",").size() > 1);
                        if (S.Call && CallExpression(Unwrap(Replacement)) && MultiValueSlot(Next.Text, Begin, Position, ListAtEnd))
                            Replacement = "(" + Unwrap(Replacement) + ")";
                        if (Position < Next.Text.size() && std::string(".:[(").find(Next.Text[Position]) != std::string::npos &&
                            !PrefixExpression(Replacement) &&
                            !(Replacement.starts_with('(') && Closing(Replacement, 0) == Replacement.size() - 1))
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
        Source = Nodes.Release();
    };
    Apply(Nodes);
    std::function<void(std::vector<Statement> &)> Tables = [&](std::vector<Statement> &Source)
    {
        for (auto &S : Source)
        {
            Tables(S.Body);
            Tables(S.Else);
        }
        GapList List(Source);
        for (std::size_t N = 0; N < List.size(); ++N)
        {
            List.Focus(N + 1);
            auto &S = List[N];
            if (!S.Constructor || S.Type != Kind::Assign || S.Left.size() != 1 || TableText(S.Fields) != S.Text)
                continue;
            const auto TableId = S.Left[0];
            if (E.Symbols[TableId].Cell || E.Symbols[TableId].ReferenceAlias)
                continue;
            const auto Table = Token(TableId);
            // Fields may be stored directly after the constructor, or (for the
            // array part) evaluated into temporaries that SETLIST stores later.
            // Folding keeps every value's evaluation order: items are placed
            // where they were evaluated, not where they were stored.
            // An empty key is a positional (array) field.
            struct Item
            {
                std::string Key, Value;
                unsigned Pending = std::numeric_limits<unsigned>::max();
                bool Stored = true, Call = false;
            };
            std::vector<Item> Items;
            std::vector<std::size_t> Consumed, Committed;
            std::vector<Item> CommittedItems;
            std::set<unsigned> Skipped, Pending;
            auto Fields = S.Fields;
            auto CommittedFields = Fields;
            unsigned NextIndex = 1 + unsigned(std::count_if(Fields.begin(), Fields.end(), [](const auto &Field) { return Field.first.empty(); }));
            // A positional value keeps a single result unless it may expand last.
            auto Single = [](const std::string &Value, bool Call) { return Call && CallExpression(Unwrap(Value)) ? "(" + Unwrap(Value) + ")" : Value; };
            auto References = [&](const std::string &Text, const std::set<unsigned> &Symbols)
            {
                for (auto U : Tokens(Text))
                    if (Symbols.count(U))
                        return true;
                return false;
            };
            for (std::size_t J = N + 1; J < List.size(); ++J)
            {
                auto &Next = List[J];
                // Independent empty table allocations may be interleaved with
                // the first writes to this table.
                if (Next.Constructor && Next.Type == Kind::Assign && Next.Left.size() == 1 && Next.Text == "{}" && Pending.empty())
                {
                    Skipped.insert(Next.Left[0]);
                    continue;
                }
                if (Next.Type == Kind::Assign && Next.Left.size() == 1)
                {
                    auto U = Next.Left[0];
                    const auto &Value = E.Symbols[U];
                    if (Reads[U] != 1 || WritesCount[U] != 1 || Value.Keep || Value.Captured || Value.Cell || Value.ReferenceAlias ||
                        Next.Text.find(Table) != std::string::npos || References(Next.Text, Skipped) || References(Next.Text, Pending))
                        break;
                    Pending.insert(U);
                    Items.push_back({{}, Next.Text, U, false, Next.Call});
                    Consumed.push_back(J);
                    continue;
                }
                if (Next.Type != Kind::Raw || !Next.Text.starts_with(Table))
                    break;
                // The key ends at its identifier or bracket; values may contain
                // nested assignments inside function bodies.
                std::size_t KeyEnd = Table.size();
                if (Next.Text.compare(KeyEnd, 1, ".") == 0)
                {
                    ++KeyEnd;
                    while (KeyEnd < Next.Text.size() &&
                           (std::isalnum(static_cast<unsigned char>(Next.Text[KeyEnd])) || Next.Text[KeyEnd] == '_'))
                        ++KeyEnd;
                }
                else if (Next.Text.compare(KeyEnd, 1, "[") == 0)
                {
                    KeyEnd = Closing(Next.Text, KeyEnd);
                    if (KeyEnd == std::string::npos)
                        break;
                    ++KeyEnd;
                }
                if (Next.Text.compare(KeyEnd, 3, " = ") != 0)
                    break;
                auto Key = Next.Text.substr(Table.size(), KeyEnd - Table.size());
                auto Right = Next.Text.substr(KeyEnd + 3);
                if (Key.starts_with('.') && Identifier(Key.substr(1)))
                    Key.erase(0, 1);
                else if (!(Key.starts_with('[') && Key.ends_with(']') && Closing(Key, 0) == Key.size() - 1 &&
                           Literal(Key.substr(1, Key.size() - 2)) && Key != "[nil]" && Key != "[{}]"))
                    break;
                if (Right.find(Table) != std::string::npos || References(Right, Skipped))
                    break;
                // SETLIST items become positional fields; their indices must
                // continue the array part without gaps.
                const bool Positional = Next.ListItem;
                if (Positional)
                {
                    unsigned Index = 0;
                    auto Digits = Key.substr(1, Key.size() - 2);
                    auto [Last, Status] = std::from_chars(Digits.data(), Digits.data() + Digits.size(), Index);
                    if (Status != std::errc{} || Last != Digits.data() + Digits.size() || Index != NextIndex)
                        break;
                    Key.clear();
                }
                // A store of a pending array temporary completes that item.
                auto Stored = Tokens(Right);
                if (Stored.size() == 1 && Right == Token(Stored[0]) && Pending.count(Stored[0]))
                {
                    auto It = std::find_if(Items.begin(), Items.end(), [&](const Item &Entry) { return Entry.Pending == Stored[0]; });
                    // SETLIST stores items in evaluation order.
                    if (std::any_of(Items.begin(), It, [](const Item &Entry) { return !Entry.Stored; }))
                        break;
                    if (!Positional && (std::any_of(Fields.begin(), Fields.end(), [&](const auto &Field) { return Field.first == Key; }) ||
                                        std::any_of(Items.begin(), Items.end(), [&](const Item &Entry) { return Entry.Key == Key; })))
                        break;
                    It->Key = Key;
                    It->Stored = true;
                    if (Positional && !Next.Expand)
                        It->Value = Single(It->Value, It->Call);
                    Pending.erase(Stored[0]);
                }
                else if (Positional)
                {
                    if (References(Right, Pending))
                        break;
                    Items.push_back({{}, Next.Expand ? Right : Single(Right, Next.Call)});
                }
                else
                {
                    if (References(Right, Pending))
                        break;
                    auto Slot = std::find_if(Fields.begin(), Fields.end(), [&](const auto &Field) { return Field.first == Key; });
                    if (Slot != Fields.end() && !Slot->second.empty())
                        break;
                    if (std::any_of(Items.begin(), Items.end(), [&](const Item &Entry) { return Entry.Key == Key; }))
                        break;
                    if (Slot != Fields.end())
                        Slot->second = Right;
                    else
                        Items.push_back({Key, Right});
                }
                Consumed.push_back(J);
                if (Positional)
                    ++NextIndex;
                if (Pending.empty())
                {
                    Committed = Consumed;
                    CommittedItems = Items;
                    CommittedFields = Fields;
                }
                // Values of an expanded tail must remain the final field.
                if (Next.Expand)
                    break;
            }
            if (Committed.empty())
                continue;
            for (const auto &Entry : CommittedItems)
                CommittedFields.emplace_back(Entry.Key, Entry.Value);
            for (auto J = Committed.rbegin(); J != Committed.rend(); ++J)
                List.erase(List.begin() + std::ptrdiff_t(*J));
            // Erasing later statements keeps this reference valid.
            S.Fields = std::move(CommittedFields);
            S.Text = TableText(S.Fields);
            Changed = true;
        }
        Source = List.Release();
    };
    Tables(Nodes);
    return Changed;
}

} // namespace Astra::Detail
