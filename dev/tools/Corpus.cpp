// Corpus regression harness: decompile every input, recompile each result
// with the Luau compiler, and compare the recompiled chunk's structure with
// the original. A faithful reconstruction keeps every function's parameter,
// upvalue, and vararg shape and references the same constants.
#include "Astra/Decompiler.hpp"
#include "Bytecode.hpp"
#include "Luau/Compiler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

namespace
{
struct Report
{
    std::string Name;
    std::string Failure;
    std::vector<std::string> Mismatches;
    std::size_t Lines = 0, GenericLocals = 0, LocalClosures = 0, Groupings = 0, Fallbacks = 0;
    std::size_t OriginalInstructions = 0, RecompiledInstructions = 0;
    double Milliseconds = 0;
};

std::string ReadFile(const std::filesystem::path &Path)
{
    std::ifstream In(Path, std::ios::binary);
    if (!In)
        throw std::runtime_error("cannot open " + Path.string());
    return std::string(std::istreambuf_iterator<char>(In), {});
}

bool Digits(std::string_view Text)
{
    return !Text.empty() && std::all_of(Text.begin(), Text.end(), [](unsigned char C) { return std::isdigit(C); });
}

// Count readability signals in generated source, skipping strings and comments.
void Measure(const std::string &Source, Report &R)
{
    std::istringstream Lines(Source);
    std::string Line;
    static const char *Generic[] = {"Value", "Table", "Number", "String", "Boolean", "Function", "Vector", "Integer"};
    while (std::getline(Lines, Line))
    {
        auto First = Line.find_first_not_of(" \t");
        if (First == std::string::npos || Line.compare(First, 2, "--") == 0)
            continue;
        ++R.Lines;
        std::string_view Text(Line);
        Text.remove_prefix(First);
        bool Function = Text.starts_with("local function ");
        if (Text.starts_with("local "))
        {
            auto Names = Text.substr(Function ? 15 : 6);
            for (const char *Prefix : Generic)
                if (Names.starts_with(Prefix))
                {
                    auto End = Names.find_first_not_of("0123456789", std::string_view(Prefix).size());
                    auto Suffix = Names.substr(std::string_view(Prefix).size(), End - std::string_view(Prefix).size());
                    if (Digits(Suffix))
                    {
                        ++R.GenericLocals;
                        if (Function)
                            ++R.LocalClosures;
                    }
                    break;
                }
        }
        for (std::size_t Pos = 0; (Pos = Line.find("((", Pos)) != std::string::npos; Pos += 2)
            ++R.Groupings;
    }
}

std::string ConstantKey(const Astra::Prototype &P, const Astra::Constant &K)
{
    switch (K.Tag)
    {
    case LBC_CONSTANT_STRING:
        return "s:" + K.Text;
    case LBC_CONSTANT_NUMBER:
        return "n:" + Astra::Number(K.Number);
    case LBC_CONSTANT_BOOLEAN:
        return K.Index ? "b:true" : "b:false";
    case LBC_CONSTANT_VECTOR:
    case LBC_CONSTANT_VECTORD:
        return "v:" + Astra::Number(K.Vector[0]) + "," + Astra::Number(K.Vector[1]) + "," + Astra::Number(K.Vector[2]);
    case LBC_CONSTANT_IMPORT:
    {
        std::string Path = "i:";
        for (unsigned J = 0; J < (K.Index >> 30); ++J)
            Path += (J ? "." : "") + P.Constants[(K.Index >> (20 - 10 * J)) & 1023].Text;
        return Path;
    }
    default:
        return {};
    }
}

void Compare(const std::string &Original, const std::string &Recompiled, Report &R)
{
    Astra::Options Settings;
    auto A = Astra::ReadBytecode(Original, Settings);
    auto B = Astra::ReadBytecode(Recompiled, Settings);
    for (const auto &P : A.Prototypes)
        R.OriginalInstructions += P.Instructions.size();
    for (const auto &P : B.Prototypes)
        R.RecompiledInstructions += P.Instructions.size();
    if (A.Prototypes.size() != B.Prototypes.size())
    {
        R.Mismatches.push_back("function count " + std::to_string(A.Prototypes.size()) + " -> " + std::to_string(B.Prototypes.size()));
        return;
    }
    for (std::size_t Id = 0; Id < A.Prototypes.size(); ++Id)
    {
        const auto &X = A.Prototypes[Id];
        const auto &Y = B.Prototypes[Id];
        auto Where = "function " + std::to_string(Id) + (X.Name.empty() ? "" : " " + X.Name) + ": ";
        if (X.Parameters != Y.Parameters || X.Vararg != Y.Vararg)
            R.Mismatches.push_back(Where + "signature");
        if (X.Upvalues != Y.Upvalues)
            R.Mismatches.push_back(Where + "upvalues " + std::to_string(X.Upvalues) + " -> " + std::to_string(Y.Upvalues));
        std::set<std::string> Left, Right;
        for (const auto &K : X.Constants)
            if (auto Key = ConstantKey(X, K); !Key.empty())
                Left.insert(Key);
        for (const auto &K : Y.Constants)
            if (auto Key = ConstantKey(Y, K); !Key.empty())
                Right.insert(Key);
        // Imports are an optimization of global reads; compare their path strings.
        auto Normalize = [](std::set<std::string> &Keys)
        {
            std::set<std::string> Out;
            for (const auto &K : Keys)
            {
                if (K.starts_with("i:"))
                {
                    std::string Path = K.substr(2);
                    for (std::size_t Start = 0;;)
                    {
                        auto Dot = Path.find('.', Start);
                        Out.insert("s:" + Path.substr(Start, Dot - Start));
                        if (Dot == std::string::npos)
                            break;
                        Start = Dot + 1;
                    }
                }
                else
                    Out.insert(K);
            }
            Keys = std::move(Out);
        };
        Normalize(Left);
        Normalize(Right);
        std::vector<std::string> Missing, Extra;
        std::set_difference(Left.begin(), Left.end(), Right.begin(), Right.end(), std::back_inserter(Missing));
        std::set_difference(Right.begin(), Right.end(), Left.begin(), Left.end(), std::back_inserter(Extra));
        if (!Missing.empty() || !Extra.empty())
        {
            std::string Text = Where + "constants";
            for (std::size_t J = 0; J < std::min<std::size_t>(Missing.size(), 4); ++J)
                Text += " -" + Missing[J];
            for (std::size_t J = 0; J < std::min<std::size_t>(Extra.size(), 4); ++J)
                Text += " +" + Extra[J];
            R.Mismatches.push_back(Text);
        }
    }
}
} // namespace

