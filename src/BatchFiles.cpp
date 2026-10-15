#include "BatchFiles.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <thread>

namespace Taze
{
namespace
{
std::string PathKey(const std::filesystem::path &Path)
{
    auto Key = std::filesystem::weakly_canonical(Path).generic_string();
#ifdef _WIN32
    std::transform(Key.begin(), Key.end(), Key.begin(), [](unsigned char C) { return char(std::tolower(C)); });
#endif
    return Key;
}
}

int DecompileFiles(const std::vector<std::string> &Inputs, const std::string &Directory, const Options &Settings, unsigned Workers, bool Overwrite)
{
    if (Inputs.empty() || Directory.empty()) throw Error("--batch needs inputs and --output-dir");
    if (Workers > 32) throw Error("Worker count must be 0..32");
    if (Workers == 0) Workers = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
    std::vector<std::filesystem::path> Outputs;
    std::set<std::string> InputPaths, OutputPaths;
    for (const auto &Input : Inputs) InputPaths.insert(PathKey(Input));
    for (const auto &Input : Inputs)
    {
        auto Output = std::filesystem::path(Directory) / (std::filesystem::path(Input).stem().string() + ".luau");
        auto Key = PathKey(Output);
        if (!OutputPaths.insert(Key).second) throw Error("Batch output filename collision: " + Output.string());
        if (InputPaths.count(Key)) throw Error("Batch output would overwrite an input: " + Output.string());
        if (std::filesystem::exists(Output))
        {
            if (!Overwrite || !std::filesystem::is_regular_file(Output)) throw Error("Batch output already exists: " + Output.string());
            for (const auto &Other : Inputs)
                if (std::filesystem::exists(Other) && std::filesystem::equivalent(Other, Output))
                    throw Error("Batch output aliases an input: " + Output.string());
        }
        Outputs.push_back(std::move(Output));
    }
    std::filesystem::create_directories(Directory);
    unsigned Failed = 0;
    // Bound retained input/output memory to one worker-sized window.
    for (std::size_t Start = 0; Start < Inputs.size(); Start += Workers)
    {
        auto Count = std::min<std::size_t>(Workers, Inputs.size() - Start);
        std::vector<std::string> Bytes(Count), Failures(Count);
        std::vector<std::string_view> Views(Count);
        for (std::size_t I = 0; I < Count; ++I)
        {
            try
            {
                if (std::filesystem::file_size(Inputs[Start + I]) > Settings.MaxInputBytes) throw Error("Input exceeds configured byte limit");
                std::ifstream File(Inputs[Start + I], std::ios::binary);
                if (!File) throw Error("Cannot open input");
                Bytes[I].assign(std::istreambuf_iterator<char>(File), {});
                Views[I] = Bytes[I];
            }
            catch (const std::exception &Failure) { Failures[I] = Failure.what(); }
        }
        auto Results = DecompileBatch(Views, Settings, Workers);
        for (std::size_t I = 0; I < Count; ++I)
        {
            if (Failures[I].empty()) Failures[I] = Results[I].Failure;
            if (Failures[I].empty())
            {
                std::ofstream Out(Outputs[Start + I], std::ios::binary);
                const auto &Source = Results[I].Output.Source;
                if (!Out || !Out.write(Source.data(), std::streamsize(Source.size()))) Failures[I] = "Cannot write output";
                for (const auto &Diagnostic : Results[I].Output.Diagnostics)
                    std::cerr << Inputs[Start + I] << ": " << Diagnostic << '\n';
            }
            if (!Failures[I].empty()) { ++Failed; std::cerr << Inputs[Start + I] << ": " << Failures[I] << '\n'; }
        }
    }
    std::cerr << "taze: " << Inputs.size() - Failed << " succeeded, " << Failed << " failed\n";
    return Failed ? 1 : 0;
}
} // namespace Taze
