# Rewrite measurements

Measured locally on 2026-10-14, Windows x64, 12 logical processors, Release/ClangCL 22.1.3, Windows SDK 10.0.26100. Baseline reconstruction is revision `10a87e8`; the identical benchmark harness was added in `0d060dc` before changing the core. Baseline executables are preserved under ignored `artifacts/rewrite/baseline/`.

These are observations on this machine/corpus, not universal speed guarantees. Core timings exclude disk I/O, process startup, Roblox scheduling, and transport. Every sample checks source and diagnostics against the first result. Each input has five warm-up calls and 100 timed calls; median is the upper middle sample and p95 uses nearest rank. No cache is used.

## Core latency

`tools/BenchmarkCorpus.ps1` ran the baseline and rewritten binaries consecutively for each input with identical defaults.

| Input | Bytes / functions | Baseline median | Rewrite median | Baseline p95 | Rewrite p95 | Median reduction |
| --- | --- | --- | --- | --- | --- | --- |
| Captured CameraModule v13 | 13,117 / 32 | 9.3073 ms | 5.2290 ms | 11.6815 ms | 6.0246 ms | 43.8% |
| Current CameraModule v14 | 13,112 / 31 | 9.5450 ms | 5.3260 ms | 11.4119 ms | 6.0820 ms | 44.2% |
| Roblox behavioral fixture | 781 / 5 | 0.4403 ms | 0.3495 ms | 0.5298 ms | 0.5218 ms | 20.6% |
| Generated function corpus | 22,575 / 201 | 25.9084 ms | 8.3208 ms | 28.7053 ms | 9.4041 ms | 67.9% |

All four outputs remain byte-identical to the baseline: 28,003 / 27,929 / 1,608 / 37,375 source bytes respectively, with zero state-machine functions. The generated corpus contains 200 table functions with arithmetic, an if/else, and a return, plus the main prototype. It exercises symbol allocation across many independent functions.

Raw results: `artifacts/rewrite/comparison/summary.csv` and the adjacent before/after CSVs and source outputs.

## Supplied source variants

The local CameraModule source was compiled at optimization levels 0–2 and debug levels 0/2, then decompiled with fallback disabled and recompiled. Each row uses 100 samples from each binary in the same run.

| Compiler configuration | Baseline median | Rewrite median | Speedup | Source bytes |
| --- | --- | --- | --- | --- |
| O0 / D0 | 10.5721 ms | 5.8088 ms | 1.82× | 29,121 |
| O0 / D2 | 11.4866 ms | 6.5634 ms | 1.75× | 29,498 |
| O1 / D0 | 9.8028 ms | 5.8309 ms | 1.68× | 29,017 |
| O1 / D2 | 10.2733 ms | 5.5763 ms | 1.84× | 29,432 |
| O2 / D0 | 9.9810 ms | 5.8244 ms | 1.71× | 29,017 |
| O2 / D2 | 10.4540 ms | 5.8916 ms | 1.77× | 29,432 |

Every output compiles and has zero fallback. Stripped outputs are byte-identical. Each debug-rich output differs only in the consistent renaming of one captured local: `PropertyName2Capture` becomes `PropertyNameCapture2`, including its references/comment. This follows removal of provisional global naming state; final naming now supplies the suffix. The capture remains a snapshot of the same value. Regression fixtures exercise repeated names and per-iteration captures across naming modes.

`ValidateCorpus.ps1` records `identical_source` and preserves both outputs for review; `-RequireIdentical` makes any difference fail during semantics-preserving refactors. Raw results are under `artifacts/rewrite/source-corpus/`.

## Parallel throughput

`taze-batch-bench` decompiled 64 identical v14 CameraModule inputs per batch, with one warm-up batch and seven measured batches at each worker count. All source/diagnostics matched serial decompilation. Times include batch worker creation/joining, but exclude disk I/O.

| Workers | Median batch time | Inputs/second |
| --- | --- | --- |
| 1 | 405.088 ms | 157.990 |
| 2 | 200.372 ms | 319.406 |
| 4 | 119.447 ms | 535.801 |
| 8 | 86.4849 ms | 740.014 |

Eight workers deliver **4.68×** this run's serial throughput. Parallelism is across independent inputs; it does not claim to make one CameraModule request eight times faster. Tiny inputs can lose to thread overhead, and active worker memory scales with worker count. Raw results: `artifacts/rewrite/batch.csv`.

## Memory observations

`MeasureMemory.ps1` launches three separate 100-sample benchmark processes per binary on the 201-function corpus. It polls Windows process peak counters every 10 ms and records the maximum observed counter. These include runtime/executable memory and allocator behavior; they are not isolated per-request heap measurements.

| Median of three process runs | Baseline | Rewrite | Reduction |
| --- | --- | --- | --- |
| Peak working-set bytes | 12,382,208 | 12,038,144 | 2.8% |
| Peak paged bytes | 4,829,184 | 4,263,936 | 11.7% |

Removing duplicate symbol-name storage and provisional name-set allocations reduced footprint in this workload. No memory improvement is claimed for every possible input. Raw runs: `artifacts/rewrite/memory/summary.csv`.

## Live bridge

Final CameraModule validation on Roblox `0.743.0.7431129` returned 28,079 source bytes with StartLine metadata, 30 GC comments, and no require comments. All 17 selected option variants compiled, four concurrent requests returned identical default source, and invalid options were isolated from the next request. Eleven end-to-end calls measured a median of **11.6240 ms** and sampled p95 of **13.0464 ms**. This includes executor scheduling/encoding/transport and is separate from core timings; there is no comparable baseline bridge latency claim.

## Why it improved

- Final symbol and locator expansion appends each span once instead of repeatedly scanning and shifting the whole output.
- Naming happens once for surviving bindings; provisional global uniqueness sets and redundant name strings were removed.
- Dense PC/root-symbol indexing replaces node allocations and tree lookups.
- Dataflow and export analysis reuse the function's graph.
- Loop discovery uses predecessor edges; dominance queries use DFS intervals.
- Export annotations and type-name propagation are skipped when disabled.
- Bounded workers process independent chunks without shared reconstruction state.

## Reproduce

```powershell
./build.ps1 -Test
./tools/BenchmarkCorpus.ps1 -Inputs @('artifacts/CameraModule.luac', 'artifacts/rewrite/CameraModule.live.luac', 'artifacts/RobloxFixture.luac', 'artifacts/rewrite/Corpus.luac') -BaselineDirectory artifacts/rewrite/baseline
./tools/ValidateCorpus.ps1 -BaselineDirectory artifacts/rewrite/baseline -Samples 100
./build/Release/taze-batch-bench.exe artifacts/rewrite/CameraModule.live.luac
./tools/MeasureMemory.ps1 -InputPath artifacts/rewrite/Corpus.luac -BaselineDirectory artifacts/rewrite/baseline
```

Artifacts are intentionally untracked. On a fresh checkout, compile your own fixture inputs, build the baseline benchmark revision in a separate directory, and pass those paths. To reproduce the generated corpus, create `local M = {}`, append functions `M.F1` through `M.F200` with body `local y = x * N; if y > 4 then y += 1 else y -= 1 end; return y`, and finish with `return M.F37(10), M.F123(2), M.F200(0)`. Compile at optimization 1/debug 2. Repeat samples on an otherwise idle machine when comparing small changes.
