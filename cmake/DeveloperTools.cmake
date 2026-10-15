# Benchmarks use the public API and remain available without the Luau test VM.
add_executable(astra-bench dev/benchmarks/Benchmark.cpp)
target_link_libraries(astra-bench PRIVATE AstraDecompiler)

add_executable(astra-batch-bench dev/benchmarks/BatchBenchmark.cpp)
target_link_libraries(astra-batch-bench PRIVATE AstraDecompiler)
