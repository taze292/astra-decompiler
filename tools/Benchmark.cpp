#include "Taze/Decompiler.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <vector>

int main(int Argc, char **Argv)
{
    try
    {
        if (Argc < 2 || Argc > 4)
            throw Taze::Error("Usage: taze-bench INPUT.luac [samples=100] [module-path]");
        std::ifstream File(Argv[1], std::ios::binary);
        if (!File)
            throw Taze::Error("Cannot open benchmark input");
        std::string Bytes((std::istreambuf_iterator<char>(File)), {});
        unsigned Samples = 100;
        if (Argc > 2)
        {
            std::string Text = Argv[2];
            std::size_t Used = 0;
            auto Value = std::stoul(Text, &Used);
            if (Used != Text.size() || Value < 5 || Value > 100000)
                throw Taze::Error("Samples must be 5..100000");
            Samples = unsigned(Value);
        }
        Taze::Options Settings;
        Settings.IncludeHeader = false; // Timing is intentionally variable; compare only generated code.
        if (Argc > 3)
            Settings.ModulePath = Argv[3];
        auto Expected = Taze::Decompile(Bytes, Settings);
        for (unsigned I = 0; I < 5; ++I)
            Taze::Decompile(Bytes, Settings);
        std::vector<double> Times;
        for (unsigned I = 0; I < Samples; ++I)
        {
            auto Start = std::chrono::steady_clock::now();
            auto Actual = Taze::Decompile(Bytes, Settings);
            auto End = std::chrono::steady_clock::now();
            if (Actual.Source != Expected.Source || Actual.Diagnostics != Expected.Diagnostics)
                throw Taze::Error("Nondeterministic source or diagnostics");
            Times.push_back(std::chrono::duration<double, std::milli>(End - Start).count());
        }
        std::sort(Times.begin(), Times.end());
        double Median = Times[Times.size() / 2];
        double P95 = Times[(Times.size() * 95 + 99) / 100 - 1];
        std::cout << "input_bytes,output_bytes,functions,fallbacks,samples,median_ms,p95_ms,inputs_per_second\n"
                  << Bytes.size() << ',' << Expected.Source.size() << ',' << Expected.FunctionCount << ',' << Expected.StateMachineFunctions
                  << ',' << Samples << ',' << std::fixed << std::setprecision(4) << Median << ',' << P95 << ',' << 1000.0 / Median << '\n';
    }
    catch (const std::exception &Error)
    {
        std::cerr << "taze-bench: " << Error.what() << '\n';
        return 1;
    }
}