int main(int Argc, char **Argv)
{
    std::vector<std::filesystem::path> Inputs;
    std::filesystem::path OutputDirectory;
    unsigned Jobs = std::max(1u, std::thread::hardware_concurrency());
    int Optimization = 1;
    bool Verbose = false;
    Astra::Options Settings;
    Settings.IncludeHeader = false;
    for (int I = 1; I < Argc; ++I)
    {
        std::string Arg = Argv[I];
        auto Value = [&]() -> std::string
        {
            if (++I == Argc)
                throw std::runtime_error("missing value for " + Arg);
            return Argv[I];
        };
        if (Arg == "--out")
            OutputDirectory = Value();
        else if (Arg == "--jobs")
            Jobs = unsigned(std::stoul(Value()));
        else if (Arg == "--optimization")
            Optimization = std::stoi(Value());
        else if (Arg == "--verbose")
            Verbose = true;
        else if (Arg == "--option")
        {
            auto Pair = Value();
            auto Equal = Pair.find('=');
            if (Equal == std::string::npos)
                throw std::runtime_error("--option expects KEY=VALUE");
            Astra::ApplyOption(Settings, Pair.substr(0, Equal), Pair.substr(Equal + 1));
        }
        else if (std::filesystem::is_directory(Arg))
        {
            for (const auto &Entry : std::filesystem::directory_iterator(Arg))
                if (Entry.is_regular_file())
                    Inputs.push_back(Entry.path());
        }
        else
            Inputs.push_back(Arg);
    }
    if (Inputs.empty())
    {
        std::cerr << "Usage: astra-corpus [--out DIR] [--jobs N] [--optimization 0..2] [--option KEY=VALUE] [--verbose] PATH...\n";
        return 2;
    }
    std::sort(Inputs.begin(), Inputs.end());
    if (!OutputDirectory.empty())
        std::filesystem::create_directories(OutputDirectory);
    std::vector<Report> Reports(Inputs.size());
    std::atomic<std::size_t> Next{0};
    auto Work = [&]
    {
        for (auto Index = Next++; Index < Inputs.size(); Index = Next++)
        {
            auto &R = Reports[Index];
            R.Name = Inputs[Index].filename().string();
            try
            {
                auto Bytes = ReadFile(Inputs[Index]);
                auto Start = std::chrono::steady_clock::now();
                auto Result = Astra::Decompile(Bytes, Settings);
                R.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
                R.Fallbacks = Result.StateMachineFunctions;
                Measure(Result.Source, R);
                if (!OutputDirectory.empty())
                {
                    std::ofstream Out(OutputDirectory / (Inputs[Index].stem().string() + ".luau"), std::ios::binary);
                    Out << Result.Source;
                }
                Luau::CompileOptions Options;
                Options.optimizationLevel = Optimization;
                Options.debugLevel = 1;
                // Roblox folds Vector3.new with constant arguments into vector constants.
                Options.vectorLib = "Vector3";
                Options.vectorCtor = "new";
                Options.vectorType = "Vector3";
                auto Recompiled = Luau::compile(Result.Source, Options);
                if (Recompiled.empty() || Recompiled[0] == 0)
                {
                    R.Failure = "recompile: " + Recompiled.substr(1);
                    continue;
                }
                Compare(Bytes, Recompiled, R);
            }
            catch (const std::exception &Error)
            {
                R.Failure = std::string("decompile: ") + Error.what();
            }
        }
    };
    {
        std::vector<std::jthread> Threads;
        for (unsigned J = 1; J < Jobs; ++J)
            Threads.emplace_back(Work);
        Work();
    }
    std::size_t Failed = 0, Mismatched = 0, Lines = 0, Generic = 0, Closures = 0, Groupings = 0, Fallbacks = 0, Original = 0,
                Recompiled = 0;
    double Time = 0, Slowest = 0;
    std::string SlowestName;
    for (const auto &R : Reports)
    {
        if (!R.Failure.empty())
        {
            ++Failed;
            std::cout << "FAIL " << R.Name << ": " << R.Failure.substr(0, 300) << '\n';
            continue;
        }
        if (!R.Mismatches.empty())
        {
            ++Mismatched;
            std::cout << "DIFF " << R.Name << ": " << R.Mismatches.front();
            if (R.Mismatches.size() > 1)
                std::cout << " (+" << R.Mismatches.size() - 1 << " more)";
            std::cout << '\n';
            if (Verbose)
                for (std::size_t J = 1; J < R.Mismatches.size(); ++J)
                    std::cout << "     " << R.Mismatches[J] << '\n';
        }
        Lines += R.Lines;
        Generic += R.GenericLocals;
        Closures += R.LocalClosures;
        Groupings += R.Groupings;
        Fallbacks += R.Fallbacks;
        Original += R.OriginalInstructions;
        Recompiled += R.RecompiledInstructions;
        Time += R.Milliseconds;
        if (R.Milliseconds > Slowest)
        {
            Slowest = R.Milliseconds;
            SlowestName = R.Name;
        }
    }
    std::printf("\nfiles=%zu failed=%zu structural_diffs=%zu fallbacks=%zu\n", Reports.size(), Failed, Mismatched, Fallbacks);
    std::printf("lines=%zu generic_locals=%zu local_closures=%zu double_parens=%zu\n", Lines, Generic, Closures, Groupings);
    std::printf("instructions original=%zu recompiled=%zu ratio=%.3f\n", Original, Recompiled,
                Original ? double(Recompiled) / double(Original) : 0.0);
    std::printf("decompile_ms total=%.1f slowest=%.1f (%s)\n", Time, Slowest, SlowestName.c_str());
    return Failed ? 1 : 0;
}
