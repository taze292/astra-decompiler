#include "Reconstruction.hpp"
#include <cstring>

// Precedence-aware removal of redundant grouping parentheses.
//
// Lowering wraps every operation in parentheses so that textual substitution
// can never change how an expression parses. This pass parses the generated
// Luau expression syntax (including source-symbol tokens) and deletes only the
// parentheses whose removal provably keeps the same parse and value count.
// It edits the text in place, so nested function bodies, comments, and table
// layout are preserved exactly. Any construct it does not understand leaves
// the text unchanged.
namespace Astra::Detail
{
namespace
{
enum class TokenKind
{
    Name,
    Keyword,
    Number,
    String,
    Symbol,
    Function,
    End
};

struct Lexeme
{
    TokenKind Kind;
    std::size_t Begin, End;
    std::string_view Text;
};

bool Keyword(std::string_view Text)
{
    static const std::set<std::string_view> Words = {"and", "break", "do",   "else", "elseif", "end",   "false",
                                                     "for", "function", "if", "in",   "local",  "nil",   "not",
                                                     "or",  "repeat", "return", "then", "true", "until", "while", "continue"};
    return Words.count(Text) != 0;
}

class Lexer
{
  public:
    explicit Lexer(const std::string &Source) : S(Source) {}

    // Returns false when the text contains syntax this pass does not model.
    bool Run(std::vector<Lexeme> &Out)
    {
        while (true)
        {
            Skip();
            if (Pos >= S.size())
            {
                Out.push_back({TokenKind::End, Pos, Pos, {}});
                return true;
            }
            auto Begin = Pos;
            unsigned char C = S[Pos];
            if (C == '\x1f')
            {
                auto Close = S.find('\x1e', Pos);
                if (Close == std::string::npos)
                    return false;
                Pos = Close + 1;
                Out.push_back({TokenKind::Name, Begin, Pos, View(Begin)});
            }
            else if (std::isalpha(C) || C == '_')
            {
                while (Pos < S.size() && (std::isalnum(static_cast<unsigned char>(S[Pos])) || S[Pos] == '_'))
                    ++Pos;
                auto Text = View(Begin);
                if (Text == "function")
                {
                    if (!SkipFunction())
                        return false;
                    Out.push_back({TokenKind::Function, Begin, Pos, View(Begin)});
                }
                else
                    Out.push_back({Keyword(Text) ? TokenKind::Keyword : TokenKind::Name, Begin, Pos, Text});
            }
            else if (std::isdigit(C) || (C == '.' && Pos + 1 < S.size() && std::isdigit(static_cast<unsigned char>(S[Pos + 1]))))
            {
                SkipNumber();
                Out.push_back({TokenKind::Number, Begin, Pos, View(Begin)});
            }
            else if (C == '"' || C == '\'')
            {
                if (!SkipString())
                    return false;
                Out.push_back({TokenKind::String, Begin, Pos, View(Begin)});
            }
            else if (C == '[' && LongBracket(Pos) != std::string::npos)
                return false;
            else
            {
                static const char *Long[] = {"...", "..", "==", "~=", "<=", ">=", "//", "::", "+=", "-=", "*=", "/=", "%=", "^=", "..="};
                std::size_t Length = 1;
                for (const char *Op : Long)
                    if (S.compare(Pos, std::strlen(Op), Op) == 0)
                        Length = std::max(Length, std::strlen(Op));
                Pos += Length;
                auto Text = View(Begin);
                if (Text == "::" || Text.size() > 1 && Text.back() == '=' && Text != "==" && Text != "~=" && Text != "<=" && Text != ">=")
                    return false;
                if (Text.size() == 1 && std::string_view("+-*/%^#<>=(){}[];:,.").find(Text[0]) == std::string_view::npos)
                    return false;
                Out.push_back({TokenKind::Symbol, Begin, Pos, Text});
            }
        }
    }

  private:
    const std::string &S;
    std::size_t Pos = 0;

    std::string_view View(std::size_t Begin) const
    {
        return std::string_view(S).substr(Begin, Pos - Begin);
    }

    // Position after a long bracket opener ([[ or [==[), or npos.
    std::size_t LongBracket(std::size_t At) const
    {
        if (At >= S.size() || S[At] != '[')
            return std::string::npos;
        auto P = At + 1;
        while (P < S.size() && S[P] == '=')
            ++P;
        return P < S.size() && S[P] == '[' ? P + 1 : std::string::npos;
    }

