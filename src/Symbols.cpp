#include "Reconstruction.hpp"

namespace Taze::Detail
{
Engine::Engine(const Chunk &Bytecode, const Options &Options) : C(Bytecode), Settings(Options)
{
    // A generated local must never shadow an original global, even when PascalCase changes a debug name.
    for (const auto &P : C.Prototypes)
        for (const auto &I : P.Instructions)
        {
            if (I.Op == LOP_GETGLOBAL || I.Op == LOP_SETGLOBAL)
                Names.insert(P.Constants[I.Aux].Text);
            if (I.Op == LOP_GETIMPORT)
                Names.insert(P.Constants[(I.Aux >> 20) & 1023].Text);
        }
    for (const char *Name : {"game", "workspace", "script", "math", "string", "table", "coroutine", "vector", "integer", "getfenv",
                             "setmetatable", "next", "type", "select", "error", "tonumber"})
        Names.insert(Name);
    GlobalNames = Names;
}
unsigned Engine::New(std::string Name, unsigned Owner, bool Keep, bool Parameter, bool Cell, bool Preserve, bool Debug)
{
    std::string Original = Name;
    if (Name.starts_with("Value") && Name.size() > 5 &&
        std::all_of(Name.begin() + 5, Name.end(), [](unsigned char C) { return std::isdigit(C); }))
        Name = "Value" + std::to_string(++NextValue);
    Name = Preserve && Identifier(Name) ? Name : Pascal(Name);
    std::string Base = Name;
    unsigned Suffix = 2;
    while (Names.count(Name))
        Name = Base + std::to_string(Suffix++);
    Names.insert(Name);
    unsigned Id = unsigned(Symbols.size());
    Symbols.push_back({Name, Base, Keep, Parameter, Cell, Owner, false, {}, std::move(Original), Preserve, Debug});
    return Id;
}
void Engine::Rename(unsigned Target, unsigned Source)
{
    auto &S = Symbols[Target];
    const auto &From = Symbols[Source];
    S.Name = From.Name;
    S.Base = From.Base;
    S.Original = From.Original;
    S.Preserve = From.Preserve;
    S.Debug = From.Debug;
}
bool Engine::AnalyzeExports() const
{
    return Settings.IncludeLineComments && Settings.IncludeFunctionLocators && Settings.IncludeRequireLocators && !Settings.ModulePath.empty();
}
std::string Engine::Resolve(std::string Text)
{
    auto Paths = AnalyzeExports() ? ExportGraph.Find(MainExportFrame) : std::map<unsigned, std::string>{};
    for (const auto &[Proto, Fallback] : Locators)
    {
        auto It = Paths.find(Proto);
        auto Lookup = It == Paths.end() ? (Settings.IncludeGcLocators ? Fallback : "") : "require(" + Settings.ModulePath + ")" + It->second;
        if (Lookup.empty())
        {
            Replace(Text, " | \x1d" + std::to_string(Proto) + "\x1c", "");
            continue;
        }
        Replace(Text, "\x1d" + std::to_string(Proto) + "\x1c", Lookup);
    }
    auto References = Tokens(Text);
    std::map<unsigned, std::vector<unsigned>> ByOwner;
    std::set<unsigned> Seen;
    for (auto Id : References)
        if (Seen.insert(Id).second)
            ByOwner[Symbols[Id].Owner].push_back(Id);
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
        {
            auto &S = Symbols[Id];
            std::string Base = S.Base;
            bool Generic = Base.starts_with("Value") && Base.size() > 5 &&
                           std::all_of(Base.begin() + 5, Base.end(), [](unsigned char C) { return std::isdigit(C); });
            Generic = Generic || Settings.Naming == VariableNaming::Generic || (Settings.Naming == VariableNaming::Debug && !S.Debug && !S.Preserve);
            if (Generic)
            {
                const auto Prefix = CaseName(S.Parameter ? "Argument" : (!Settings.UseTypeNames || S.Category.empty() ? "Value" : S.Category), Settings.Casing);
                do
                {
                    S.Name = Prefix + std::to_string(++Counters[Prefix]);
                } while (Taken.count(S.Name));
            }
            else
            {
                Base = S.Preserve && Settings.PreserveFunctionNames && Identifier(S.Original) ? S.Original : CaseName(S.Original, Settings.Casing);
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
    for (auto Id : Tokens(Text))
        Replace(Text, Token(Id), Symbols.at(Id).Name);
    return Text;
}
void Engine::Budget(std::size_t Amount)
{
    Work += Amount;
    if (Work > Settings.MaxOutputBytes * 8)
        throw Error("Source reconstruction exceeds output/work limit");
}

} // namespace Taze::Detail
