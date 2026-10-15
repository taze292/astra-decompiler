# Decompiler pipeline rewrite

## Objective and acceptance gates

Make decompilation predictable, configurable, measurable, and faster without sacrificing the existing reconstruction semantics. Work proceeds in reviewable local commits; nothing is pushed or published during this rewrite. The starting revision is `10a87e8`.

Success means:

- The existing compiler/VM round trips and WebSocket tests continue to pass.
- New naming/comment policies compile and preserve fixture results, including captures, collisions, recursion, and stripped debug information.
- Invalid options, malformed inputs, and resource exhaustion produce bounded failures, not partial source or process crashes.
- A repeatable in-process benchmark compares identical bytecode, settings, compiler configuration, and warm-up/sample counts before and after changes. Report median and p95 latency, output size, fallback count, and throughput. Do not claim every workload improves from one example.
- CameraModule from the connected client decompiles and compiles in Roblox. Compare the supplied source across optimization/debug configurations too. Compilation alone is explicitly not a claim of full camera behavior equivalence.
- Independent concurrent requests produce exactly the same source and diagnostics as serial requests; batch failures remain isolated and ordered.
- Public options, pipeline ownership, bridge protocol, performance methodology, and limitations have current documentation.

## Audit findings

The existing implementation has a substantial semantic regression suite, conservative state-machine fallback, and validation against the bundled Luau compiler and VM. These are assets to preserve. The main architectural problems are:

1. `Decompiler.cpp` combines source utilities, symbol allocation, register dataflow, instruction lowering, structuring, optimization, rendering, and public orchestration in roughly 2,550 lines.
2. Final symbol substitution repeatedly searches and mutates the entire source for every reference, including duplicate references. This scales poorly with source size and symbol count.
3. Per-function dataflow reconstructs successors already computed by control-flow analysis. Program-counter lookups allocate tree nodes, and root-symbol lookups use maps for dense integer domains.
4. Export analysis runs whenever a module path exists, even when its annotations will not be emitted.
5. Casing, naming, and the two function-locator kinds are coupled to fixed policies. The bridge cannot override source options per request, and silent failures have little inspectable information.
6. There is no maintained in-process benchmark or public stage timing. The transport already serves different clients concurrently, but individual callers lack an ordered parallel batch API.

## Target pipeline and ownership

```text
immutable input + validated Options
    -> deserialize and validate instruction stream
    -> control-flow graph, liveness, reaching definitions
    -> instruction and closure lowering into statement nodes
    -> structured regions (or diagnosed fallback)
    -> conservative statement simplification and scope placement
    -> optional export/GC annotations
    -> deterministic naming and bounded single-pass token expansion
    -> source + diagnostics + statistics
```

Each request owns its chunk, analysis, symbols, export graph, and output. There is no mutable global reconstruction cache. Internal headers define stage contracts; public headers expose settings, results, and independent batch work. The Luau checkout remains an untracked reference/build dependency.

Stage boundaries are introduced before changing behavior. Existing transformations are retained and moved into focused translation units: text/expressions, symbols, function analysis/lowering, structuring, optimization, rendering, and pipeline orchestration. This is an incremental rewrite of the architecture and hot paths, not speculative replacement of every proven transformation.

## Milestones and commit boundaries

### 1. Baseline and measurements

- Preserve baseline executables under ignored artifacts.
- Add a reusable benchmark executable with warm-up, median/p95, deterministic-output checks, and CSV output.
- Run the existing test suite, captured CameraModule, and the six compiled source variants. Include a small fixture and a generated larger function corpus.
- Capture current live CameraModule bytes and record client version and selected path.

### 2. Separate reconstruction stages

- Extract internal stage declarations and move existing implementations into focused files.
- Keep default output byte-identical on the benchmark corpus and keep all semantic tests passing.
- Make orchestration small enough to show the actual decompilation sequence.

### 3. Shared configuration and naming