    void Skip()
    {
        while (Pos < S.size())
        {
            if (std::isspace(static_cast<unsigned char>(S[Pos])))
                ++Pos;
            else if (S.compare(Pos, 2, "--") == 0)
            {
                auto Open = LongBracket(Pos + 2);
                if (Open != std::string::npos)
                {
                    auto Level = Open - Pos - 4;
                    auto Close = S.find("]" + std::string(Level, '=') + "]", Open);
                    Pos = Close == std::string::npos ? S.size() : Close + Level + 2;
                }
                else
                {
                    auto Line = S.find('\n', Pos);
                    Pos = Line == std::string::npos ? S.size() : Line;
                }
            }
            else
                break;
        }
    }

    void SkipNumber()
    {
        if (S.compare(Pos, 2, "0x") == 0 || S.compare(Pos, 2, "0X") == 0 || S.compare(Pos, 2, "0b") == 0 || S.compare(Pos, 2, "0B") == 0)
            Pos += 2;
        while (Pos < S.size())
        {
            unsigned char C = S[Pos];
            if ((C == 'e' || C == 'E') && Pos + 1 < S.size() && (S[Pos + 1] == '+' || S[Pos + 1] == '-'))
                Pos += 2;
            else if (std::isalnum(C) || C == '.' || C == '_')
                ++Pos;
            else
                break;
        }
    }

    bool SkipString()
    {
        char Quote = S[Pos++];
        while (Pos < S.size() && S[Pos] != Quote)
        {
            if (S[Pos] == '\n')
                return false;
            Pos += S[Pos] == '\\' ? 2 : 1;
        }
        if (Pos >= S.size())
            return false;
        ++Pos;
        return true;
    }

    // Skip a function literal through its matching `end`. Statements inside
    // are not inspected; only block nesting, strings, and comments matter.
    bool SkipFunction()
    {
        unsigned Depth = 1;
        while (Pos < S.size())
        {
            Skip();
            if (Pos >= S.size())
                break;
            unsigned char C = S[Pos];
            if (C == '"' || C == '\'')
            {
                if (!SkipString())
                    return false;
            }
            else if (C == '[' && LongBracket(Pos) != std::string::npos)
                return false;
            else if (std::isalpha(C) || C == '_')
            {
                auto Begin = Pos;
                while (Pos < S.size() && (std::isalnum(static_cast<unsigned char>(S[Pos])) || S[Pos] == '_'))
                    ++Pos;
                auto Word = View(Begin);
                if (Word == "function" || Word == "if" || Word == "do" || Word == "repeat")
                    ++Depth;
                else if ((Word == "end" || Word == "until") && --Depth == 0)
                    return Word == "end";
            }
            else if (C == '\x1f')
            {
                auto Close = S.find('\x1e', Pos);
                if (Close == std::string::npos)
                    return false;
                Pos = Close + 1;
            }
            else if (std::isdigit(C))
                SkipNumber();
            else
                ++Pos;
        }
        return false;
    }
};

enum class Slot
{
    Free,      // single value: assignment source, argument, key, condition
    Tail,      // last element of a multi-value list
    Prefix,    // base of an index, call, or method call
    Operand,   // operand of a binary operator
    Unary      // operand of a unary operator
};

struct Node
{
    enum Type
    {
        Atom,
        Group,
        Binary,
        Unary,
        Suffixed,
        IfElse,
        Table
    } Kind = Atom;
    bool PrefixExpression = false; // name, call, index, or group
    bool MultiValue = false;       // call or vararg
    int Strength = 100;            // binding strength when used as an operand
    std::string_view Operator;
    std::size_t Open = 0, Close = 0;
    std::vector<std::pair<int, Slot>> Children;
    int Height = 1; // nesting depth, bounded to keep recursion shallow
    std::size_t Begin = 0, End = 0; // source span
};

int BinaryStrength(std::string_view Op)
{
    if (Op == "or")
        return 1;
    if (Op == "and")
        return 2;
    if (Op == "<" || Op == ">" || Op == "<=" || Op == ">=" || Op == "~=" || Op == "==")
        return 3;
    if (Op == "..")
        return 4;
    if (Op == "+" || Op == "-")
        return 5;
    if (Op == "*" || Op == "/" || Op == "//" || Op == "%")
        return 6;
    if (Op == "^")
        return 8;
    return 0;
}

constexpr int UnaryStrength = 7;
// Deeper expressions are left exactly as generated rather than risking the stack.
constexpr int MaxHeight = 128;

class Parser
{
  public:
    Parser(const std::string &Source, std::vector<Lexeme> Tokens) : S(Source), T(std::move(Tokens)) {}

