#include "Taze/Decompiler.hpp"
#include <algorithm>
#include <atomic>
#include <thread>

namespace Taze
{
std::vector<BatchResult> DecompileBatch(std::span<const std::string_view> Inputs, const Options &Settings, unsigned Workers)
{
    ValidateOptions(Settings);
    if (Workers > 32)
        throw Error("Worker count must be 0..32");
    if (Workers == 0)
        Workers = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
    Workers = unsigned(std::min<std::size_t>(Workers, Inputs.size()));
    std::vector<BatchResult> Results(Inputs.size());
    std::atomic<std::size_t> Next{0};
    auto Work = [&]
    {
        for (auto I = Next.fetch_add(1, std::memory_order_relaxed); I < Inputs.size(); I = Next.fetch_add(1, std::memory_order_relaxed))
        {
            try
            {
                Results[I].Output = Decompile(Inputs[I], Settings);
            }
            catch (const std::exception &Failure)
            {
                Results[I].Failure = Failure.what();
            }
        }
    };
    if (Workers <= 1)
        Work();
    else
    {
        // jthread joins even if creating a later worker throws.
        std::vector<std::jthread> Threads;
        Threads.reserve(Workers - 1);
        for (unsigned I = 1; I < Workers; ++I)
            Threads.emplace_back(Work);
        Work();
    }
    return Results;
}
} // namespace Taze
