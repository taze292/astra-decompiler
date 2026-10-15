#include "Reconstruction.hpp"
#include <charconv>

namespace Astra::Detail
{
namespace
{
struct SourceToken
{
    std::size_t Begin, End;
    std::string_view Text;
};

// Formatting works on source tokens, never on string contents or comments.
// The final pass also covers constant-table templates and helper tables that
// do not pass through the statement optimizer.
std::string FormatTables(std::string Source, const Options &Settings)
{
    if (Source.find('{') == std::string::npos)
        return Source;
    std::vector<SourceToken> Tokens;
    auto LongEnd = [&](std::size_t Begin) -> std::size_t
    {
        if (Source[Begin] != '[')
            return Begin;
        auto Pos = Begin + 1;
        while (Pos < Source.size() && Source[Pos] == '=')
            ++Pos;
        if (Pos == Source.size() || Source[Pos] != '[')
            return Begin;
        auto Close = "]" + std::string(Pos - Begin - 1, '=') + "]";
        auto End = Source.find(Close, Pos + 1);
        return End == std::string::npos ? Source.size() : End + Close.size();
    };
    unsigned BraceDepth = 0;
    for (std::size_t Pos = 0; Pos < Source.size();)
    {
        if (std::isspace(static_cast<unsigned char>(Source[Pos])))
        {
            ++Pos;
            continue;
        }
        if (Source.compare(Pos, 2, "--") == 0)
        {
            auto End = Pos + 2 < Source.size() ? LongEnd(Pos + 2) : Pos + 2;
            if (End == Pos + 2)
            {
                End = Source.find('\n', Pos + 2);
                if (End == std::string::npos)
                    End = Source.size();
            }
            Pos = End;
            continue;
        }
        auto Begin = Pos++;
        if (Source[Begin] == '\'' || Source[Begin] == '"')
        {
            while (Pos < Source.size())
            {
                if (Source[Pos] == '\\')
                    Pos = std::min(Pos + 2, Source.size());
                else if (Source[Pos++] == Source[Begin])
                    break;
            }
        }
        else if (auto End = LongEnd(Begin); End != Begin)
            Pos = End;
        else if (std::isalnum(static_cast<unsigned char>(Source[Begin])) || Source[Begin] == '_')
            while (Pos < Source.size() && (std::isalnum(static_cast<unsigned char>(Source[Pos])) || Source[Pos] == '_'))
                ++Pos;
        auto Text = std::string_view(Source).substr(Begin, Pos - Begin);
        if (Text == "{")
            ++BraceDepth;
        // Statements outside constructors cannot affect field boundaries.
        // Keep scanning them lexically to distinguish actual braces from
        // literals/comments, but retain only tokens needed by table edits.
        if (BraceDepth)
            Tokens.push_back({Begin, Pos, Text});
        if (Text == "}" && BraceDepth)
            --BraceDepth;
    }
    if (Tokens.empty())
        return Source;
    struct Edit
    {
        std::size_t Begin, End;
        std::string Text;
        bool Before = false;
    };
    struct Table
    {
        std::size_t Open, Close;
        std::vector<std::pair<std::size_t, std::size_t>> Fields;
        bool Function = false;
    };
    std::vector<Edit> Edits;
    std::vector<std::size_t> Stack, TableStack;
    std::vector<Table> Tables;
    for (std::size_t I = 0; I < Tokens.size(); ++I)
    {
        auto Text = Tokens[I].Text;
        if (Text == "function")
            for (auto T : TableStack)
                Tables[T].Function = true;
        if (Text == "{" || Text == "(" || Text == "[")
        {
            Stack.push_back(I);
            if (Text == "{")
            {
                TableStack.push_back(Tables.size());
                Tables.push_back({I, I, {{I + 1, I + 1}}});
            }
        }
        else if ((Text == "," || Text == ";") && !Stack.empty() && Tokens[Stack.back()].Text == "{")
        {
            auto &T = Tables[TableStack.back()];
            T.Fields.back().second = I;
            T.Fields.push_back({I + 1, I + 1});
        }
        else if ((Text == "}" || Text == ")" || Text == "]") && !Stack.empty())
        {
            if (Text == "}" && Tokens[Stack.back()].Text == "{")
            {
                auto &T = Tables[TableStack.back()];
                T.Close = I;
                T.Fields.back().second = I;
                if (T.Fields.back().first == I)
                    T.Fields.pop_back();
                TableStack.pop_back();
            }
            Stack.pop_back();
        }
    }
    auto InlineGap = [&](std::size_t Begin, std::size_t End)
    {
        for (auto I = Begin; I < End; ++I)
            if (Source[I] != ' ' && Source[I] != '\t')
                return false;
        return true;
    };
    for (const auto &T : Tables)
    {
        if (T.Close <= T.Open + 1)
            continue;
        const auto &Open = Tokens[T.Open], &Close = Tokens[T.Close];
        auto Padding = Settings.SpaceInsideTableBraces ? " " : "";
        if (InlineGap(Open.End, Tokens[T.Open + 1].Begin) && Source.compare(Open.End, Tokens[T.Open + 1].Begin - Open.End, Padding) != 0)
            Edits.push_back({Open.End, Tokens[T.Open + 1].Begin, Padding});
        if (InlineGap(Tokens[T.Close - 1].End, Close.Begin) &&
            Source.compare(Tokens[T.Close - 1].End, Close.Begin - Tokens[T.Close - 1].End, Padding) != 0)
            Edits.push_back({Tokens[T.Close - 1].End, Close.Begin, Padding});
        if (!Settings.CompactArrays || T.Function)
            continue;
        auto NumericKey = [&](std::size_t Begin, std::size_t End, unsigned &Index)
        {
            if (End < Begin + 5 || Tokens[Begin].Text != "[" || Tokens[Begin + 2].Text != "]" || Tokens[Begin + 3].Text != "=")
                return false;
            auto Text = Tokens[Begin + 1].Text;
            auto [Last, Error] = std::from_chars(Text.data(), Text.data() + Text.size(), Index);
            return Error == std::errc{} && Last == Text.data() + Text.size();
        };
        std::size_t Prefix = 0;
        for (auto [Begin, End] : T.Fields)
        {
            unsigned Index = 0;
            if (!NumericKey(Begin, End, Index) || Index != Prefix + 1 ||
                Source.substr(Tokens[Begin].Begin, Tokens[Begin + 4].Begin - Tokens[Begin].Begin).find("--") != std::string::npos ||
                Unwrap(Source.substr(Tokens[Begin + 4].Begin, Tokens[End - 1].End - Tokens[Begin + 4].Begin)) == "nil")
                break;
            ++Prefix;
        }
        if (!Prefix)
            continue;
        bool Safe = true;
        // Luau can flush list fields after keyed fields. A later duplicate or
        // unknown key could therefore change the last write to a compacted
        // index. Existing list fields also have their own implicit numbering.
        for (auto [Begin, End] : T.Fields)
        {
            unsigned Index = 0;
            if (NumericKey(Begin, End, Index))
            {
                if (Begin >= T.Fields[Prefix - 1].second && Index >= 1 && Index <= Prefix)
                    Safe = false;
            }
            else if (End >= Begin + 3 && Tokens[Begin + 1].Text == "=" && Identifier(Tokens[Begin].Text))
                continue;
            else if (End >= Begin + 5 && Tokens[Begin].Text == "[" && Tokens[Begin + 2].Text == "]" && Tokens[Begin + 3].Text == "=" &&
                     !Tokens[Begin + 1].Text.empty() && (Tokens[Begin + 1].Text.front() == '\'' || Tokens[Begin + 1].Text.front() == '"'))
                continue;
            else
                Safe = false;
        }
        if (!Safe)
            continue;
        for (std::size_t N = 0; N < Prefix; ++N)
        {
            auto [Begin, End] = T.Fields[N];
            auto ValueBegin = Tokens[Begin + 4].Begin, ValueEnd = Tokens[End - 1].End;
            // An explicit keyed field always consumes one result. A final
            // positional call or vararg must keep that single-result contract.
            bool Single = Tokens[End - 1].Text == ")" || std::string_view(Source).substr(ValueBegin, ValueEnd - ValueBegin) == "...";
            Edits.push_back({Tokens[Begin].Begin, ValueBegin, Single ? "(" : ""});
            if (Single)
                Edits.push_back({ValueEnd, ValueEnd, ")", true});
        }
    }
    if (Edits.empty())
        return Source;
    std::stable_sort(Edits.begin(), Edits.end(),
                     [](const Edit &A, const Edit &B) { return A.Begin != B.Begin ? A.Begin < B.Begin : A.Before > B.Before; });
    std::string Out;
    Out.reserve(Source.size());
    std::size_t Pos = 0;
    for (const auto &E : Edits)
    {
        if (E.Begin < Pos)
            continue;
        Out.append(Source, Pos, E.Begin - Pos);
        Out += E.Text;
        Pos = E.End;
    }
    Out.append(Source, Pos, Source.size() - Pos);
    return Out;
}
} // namespace

Engine::Engine(const Chunk &Bytecode, const Options &Options) : C(Bytecode), Settings(Options), Analysis{Options.MaxAnalysisWork}
{
    // A generated local must never shadow an original global, even when PascalCase changes a debug name.
    for (const auto &P : C.Prototypes)
        for (const auto &I : P.Instructions)
        {
            if (I.Op == LOP_GETGLOBAL || I.Op == LOP_SETGLOBAL)
                GlobalNames.insert(P.Constants[I.Aux].Text);
            if (I.Op == LOP_GETIMPORT)
                GlobalNames.insert(P.Constants[(I.Aux >> 20) & 1023].Text);
        }
    for (const char *Name : {"game", "workspace", "script", "math", "string", "table", "coroutine", "vector", "integer", "getfenv",
                             "setmetatable", "next", "type", "select", "error", "tonumber"})
        GlobalNames.insert(Name);
}
unsigned Engine::New(std::string Name, unsigned Owner, bool Keep, bool Parameter, bool Cell, bool Preserve, bool Debug)
{
    std::string Original = Name;
    if (Name.starts_with("Value") && Name.size() > 5 &&
        std::all_of(Name.begin() + 5, Name.end(), [](unsigned char C) { return std::isdigit(C); }))
        Name = "Value" + std::to_string(++NextValue);
    Name = Preserve && Identifier(Name) ? Name : Pascal(Name);
    unsigned Id = unsigned(Symbols.size());
    // Provisional names need no uniqueness: all internal references use symbol IDs.
    // Only surviving bindings receive collision-free names during finalization.
    Symbols.push_back({Name, Keep, Parameter, Cell, Owner, false, {}, std::move(Original), Preserve, Debug});
    return Id;
}
void Engine::Rename(unsigned Target, unsigned Source)
{
    auto &S = Symbols[Target];
    const auto &From = Symbols[Source];
    S.Name = From.Name;
    S.Original = From.Original;
    S.Preserve = From.Preserve;
    S.Debug = From.Debug;
}
bool Engine::AnalyzeExports() const
{
    return Settings.IncludeLineComments && Settings.IncludeFunctionLocators && Settings.IncludeRequireLocators &&
           !Settings.ModulePath.empty();
}
std::string Engine::Resolve(std::string Text)
{
    auto Paths = AnalyzeExports() ? ExportGraph.Find(MainExportFrame) : std::map<unsigned, std::string>{};
    std::map<unsigned, std::string> Lookups;
    for (const auto &[Proto, Fallback] : Locators)
    {
        auto It = Paths.find(Proto);
        auto Lookup =
            It == Paths.end() ? (Settings.IncludeGcLocators ? Fallback : "") : "require(" + Settings.ModulePath + ")" + It->second;
        Lookups[Proto] = Lookup.empty() ? "" : " | " + Lookup;
    }
    auto References = Tokens(Text);
    std::map<unsigned, std::vector<unsigned>> ByOwner;
    std::vector<bool> Seen(Symbols.size());
    for (auto Id : References)
        if (!Seen.at(Id))
        {
            Seen[Id] = true;
            ByOwner[Symbols[Id].Owner].push_back(Id);
        }
    std::map<unsigned, std::set<std::string>> Used;
    std::set<unsigned> Ready;
    std::function<void(unsigned)> Allocate = [&](unsigned Owner)
    {
        if (!Ready.insert(Owner).second)
            return;
        auto &Taken = Used[Owner];
        Taken = GlobalNames;
        for (auto Parent : Parents[Owner])
        {
            Allocate(Parent);
            Taken.insert(Used[Parent].begin(), Used[Parent].end());
        }
        std::map<std::string, unsigned> Counters;
        for (auto Id : ByOwner[Owner])
            if (Symbols[Id].ImplicitSelf)
                Taken.insert("self");
        for (auto Id : ByOwner[Owner])
        {
            auto &S = Symbols[Id];
            if (S.ImplicitSelf)
            {
                S.Name = "self";
                continue;
            }
            std::string Base = S.Name;
            bool Generic = Base.starts_with("Value") && Base.size() > 5 &&
                           std::all_of(Base.begin() + 5, Base.end(), [](unsigned char C) { return std::isdigit(C); });
            Generic = Generic || (S.Parameter && !S.Debug && Base.starts_with("Argument") && Base.size() > 8 &&
                                  std::all_of(Base.begin() + 8, Base.end(), [](unsigned char C) { return std::isdigit(C); }));
            Generic = Generic || Settings.Naming == VariableNaming::Generic ||
                      (Settings.Naming == VariableNaming::Debug && !S.Debug && !S.Preserve);
            if (Generic)
            {
                const auto Prefix =
                    CaseName(S.Parameter ? Settings.ArgumentPrefix
                                         : (!Settings.UseTypeNames || S.Category.empty() ? Settings.LocalPrefix : S.Category),
                             Settings.Casing);
                do
                {
                    S.Name = Prefix + std::to_string(++Counters[Prefix]);
                } while (Taken.count(S.Name));
            }
            else
            {
                Base = S.Preserve && Settings.PreserveFunctionNames && Identifier(S.Original) ? S.Original
                                                                                              : CaseName(S.Original, Settings.Casing);
                S.Name = Base;
                unsigned Suffix = 2;
                while (Taken.count(S.Name))
                    S.Name = Base + std::to_string(Suffix++);
            }
            Taken.insert(S.Name);
        }
    };
    for (auto &[Owner, Ids] : ByOwner)
        Allocate(Owner);
    // Expand each placeholder once. Never scan or shift the full source per symbol.
    std::string Output;
    Output.reserve(std::min(Text.size(), Settings.MaxOutputBytes));
    auto Append = [&](std::string_view Part)
    {
        if (Part.size() > Settings.MaxOutputBytes - Output.size())
            throw Error("Generated source exceeds output limit");
        Output.append(Part);
    };
    for (std::size_t Pos = 0; Pos < Text.size();)
    {
        auto Next = Text.find_first_of("\x1f\x1d", Pos);
        if (Next == std::string::npos)
        {
            Append(std::string_view(Text).substr(Pos));
            break;
        }
        Append(std::string_view(Text).substr(Pos, Next - Pos));
        bool Symbol = Text[Next] == '\x1f';
        auto End = Text.find(Symbol ? '\x1e' : '\x1c', Next + 1);
        if (End == std::string::npos)
            throw Error("Internal: unterminated output token");
        unsigned Id = 0;
        auto [Last, Status] = std::from_chars(Text.data() + Next + 1, Text.data() + End, Id);
        if (Status != std::errc{} || Last != Text.data() + End)
            throw Error("Internal: invalid output token");
        Append(Symbol ? Symbols.at(Id).Name : Lookups.at(Id));
        Pos = End + 1;
    }
    return FormatTables(std::move(Output), Settings);
}
void Engine::Budget(std::size_t Amount)
{
    Work += Amount;
    if (Work > Settings.MaxOutputBytes * 8)
        throw Error("Source reconstruction exceeds output/work limit");
}

} // namespace Astra::Detail