    std::vector<Node> Nodes;
    bool Failed = false;

    bool AtEnd() const
    {
        return T[Pos].Kind == TokenKind::End;
    }
    bool Check(std::string_view Text) const
    {
        return (T[Pos].Kind == TokenKind::Symbol || T[Pos].Kind == TokenKind::Keyword) && T[Pos].Text == Text;
    }
    bool Accept(std::string_view Text)
    {
        if (!Check(Text))
            return false;
        ++Pos;
        return true;
    }
    void Expect(std::string_view Text)
    {
        if (!Accept(Text))
            Failed = true;
    }

    int Expression(int Limit = 0)
    {
        struct Guard
        {
            int &Depth;
            ~Guard()
            {
                --Depth;
            }
        } Nesting{++Depth};
        if (Depth > MaxHeight)
        {
            Failed = true;
            return Add({});
        }
        int Left;
        const auto First = Pos;
        if (Check("not") || Check("-") || Check("#"))
        {
            auto Op = T[Pos].Text;
            ++Pos;
            int Operand = Expression(UnaryStrength);
            Left = Span(Add({Node::Unary, false, false, UnaryStrength, Op, 0, 0, {{Operand, Slot::Unary}}}), First);
        }
        else
            Left = Simple();
        while (!Failed)
        {
            auto Op = T[Pos].Text;
            int Strength = (T[Pos].Kind == TokenKind::Symbol || T[Pos].Kind == TokenKind::Keyword) ? BinaryStrength(Op) : 0;
            if (!Strength || Strength <= Limit)
                break;
            bool Right = Op == ".." || Op == "^";
            ++Pos;
            // Right-associative operators accept an equal-strength right operand.
            int Rhs = Expression(Right ? Strength - 1 : Strength);
            Left = Span(Add({Node::Binary, false, false, Strength, Op, 0, 0, {{Left, Slot::Operand}, {Rhs, Slot::Operand}}}), First);
        }
        return Left;
    }

    // Comma-separated expression list; the last element can receive multiple values.
    std::vector<int> List()
    {
        std::vector<int> Items{Expression()};
        while (!Failed && Accept(","))
            Items.push_back(Expression());
        return Items;
    }

  private:
    const std::string &S;
    std::vector<Lexeme> T;
    std::size_t Pos = 0;
    int Depth = 0;

    int Span(int Id, std::size_t First)
    {
        Nodes[std::size_t(Id)].Begin = T[First].Begin;
        Nodes[std::size_t(Id)].End = Pos ? T[Pos - 1].End : T[First].Begin;
        return Id;
    }

    int Add(Node N)
    {
        for (auto [Child, Context] : N.Children)
            N.Height = std::max(N.Height, Nodes[std::size_t(Child)].Height + 1);
        if (N.Height > MaxHeight)
            Failed = true;
        Nodes.push_back(std::move(N));
        return int(Nodes.size() - 1);
    }

    int Simple()
    {
        const auto First = Pos;
        const auto &Token = T[Pos];
        if (Token.Kind == TokenKind::Number || Token.Kind == TokenKind::String || Token.Kind == TokenKind::Function ||
            (Token.Kind == TokenKind::Keyword && (Token.Text == "nil" || Token.Text == "true" || Token.Text == "false")))
        {
            ++Pos;
            return Span(Add({}), First);
        }
        if (Check("..."))
        {
            ++Pos;
            return Span(Add({Node::Atom, false, true}), First);
        }
        if (Check("{"))
            return Span(Constructor(), First);
        if (Check("if"))
        {
            ++Pos;
            Node N{Node::IfElse, false, false, 0};
            N.Children.push_back({Expression(), Slot::Free});
            Expect("then");
            N.Children.push_back({Expression(), Slot::Free});
            while (!Failed && Accept("elseif"))
            {
                N.Children.push_back({Expression(), Slot::Free});
                Expect("then");
                N.Children.push_back({Expression(), Slot::Free});
            }
            Expect("else");
            N.Children.push_back({Expression(), Slot::Free});
            return Span(Add(std::move(N)), First);
        }
        return Suffixed();
    }

