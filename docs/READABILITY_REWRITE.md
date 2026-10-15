# Capture reconstruction and live configuration

## Process and acceptance gates

This follow-up to the staged rewrite uses four checkpoints:

1. **Diagnose and preserve a baseline.** Trace physical register reuse, reference captures, and CLOSEUPVALS. Keep the pre-change binaries in ignored `artifacts/readability/baseline/`. Compare actual module output and a reproducible callback fixture.
2. **Change reconstruction with behavioral checks.** Separate capture lifetimes, recover implicit receivers, and simplify expressions only when evaluation order is preserved. Compile, decompile, recompile, and execute fixtures across compiler optimization/debug settings and structured/state-machine modes.
3. **Change configuration and presentation.** Add the timed header, globally replaceable Luau options table, JSON request format, verified module paths, naming controls, and silent bridge installation. Exercise malformed requests, duplicate names, option replacement, and compatibility protocols.
4. **Measure and validate the complete result.** Run all CTest suites, six local CameraModule compiler configurations, eight live scripts/modules with eight presentation variants each, and repeated in-process latency benchmarks. Commit locally; do not push or update published release URLs.

## Why the table boxes appeared

Luau captures a register's current local by reference. The previous reconstruction marked the entire physical register as a permanent one-element table whenever any closure captured it. All definitions in that register were merged, including unrelated earlier temporaries. `CLOSEUPVALS` then copied the table to detach future register writes from the old captured identity. That explains both the `[1]` accesses and `X = { X[1] }` tail assignments. Removing those lines alone would not fix the lifetime model and could break closures.

The new analysis distinguishes native reference locals from captures needing boxes. A native candidate must not be recreated by a loop, and a close must not be followed by writes or effectful uses of that register. A direct return after close is safe; operations that can mutate the detached capture before the register is reused retain boxes. Reaching-definition analysis joins the captured value only with writes reachable while the upvalue is open. Earlier register values remain independent symbols. Nested closures share that lexical symbol, and mutable references prevent unsafe copy propagation. Native captures emit no close operation because Luau itself closes the lexical variable.

Loops, reused closed registers, and state-machine fallback retain explicit boxes and capture-identity snapshots. `native-captures=false` also retains the conservative representation for comparison. This is a correctness boundary, not a guarantee that every possible bytecode program can be rendered without boxes.

## Output improvements

- Direct table installation becomes a colon method when its first parameter is known/inferred as self. The implicit receiver is always lowercase `self`; conversion is suppressed when it could shadow an outer receiver or a global.
- Adjacent expressions can move past stable local/literal reads. Property accesses, calls, mutable captures, completed arithmetic operations, and tuple arity remain ordering barriers where necessary. Arithmetic therefore keeps its evaluation order without retaining every bytecode temporary.
- A leading break guard in `while true` becomes the while condition.
- Constructor and call hints include the class passed to `Instance.new`, event connections, and raycast results. Erased argument names are still generated rather than claimed to be recovered originals.
- Capture comments default to a compact single line. Expanded comments, comment suppression, group spacing, inference switches, naming prefixes, and method syntax are configurable.
- The only default header comments are the requested generator/bytecode line and actual C++ elapsed seconds. Header-free output is deterministic.

## Performance experiments retained

Definition counts and conservative loop membership are computed once per function with copy captures instead of scanning the whole instruction list for every capture. Functions without such captures skip that work. Adjacent substitution rejects non-candidates early and only performs extra ordering analysis when needed.

Simplification stops when a pass makes no statement changes, respecting the configured maximum. This removed the initial regression on the 201-function corpus. Source bodies before/after this convergence optimization were byte-identical on the callback, CameraModule, and generated-function inputs. The full behavioral suite also passed.

Independent-input batch parallelism remains available. Nested prototypes still share request-local capture and naming state, so they are not lowered concurrently. Detailed measurements are in [PERFORMANCE.md](PERFORMANCE.md), and validation scope is in [VALIDATION.md](VALIDATION.md).

## Reproduction

```powershell
./build.ps1 -Test
./build/Release/astra-compile.exe examples/CaptureLifetimes.luau artifacts/captures.luac 1 0
./build/Release/astra.exe artifacts/captures.luac --no-header -o artifacts/captures.luau
./build/Release/astra-bench.exe artifacts/captures.luac 300
./tools/ValidateCorpus.ps1 -OutputDirectory artifacts/readability/source-corpus
```

For live checks, start the newly built server, install local `tools/Decompile.luau`, then execute `tools/TestReadability.luau` and `tools/TestCameraModule.luau`. They store results in `getgenv().AstraReadabilityValidation` and `getgenv().AstraRewriteValidation` without printing. The former discovers the two Projectile modules when present, CameraModule, ControlModule, Data, and up to three LocalScripts under the local player's PlayerScripts (or StarterPlayerScripts when the runtime container is absent). It compiles recovered code without executing game modules and restores the user's configuration afterward.
