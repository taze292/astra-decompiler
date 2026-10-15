# Astra 0.4 validation

Validated on 2026-10-15 with the local Windows/ClangCL Release build and the connected `[FPS] Flick` client. Commits remain local; the repository, Pages settings, and published releases were not changed remotely.

## Local regression suites

`./build.ps1 -Test` passes all six suites, totaling **17,960 checks**:

| Suite | Checks | Result |
| --- | --- | --- |
| RoundTrip | 1,601 | Passed |
| Pipeline | 2,148 | Passed |
| ControlFlowStress | 13,921 across 3,978 configurations | Passed |
| InlineRecovery | 235 | Passed |
| WebSocketBridge | 48 | Passed |
| BatchCLI | 7 | Passed |

The stress suite has zero source-fixture fallbacks. It still exercises 48 fallback invocations for deliberately irregular raw-bytecode configurations; fallback support has not been removed to manufacture a zero count.

New output cases compare execution before and after formatting. They cover ordered arrays, gaps, nils, duplicate/computed keys, existing positional entries, nested constructors, calls/varargs returning multiple values, evaluation order, long strings/comments, and braces inside strings. Bytecode fixtures vary compiler optimization/debug levels and output options. Captured-table tests prevent initializer folding from letting a closure observe an unassigned table binding.

Upvalue checks cover lowercase copy/ref labels, bracketed indexes, independent index disabling, compact comments, and recovered-inline provenance. Spacing checks cover nested tabs, zero/one/two group blank lines, and table padding without changing string contents.

Bridge mocks test all six locator-probe modes plus immutable option snapshots, partial global replacements, per-call layering, invalid fields/types, byte limits, binary/stale/duplicate responses, close events and missing close events, timeouts, reconnects, failed installation, and hook restoration. WebSocket tests reject alias duplicates, reserved PascalCase metadata, and raised host caps. A consistency audit found all 33 public defaults and two reserved metadata fields agree across C++, the bridge, parser aliases, and the reference table.

All six locally compiled CameraModule O0/O1/O2 and debug-0/debug-2 variants decompile with fallback disabled and recompile. The local release packager produces a tag-pinned v0.4.0 loader and matching checksums without changing the tracked development loader.

## Terminal-loop correction

The first connected-game scan found one fallback in an unparented `Client` script: a nested branch followed by an empty infinite loop. Shared-terminal analysis incorrectly collected the loop's self-jump as straight-line cleanup before an unreachable return. Jump instructions are now excluded from that cleanup path.

`TerminalLoopCases.hpp` adds **216 checks**, included in Pipeline above, across nested branches, branch inputs, compiler/debug levels, and simplification passes. A VM interrupt fence verifies both nontermination and the exact preceding side-effect trace without leaving a fixture running forever. Equivalent loop wording is accepted; the tests assert zero state machines and behavior independently.

The original live `Client` now compiles with `AllowFallback=false`.

## Live Roblox checks

The silent Astra v7 bridge was installed against `ws://127.0.0.1:8877/decompile`. Bytecode went directly from the executor to the local server. MCP returned aggregate results and bounded generated-source excerpts. Game modules were compiled, not executed or replaced.

- CameraModule: **20 variants** compile, four concurrent requests match, invalid settings do not leak, and repeated header-free output is deterministic. Median bridge round trip was 13.12 ms in this run; this includes transport and Roblox scheduling.
- Readability: **eight targets × ten configurations** compile, including both Projectile modules, CameraModule, ControlModule, Data, and three LocalScripts. Closing-box noise remains absent in these targets. Live global PascalCase option replacement also passed.
- After the terminal-loop fix, the commentless sweep tested **306 scripts**, compiled all **305** available bytecode outputs, and reported **zero fallbacks**. One unparented `LocalScript` supplied no nonempty bytecode.
- The ordinary-comment sweep tested **328 scripts**, compiled all **327** available bytecode outputs, and reported **zero fallbacks**, with the same empty-bytecode exception. Inventory grows during executor/MCP testing, so these are separate snapshots rather than a fixed corpus.
- The controlled Roblox fixture executes the original and recovered code and compares all **ten returned values**, including nil slots, capture updates, services, and varargs. It passes, and the stored CameraModule output also compiles. The harness is silent and reports through `AstraTestResult`.

Compilation does not establish full behavioral equivalence of game modules. These sweeps describe this connected client and the scripts accessible during testing, not every Roblox game or future bytecode format.

## Performance comparison

Baseline: the pre-Astra working revision `a18b706`, with binaries preserved before editing. Both in-process benchmark variants omit the elapsed header and check deterministic output. Each has five warmups and 301 samples; three runs alternate the before/after order. The table reports the median of those three run medians.

| Local input | Baseline median | Astra median | Change |
| --- | --- | --- | --- |
| Captured CameraModule, 32 functions | 4.5969 ms | 4.8485 ms | 5.5% slower |
| Generated corpus, 201 functions | 8.3119 ms | 8.3730 ms | 0.7% slower |
| ClientLoader guard fixture | 1.0935 ms | 1.0823 ms | 1.0% faster |

The new lexical formatting work has a small measured cost on CameraModule. This update does **not** claim a universal speedup. The finalizer limits retained tokens to table-constructor ranges, avoids unchanged padding edits, and returns unchanged source without rebuilding it. All three benchmark inputs retain zero fallback. Output differences from the baseline are intentional comment/array-format changes; the generated corpus body remains identical.

Local logs and measurements are in ignored `artifacts/astra/`, including `build-test-final.log`, `crossover.csv`, `crossover-summary.csv`, and `source-corpus/`. Reproduce ordinary comparisons with `dev/tools/BenchmarkCorpus.ps1`; run both variant orders when comparing small timing differences.

## Documentation and release checks

The dependency-free site generator builds the README, all Markdown in `docs/`, and third-party notices. It checks every generated local link and heading anchor, preserves fenced source literally, escapes raw HTML, and uses relative URLs for Pages project subpaths. The options page was also inspected in the browser. GitHub Actions configuration and setup instructions are ready; no website has been published.

See [all options](OPTIONS.md), [migration](ASTRA_MIGRATION.md), and [repository rename and Pages setup](GITHUB_PAGES.md).