    int Constructor()
    {
        Expect("{");
        Node N{Node::Table};
        while (!Failed && !Check("}"))
        {
            bool Positional = true;
            if (Accept("["))
            {
                N.Children.push_back({Expression(), Slot::Free});
                Expect("]");
                Expect("=");
                Positional = false;
            }
            else if (T[Pos].Kind == TokenKind::Name && T[Pos + 1].Kind == TokenKind::Symbol && T[Pos + 1].Text == "=")
            {
                Pos += 2;
                Positional = false;
            }
            int Value = Expression();
            bool Last = !(Check(",") || Check(";")) || T[Pos + 1].Text == "}";
            N.Children.push_back({Value, Positional && Last ? Slot::Tail : Slot::Free});
            if (!Accept(",") && !Accept(";"))
                break;
        }
        Expect("}");
        return Add(std::move(N));
    }

    int Primary()
    {
        const auto First = Pos;
        if (T[Pos].Kind == TokenKind::Name)
        {
            ++Pos;
            return Span(Add({Node::Atom, true}), First);
        }
        if (Check("("))
        {
            auto Open = T[Pos].Begin;
            ++Pos;
            int Inner = Expression();
            if (!Check(")"))
            {
                Failed = true;
                return Add({});
            }
            auto Close = T[Pos].Begin;
            ++Pos;
            return Span(Add({Node::Group, true, false, 100, {}, Open, Close, {{Inner, Slot::Free}}}), First);
        }
        Failed = true;
        return Add({});
    }

    int Suffixed()
    {
        const auto First = Pos;
        int Base = Primary();
        while (!Failed)
        {
            if (Accept("."))
            {
                if (T[Pos].Kind != TokenKind::Name)
                {
                    Failed = true;
                    break;
                }
                ++Pos;
                Base = Span(Add({Node::Suffixed, true, false, 100, {}, 0, 0, {{Base, Slot::Prefix}}}), First);
            }
            else if (Accept("["))
            {
                int Key = Expression();
                Expect("]");
                Base = Span(Add({Node::Suffixed, true, false, 100, {}, 0, 0, {{Base, Slot::Prefix}, {Key, Slot::Free}}}), First);
            }
            else if (Check(":") || Check("("))
            {
                Node Call{Node::Suffixed, true, true, 100, {}, 0, 0, {{Base, Slot::Prefix}}};
                if (Accept(":"))
                {
                    if (T[Pos].Kind != TokenKind::Name)
                    {
                        Failed = true;
                        break;
                    }
                    ++Pos;
                }
                // A call on a new line would be ambiguous with a new statement;
                // generated text never puts a call's parenthesis there.
                Expect("(");
                if (!Check(")"))
                {
                    auto Arguments = List();
                    for (std::size_t J = 0; J < Arguments.size(); ++J)
                        Call.Children.push_back({Arguments[J], J + 1 == Arguments.size() ? Slot::Tail : Slot::Free});
                }
                Expect(")");
                Base = Span(Add(std::move(Call)), First);
            }
            else
                break;
        }
        return Base;
    }
};

class Simplifier
{
  public:
    Simplifier(const std::string &Source, const std::vector<Node> &Parsed) : S(Source), Nodes(Parsed), Remove(Source.size(), false) {}

    void Visit(int Id, Slot Context, int ParentStrength = 0, bool RightOperand = false, std::string_view ParentOperator = {})
    {
        const auto &N = Nodes[std::size_t(Id)];
        if (N.Kind == Node::Group)
        {
            const auto &Inner = Nodes[std::size_t(N.Children[0].first)];
            if (Removable(Inner, Context, ParentStrength, RightOperand, ParentOperator))
            {
                Remove[N.Open] = Remove[N.Close] = true;
                // The inner expression now occupies the group's slot.
                Visit(N.Children[0].first, Context, ParentStrength, RightOperand, ParentOperator);
            }
            else
                Visit(N.Children[0].first, Slot::Free);
            return;
        }
        for (std::size_t J = 0; J < N.Children.size(); ++J)
        {
            auto [Child, ChildSlot] = N.Children[J];
            if (N.Kind == Node::Binary)
                Visit(Child, Slot::Operand, N.Strength, J == 1, N.Operator);
            else
                Visit(Child, ChildSlot, N.Strength);
        }
    }

    std::string Result() const
    {
        std::string Out;
        Out.reserve(S.size());
        for (std::size_t I = 0; I < S.size(); ++I)
        {
            if (!Remove[I])
            {
                Out += S[I];
                continue;
            }
            // Never join two words, two minus signs (a comment), or two dots.
            char Before = Out.empty() ? ' ' : Out.back();
            std::size_t Next = I + 1;
            while (Next < S.size() && Remove[Next])
                ++Next;
            char After = Next < S.size() ? S[Next] : ' ';
            auto Word = [](char C) { return std::isalnum(static_cast<unsigned char>(C)) || C == '_' || C == '\x1e' || C == '\x1f'; };
            if ((Word(Before) && Word(After)) || (Before == '-' && After == '-') || (Before == '.' && After == '.'))
                Out += ' ';
        }
        return Out;
    }