- Centralize strict validation and textual option application for CLI and bridge.
- Add casing policies: PascalCase, camelCase, snake_case, preserve.
- Add naming policies: inferred (current behavior), debug (debug names plus generic fallbacks), generic. Independently control type-based anonymous names and preservation of original function names.
- Add independent GC and require locator switches while retaining the legacy all-locators switch. Preserve the existing line-comment dependency and document it.
- Expose optimization, fallback policy, limits, and statistics with bounded values. Keep conservative defaults.
- Test combinations for valid identifiers, keywords, global/upvalue collisions, literal preservation, deterministic output, and behavior.

### 4. Optimize measured hot paths

- Replace repeated final symbol/locator replacement with bounded append-only expansion.
- Reuse graph successors; use dense program-counter and symbol-root indexing where suitable.
- Skip export and type-name work when settings make it unnecessary.
- Preserve checks for instruction boundaries, AUX/capture words, and malformed jump targets.
- Re-run the baseline benchmark and record improvements or regressions. Keep changes justified by measurements and correctness.

### 5. Bounded parallel batch decompilation

- Parallelize independent chunks, where there is no shared naming/capture state. Do not parallelize recursive closure lowering inside a shared Engine.
- Provide ordered per-input results/errors, an explicit bounded worker count, and a serial path for tiny batches.
- Add CLI batch output with preflight collision checks and per-file failures; retain ordinary single-file behavior.
- Test serial/parallel equality, mixed malformed inputs, repeated runs, resource limits, and throughput. Document aggregate memory as approximately worker count times per-request memory.

### 6. Configurable bridge and diagnostics

- Extend the protocol with a bounded, strict option block and retain legacy requests.
- Support `TazeConfig.Options` plus per-call overrides without restarting the server.
- Preserve silent return behavior by default but expose the most recent failure and request timing; offer an explicit raising mode.
- Avoid path work when require comments are disabled and avoid unnecessary line probing when all lookups are disabled.
- Add protocol validation, request isolation, compatibility, and mock executor checks. Run actual CameraModule through MCP and compile all selected option variants.

### 7. Final validation and documentation

- Run Release CTest, new configuration/batch/stress tests, reproducible benchmarks, the source-variant corpus, and live Roblox checks.
- Review the final diff and update README, option reference, architecture, and validation records with actual results and explicit remaining limits.
- Commit all source/docs/tests locally; keep extracted scripts and outputs ignored. No push or release.

## Design constraints and decisions

- Correct evaluation order, captured lifetimes, tuple arity, and valid Luau take priority over prettier output or microbenchmarks.
- Casing applies to local bindings only. Globals, fields, import names, strings, and serialized names used in GC lookups retain their identity.
- Generic naming changes emitted bindings, not internal symbol identity. Naming occurs after simplification so eliminated temporaries do not consume visible suffixes.
- Annotation-only passes are optional and must not affect executable semantics.
- Parallelism is opt-in and bounded. A small chunk can be slower with thread creation; benchmark both latency and multi-chunk throughput.
- No result cache is initially added: cache eviction, options keys, mutable bytecode, and memory accounting add complexity without baseline evidence that a cache is needed.
- No third-party networking or thread-pool dependency is required.
- Avoid broad algorithm replacement unless evidence identifies a correctness or performance problem. Preserve the existing fallback as an explicit, configurable safety net.
- New bytecode formats are supported only when the bundled Luau definitions and serialized fixtures establish their semantics.

## Verification details

Use the same Release compiler/runtime for baseline and rewritten benchmark binaries. Warm up each input, then sample multiple in-process runs; exclude process launch, disk I/O, and network time from core latency. Measure bridge round-trip latency separately because scheduler/network costs can dominate. Keep CSVs and bytecode under `artifacts/rewrite/` and put summarized numbers in tracked documentation.

Behavior tests compile source, decompile bytecode, compile generated source, and compare VM results. Formatting checks supplement these tests; they do not substitute for them. Invalid/truncated input tests and deterministic mutation stress exercise rejection paths. Test output caps during expansion, option numeric overflow, unknown options, batch error isolation, and no cross-request state leakage.

Live validation uses only the selected CameraModule and project fixtures. Compile CameraModule output without replacing or invoking the running controller. Validate locator matches where the session has live matching functions; report absent matches separately from compile failures.
