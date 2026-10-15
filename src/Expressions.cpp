#include "Reconstruction.hpp"

namespace Taze::Detail
{
bool Identifier(std::string_view Text)
{
    static const std::set<std::string> Keywords = {"and", "break",    "do",     "else", "elseif", "end",   "false",
                                                   "for", "function", "if",     "in",   "local",  "nil",   "not",
                                                   "or",  "repeat",   "return", "then", "true",   "until", "while"};
    if (Text.empty() || Keywords.count(std::string(Text)))
        return false;
    if (!(std::isalpha(static_cast<unsigned char>(Text[0])) || Text[0] == '_'))
        return false;
    for (unsigned char C : Text)
        if (!(std::isalnum(C) || C == '_'))
            return false;
    return true;
}

std::string Pascal(std::string_view Text)
{
    std::string Out;
    bool Upper = true;
    for (unsigned char C : Text)
    {
        if (C >= 128 || !std::isalnum(C))
        {
            Upper = true;
            continue;
        }
        Out += Upper ? char(std::toupper(C)) : char(C);
        Upper = false;
    }
    if (Out.empty())
        Out = "Value";
    if (std::isdigit(static_cast<unsigned char>(Out[0])))
        Out = "Value" + Out;
    return Out;
}

std::string CaseName(std::string_view Text, VariableCase Casing)
{
    std::string Out;
    if (Casing == VariableCase::Preserve && Identifier(Text))
        return std::string(Text);
    if (Casing == VariableCase::Snake)
    {
        for (std::size_t I = 0; I < Text.size(); ++I)
        {
            unsigned char C = Text[I];
            if (C >= 128 || !std::isalnum(C))
            {
                if (!Out.empty() && Out.back() != '_') Out += '_';
                continue;
            }
            bool Boundary = I > 0 && std::isupper(C) &&
                (std::islower(static_cast<unsigned char>(Text[I - 1])) || std::isdigit(static_cast<unsigned char>(Text[I - 1])) ||
                 (std::isupper(static_cast<unsigned char>(Text[I - 1])) && I + 1 < Text.size() && std::islower(static_cast<unsigned char>(Text[I + 1]))));
            if (Boundary && !Out.empty() && Out.back() != '_') Out += '_';
            Out += char(std::tolower(C));
        }
        while (!Out.empty() && Out.back() == '_') Out.pop_back();
        if (Out.empty()) Out = "value";
        if (std::isdigit(static_cast<unsigned char>(Out.front()))) Out = "value_" + Out;
    }
    else
    {
        Out = Pascal(Text);
        if (Casing == VariableCase::Camel)
            for (std::size_t I = 0; I < Out.size() && std::isupper(static_cast<unsigned char>(Out[I])); ++I)
            {
                if (I > 0 && I + 1 < Out.size() && std::islower(static_cast<unsigned char>(Out[I + 1]))) break;
                Out[I] = char(std::tolower(static_cast<unsigned char>(Out[I])));
            }
    }
    if (!Identifier(Out)) Out += '_';
    return Out;
}

std::string Token(unsigned Id)
{
    return '\x1f' + std::to_string(Id) + '\x1e';
}
std::vector<unsigned> Tokens(const std::string &Text)
{
    std::vector<unsigned> Out;
    for (std::size_t Pos = 0; (Pos = Text.find('\x1f', Pos)) != std::string::npos;)
    {
        auto End = Text.find('\x1e', Pos);
        if (End == std::string::npos)
            throw Error("Internal: unterminated source symbol");
        Out.push_back(unsigned(std::stoul(Text.substr(Pos + 1, End - Pos - 1))));
        Pos = End + 1;
    }
    return Out;
}
void Replace(std::string &Text, const std::string &From, const std::string &To)
{
    for (std::size_t Pos = 0; (Pos = Text.find(From, Pos)) != std::string::npos; Pos += To.size())
        Text.replace(Pos, From.size(), To);
}
std::string Join(const std::vector<std::string> &Items, const std::string &Separator)
{
    std::string Out;
    for (const auto &Item : Items)
    {
        if (!Out.empty())
            Out += Separator;
        Out += Item;
    }
    return Out;
}
std::size_t Closing(const std::string &Text, std::size_t Start)
{
    char Open = Text[Start], Close = Open == '(' ? ')' : ']';
    unsigned Depth = 0;
    char QuoteChar = 0;
    for (std::size_t I = Start; I < Text.size(); ++I)
    {
        char C = Text[I];
        if (QuoteChar)
        {
            if (C == '\\')
                ++I;
            else if (C == QuoteChar)
                QuoteChar = 0;
            continue;
        }
        if (C == '\'' || C == '"')
        {
            QuoteChar = C;
            continue;
        }
        if (C == Open)
            ++Depth;
        if (C == Close && --Depth == 0)
            return I;
    }
    return std::string::npos;
}

std::string Unwrap(std::string Text)
{
    while (Text.size() >= 2 && Text.front() == '(' && Closing(Text, 0) == Text.size() - 1)
        Text = Text.substr(1, Text.size() - 2);
    return Text;
}

bool PrefixExpression(const std::string &Text)
{
    std::size_t Pos = 0;
    auto Name = [&]()
    {
        auto Start = Pos;
        if (Pos < Text.size() && Text[Pos] == '\x1f')
        {
            auto End = Text.find('\x1e', Pos);
            if (End == std::string::npos)
                return false;
            Pos = End + 1;
            return true;
        }
        while (Pos < Text.size() && (std::isalnum(static_cast<unsigned char>(Text[Pos])) || Text[Pos] == '_'))
            ++Pos;
        return Identifier(Text.substr(Start, Pos - Start));
    };
    if (!Name())
        return false;
    while (Pos < Text.size())
    {
        if (Text[Pos] == '.' || Text[Pos] == ':')
        {
            ++Pos;
            if (!Name())
                return false;
        }
        else if (Text[Pos] == '(' || Text[Pos] == '[')
        {
            auto End = Closing(Text, Pos);
            if (End == std::string::npos)
                return false;
            Pos = End + 1;
        }
        else
            return false;
    }
    return true;
}

std::string CleanExpression(std::string Text)
{
    if (Text.find('\n') != std::string::npos)
        return Text;
    char QuoteChar = 0;
    for (std::size_t Pos = 0; Pos < Text.size(); ++Pos)
    {
        if (QuoteChar)
        {
            if (Text[Pos] == '\\')
                ++Pos;
            else if (Text[Pos] == QuoteChar)
                QuoteChar = 0;
            continue;
        }
        if (Text[Pos] == '\'' || Text[Pos] == '"')
        {
            QuoteChar = Text[Pos];
            continue;
        }
        if (Text[Pos] != '(')
            continue;
        auto End = Closing(Text, Pos);
        if (End == std::string::npos || End + 1 == Text.size())
            continue;
        if (std::string(".:[(").find(Text[End + 1]) == std::string::npos)
            continue;
        auto Inner = Text.substr(Pos + 1, End - Pos - 1);
        if (PrefixExpression(Inner))
        {
            Text.erase(End, 1);
            Text.erase(Pos, 1);
            if (Pos)
                --Pos;
        }
    }
    return Text;
}

std::string Negate(std::string Text)
{
    Text = Unwrap(Text);
    if (Text.starts_with("not ") && (PrefixExpression(Text.substr(4)) || (Text[4] == '(' && Closing(Text, 4) == Text.size() - 1)))
        return Unwrap(Text.substr(4));
    return "not " + (PrefixExpression(Text) ? Text : "(" + Text + ")");
}

std::string CleanCondition(std::string Text)
{
    Text = Unwrap(CleanExpression(Text));
    if (Text.starts_with("not "))
    {
        auto Operand = Text.substr(4);
        if (!PrefixExpression(Operand) && !(Operand.starts_with('(') && Closing(Operand, 0) == Operand.size() - 1) &&
            !Operand.starts_with("not "))
            return Text;
        auto Inner = Unwrap(Text.substr(4));
        if (Inner.starts_with("not "))
            return CleanCondition(Inner.substr(4));
        // Equality negation is safe for metamethods and NaN; ordering negation is not.
        unsigned Depth = 0;
        char QuoteChar = 0;
        for (std::size_t I = 0; I + 3 < Inner.size(); ++I)
        {
            char C = Inner[I];
            if (QuoteChar)
            {
                if (C == '\\')
                    ++I;
                else if (C == QuoteChar)
                    QuoteChar = 0;
                continue;
            }
            if (C == '\'' || C == '"')
            {
                QuoteChar = C;
                continue;
            }
            if (C == '(' || C == '[')
                ++Depth;
            else if (C == ')' || C == ']')
                --Depth;
            else if (!Depth && Inner.find(" and ") == std::string::npos && Inner.find(" or ") == std::string::npos &&
                     (Inner.substr(I, 4) == " == " || Inner.substr(I, 4) == " ~= "))
            {
                Inner.replace(I, 4, Inner.substr(I, 4) == " == " ? " ~= " : " == ");
                return Inner;
            }
        }
        return Negate(Inner);
    }
    return Text;
}

// Locate operators outside nested expressions and string literals, preserving precedence.
std::vector<std::string> SplitLogical(const std::string &Text, const std::string &Operator)
{
    unsigned Depth = 0;
    char QuoteChar = 0;
    std::size_t Start = 0;
    std::vector<std::string> Parts;
    for (std::size_t I = 0; I < Text.size(); ++I)
    {
        char C = Text[I];
        if (QuoteChar)
        {
            if (C == '\\')
                ++I;
            else if (C == QuoteChar)
                QuoteChar = 0;
            continue;
        }
        if (C == '\'' || C == '"')
        {
            QuoteChar = C;
            continue;
        }
        if (C == '(' || C == '[' || C == '{')
            ++Depth;
        else if (C == ')' || C == ']' || C == '}')
            --Depth;
        else if (!Depth && Text.compare(I, Operator.size(), Operator) == 0)
        {
            Parts.push_back(Text.substr(Start, I - Start));
            I += Operator.size() - 1;
            Start = I + 1;
        }
    }
    Parts.push_back(Text.substr(Start));
    return Parts;
}

std::string FormatCondition(std::string Text, unsigned ParentPrecedence)
{
    Text = CleanCondition(Text);
    if (Text.starts_with("if ") || Text.find('\n') != std::string::npos)
        return Text;
    for (unsigned Precedence = 1; Precedence <= 2; ++Precedence)
    {
        std::string Operator = Precedence == 1 ? " or " : " and ";
        auto Parts = SplitLogical(Text, Operator);
        if (Parts.size() == 1)
            continue;
        for (auto &Part : Parts)
            Part = FormatCondition(Part, Precedence);
        Text = Join(Parts, Operator);
        return Precedence < ParentPrecedence ? "(" + Text + ")" : Text;
    }
    return Text;
}


} // namespace Taze::Detail