  private:
    const std::string &S;
    const std::vector<Node> &Nodes;
    std::vector<bool> Remove;

    static bool Removable(const Node &Inner, Slot Context, int ParentStrength, bool RightOperand, std::string_view ParentOperator)
    {
        switch (Context)
        {
        case Slot::Free:
            return true;
        case Slot::Tail:
            return !Inner.MultiValue;
        case Slot::Prefix:
            return Inner.PrefixExpression;
        case Slot::Unary:
            // Keep `-(a ^ b)` grouped for readability even though it is optional.
            return Inner.Strength > UnaryStrength && !(Inner.Kind == Node::Binary && Inner.Operator == "^");
        case Slot::Operand:
        {
            if (Inner.Strength > ParentStrength)
                return true;
            if (Inner.Strength < ParentStrength || Inner.Kind != Node::Binary)
                return false;
            // Equal strength: comparisons stay grouped for readability. Other
            // operators may drop the group only on their associative side.
            if (ParentStrength == 3)
                return false;
            // `a and (b and c)` evaluates and returns exactly as `a and b and c`.
            if (ParentStrength <= 2 && Inner.Operator == ParentOperator)
                return true;
            bool RightAssociative = ParentOperator == ".." || ParentOperator == "^";
            return RightOperand == RightAssociative;
        }
        }
        return false;
    }
};

bool Parse(const std::string &Text, std::vector<Lexeme> &Tokens)
{
    return Lexer(Text).Run(Tokens);
}
} // namespace

std::string SimplifyList(const std::string &Text, bool TailLast)
{
    std::vector<Lexeme> Tokens;
    if (Text.empty() || !Parse(Text, Tokens))
        return Text;
    Parser P(Text, std::move(Tokens));
    auto Items = P.List();
    if (P.Failed || !P.AtEnd())
        return Text;
    Simplifier S(Text, P.Nodes);
    for (std::size_t J = 0; J < Items.size(); ++J)
        S.Visit(Items[J], TailLast && J + 1 == Items.size() ? Slot::Tail : Slot::Free);
    return S.Result();
}

std::string SimplifyStatement(const std::string &Text)
{
    std::vector<Lexeme> Tokens;
    if (Text.empty() || !Parse(Text, Tokens))
        return Text;
    Parser P(Text, std::move(Tokens));
    auto Targets = P.List();
    if (P.Failed)
        return Text;
    Simplifier S(Text, P.Nodes);
    if (P.AtEnd())
    {
        // A call statement. Its callee remains a prefix expression.
        if (Targets.size() != 1 || !P.Nodes[std::size_t(Targets[0])].MultiValue)
            return Text;
        S.Visit(Targets[0], Slot::Free);
        return S.Result();
    }
    if (!P.Accept("="))
        return Text;
    auto Values = P.List();
    if (P.Failed || !P.AtEnd())
        return Text;
    // Assignment targets keep their own prefix syntax; only their keys and
    // bases are simplified, never the target itself.
    for (auto Target : Targets)
        for (auto [Child, Context] : P.Nodes[std::size_t(Target)].Children)
            S.Visit(Child, Context);
    for (std::size_t J = 0; J < Values.size(); ++J)
        S.Visit(Values[J], Targets.size() > 1 && J + 1 == Values.size() ? Slot::Tail : Slot::Free);
    return S.Result();
}
} // namespace Astra::Detail

