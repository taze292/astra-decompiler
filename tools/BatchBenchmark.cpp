#include "Taze/Decompiler.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int Argc, char **Argv)
{
    try
    {
        if (Argc != 2)
            throw Taze::Error("Usage: taze-batch-bench INPUT.luac");
        std::ifstream File(Argv[1], std::ios::binary);
        if (!File)
            throw Taze::Error("Cannot open input");
        std::string Bytes((std::istreambuf_iterator<char>(File)), {});
        Taze::Options Settings;
        Settings.IncludeHeader = false;
        auto Expected = Taze::Decompile(Bytes, Settings);
        std::vector<std::string_view> Inputs(64, Bytes);
        std::cout << "workers,inputs,samples,median_ms,inputs_per_second\n";
        for (unsigned Workers : {1u, 2u, 4u, 8u})
        {
            Taze::DecompileBatch(Inputs, Settings, Workers);
            std::vector<double> Times;
            for (unsigned I = 0; I < 7; ++I)
            {
                auto Start = std::chrono::steady_clock::now();
                auto Results = Taze::DecompileBatch(Inputs, Settings, Workers);
                auto End = std::chrono::steady_clock::now();
                for (const auto &Result : Results)
                    if (!Result.Succeeded() || Result.Output.Source != Expected.Source || Result.Output.Diagnostics != Expected.Diagnostics)
                        throw Taze::Error("Batch differs from serial output");
                Times.push_back(std::chrono::duration<double, std::milli>(End - Start).count());
            }
            std::sort(Times.begin(), Times.end());
            std::cout << Workers << ",64,7," << Times[3] << ',' << 64000 / Times[3] << '\n';
        }
    }
    catch (const std::exception &Failure)
    {
        std::cerr << Failure.what() << '\n';
        return 1;
    }
}