namespace Astra::Detail
{
std::string SimplifyCondition(const std::string &Text)
{
    // Only truthiness matters in a condition, so `not not x` is `x` and a
    // negated and/or chain may push its negation into the operands (De
    // Morgan) whenever that does not add negations. Evaluation order and
    // short-circuiting are unchanged by both rewrites.
    std::vector<Lexeme> Tokens;
    if (Text.empty() || !Parse(Text, Tokens))
        return Text;
    Parser P(Text, std::move(Tokens));
    int Root = P.Expression();
    if (P.Failed || !P.AtEnd())
        return Text;
    const auto &Nodes = P.Nodes;
    auto Source = [&](int Id) { return Text.substr(Nodes[std::size_t(Id)].Begin, Nodes[std::size_t(Id)].End - Nodes[std::size_t(Id)].Begin); };
    auto Strip = [&](int Id)
    {
        while (Nodes[std::size_t(Id)].Kind == Node::Group)
            Id = Nodes[std::size_t(Id)].Children[0].first;
        return Id;
    };
    std::function<void(int, std::string_view, std::vector<int> &)> Chain = [&](int Id, std::string_view Op, std::vector<int> &Parts)
    {
        Id = Strip(Id);
        const auto &N = Nodes[std::size_t(Id)];
        if (N.Kind == Node::Binary && N.Operator == Op)
        {
            Chain(N.Children[0].first, Op, Parts);
            Chain(N.Children[1].first, Op, Parts);
        }
        else
            Parts.push_back(Id);
    };
    auto Negatable = [&](int Id)
    {
        const auto &N = Nodes[std::size_t(Strip(Id))];
        return (N.Kind == Node::Unary && N.Operator == "not") || (N.Kind == Node::Binary && (N.Operator == "==" || N.Operator == "~="));
    };
    std::function<std::string(int, bool)> Condition = [&](int Id, bool Negated) -> std::string
    {
        Id = Strip(Id);
        const auto &N = Nodes[std::size_t(Id)];
        if (N.Kind == Node::Unary && N.Operator == "not")
            return Condition(N.Children[0].first, !Negated);
        if (N.Kind == Node::Binary && (N.Operator == "and" || N.Operator == "or"))
        {
            std::vector<int> Parts;
            Chain(Id, N.Operator, Parts);
            auto Inverted = std::size_t(std::count_if(Parts.begin(), Parts.end(), Negatable));
            if (Negated && Inverted * 2 < Parts.size())
                return "not (" + Condition(Id, false) + ")";
            std::vector<std::string> Texts;
            for (int Part : Parts)
                Texts.push_back("(" + Condition(Part, Negated) + ")");
            return Join(Texts, (N.Operator == "and") != Negated ? " and " : " or ");
        }
        if (N.Kind == Node::Binary && (N.Operator == "==" || N.Operator == "~=") && Negated)
            return Source(N.Children[0].first) + (N.Operator == "==" ? " ~= " : " == ") + Source(N.Children[1].first);
        // Luau compiles `x > 2` as `2 < x`; the language defines one as the
        // other, and a literal operand has no evaluation order to preserve.
        auto Literal = [&](int Operand)
        {
            Operand = Strip(Operand);
            const auto &Value = Nodes[std::size_t(Operand)];
            if (Value.Kind == Node::Unary && Value.Operator == "-")
                Operand = Strip(Value.Children[0].first);
            auto Text = Source(Operand);
            return Nodes[std::size_t(Operand)].Kind == Node::Atom && !Text.empty() &&
                   (std::isdigit(static_cast<unsigned char>(Text[0])) || Text[0] == '"' || Text[0] == '\'');
        };
        if (N.Kind == Node::Binary && (N.Operator == "<" || N.Operator == "<=") && Literal(N.Children[0].first) &&
            !Literal(N.Children[1].first))
        {
            auto Swapped = "(" + Source(N.Children[1].first) + ") " + (N.Operator == "<" ? ">" : ">=") + " (" + Source(N.Children[0].first) + ")";
            return Negated ? "not (" + Swapped + ")" : Swapped;
        }
        return Negated ? "not (" + Source(Id) + ")" : Source(Id);
    };
    return SimplifyList(Condition(Root, false), false);
}
} // namespace Astra::Detail

namespace Astra::Detail
{
bool BooleanExpression(const std::string &Text)
{
    // Negations and comparisons always produce true or false, and so does an
    // and/or chain of them.
    std::vector<Lexeme> Tokens;
    if (Text.empty() || !Parse(Text, Tokens))
        return false;
    Parser P(Text, std::move(Tokens));
    int Root = P.Expression();
    if (P.Failed || !P.AtEnd())
        return false;
    std::function<bool(int)> Boolean = [&](int Id)
    {
        const auto *N = &P.Nodes[std::size_t(Id)];
        while (N->Kind == Node::Group)
            N = &P.Nodes[std::size_t(N->Children[0].first)];
        if (N->Kind == Node::Unary)
            return N->Operator == "not";
        if (N->Kind != Node::Binary)
            return false;
        if (N->Strength == 3)
            return true;
        return (N->Operator == "and" || N->Operator == "or") && Boolean(N->Children[0].first) && Boolean(N->Children[1].first);
    };
    return Boolean(Root);
}
} // namespace Astra::Detail
