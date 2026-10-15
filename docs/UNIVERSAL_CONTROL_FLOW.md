# Whole-game control-flow audit

This audit follows the ClientLoader fixes and uses `bcf0d4a` as its baseline. The live reproduction was `ReplicatedStorage.Packages.Net.Net` in the connected Steal a Brainrot client. Changes target graph patterns, without script-name exceptions or disabling fallback.

## Reconstruction changes

- Discover natural loops from dominance and reverse predecessor closure. Include later latches that jump through an earlier backedge, ignore unreachable predecessor stubs, and distinguish reachable external entries.
- Find numeric and generic loop tails through their actual backedges. Compiler-threaded preparation exits need not immediately follow the physical loop tail. Equivalent terminal returns are also valid exits.
- Normalize loop-exit jump trampolines and retain close-upvalue cleanup. Reconstruct shared terminal blocks beyond a containing branch only after proving that the complete path reaches a return or a known loop exit.
- Preserve all conditional latches. A `continue` that bypasses the last condition cannot be changed into a repeat-loop continuation that evaluates that condition.
- Factor short-circuit setup, including effectful calls and O0 Boolean-materialization diamonds. Forward predicate DAGs share their statement bodies and preserve comparison order, including metamethod effects. Expression expansion and analysis work remain bounded.
- Recover compiler-inlined search loops that skip a default result assignment. A literal default moves before the loop only when it is uncaptured, is not live at body entry, is outside iterator registers, and cannot be overwritten along a path reaching normal exhaustion. Other exits retain their effects or use the conservative fallback.
- Fold duplicated iterator tails only when their operation, registers, result count, and exhaustion destination agree. Native numeric loops reject observed or modified hidden control state; numeric-string coercion remains observable in the fallback.

The renderer removes unreachable terminal suffixes and restores lexical scopes for large SSA outputs. Crossing lifetimes use bounded statement scopes with their escaping values declared at the appropriate common ancestor. This preserves captured identities and evaluation order while avoiding Luau's 200-local limit. A worklist removes unobserved pure stores, including merged stores with multiple definitions. These additional analyses are skipped for small functions.

Expression substitution also groups a complete receiver before member access, indexing, or calling. This fixes logical fallback receivers and literal callees such as `(3)()` without removing their runtime errors.

Condition cleanup removes double negation only when each `not` covers its complete operand. A leading `not` in a larger `and`/`or` expression is not a double-negation pair.

## Validation and interpretation

The initial complete inventory tested 1,192 script instances: 99 inserted fallback functions appeared across 55 outputs, and 18 generated outputs failed compilation. Inventories change as cached scripts load and clients reconnect; subsequent inventories are separate coverage measurements, not identical-input benchmarks.

`Net.Net` initially contained 16 inserted fallback functions and produced 1,514,750 bytes with headers, line comments, and upvalue comments disabled. The later focused check compiled with zero inserted fallback functions and 428,431 bytes. CameraModule, observer utilities, AES modes, and the large Conch parsers are included in the live checks. The original obfuscated script's own dispatch logic is preserved.

After reconnection, the final comment-free sweep on the committed decompiler covered 1,197 instances: all 1,192 obtainable decompilations compiled, with zero inserted fallback functions and no transport retries. Four detached LocalScript entries and `Shared.CustomSnap` had no obtainable bytecode. Server-side reconstruction counters independently showed zero fallback requests. `Shared.Animals` is included in this complete passing sweep.

A second complete sweep with the bridge's normal defaults also passed all 1,192 obtainable scripts with zero fallback, compilation failures, or retries. Headers, line/lookup comments, and numbered multiline upvalue comments were enabled. The compact and default sweeps took 11.28 and 67.05 seconds respectively, including bytecode extraction, path metadata, transport, decompilation, and compilation; these are validation durations, not isolated engine benchmarks.

Final focused requests for `Net.Net`, `Shared.Animals`, and CameraModule also compiled with fallback explicitly disabled. These checks used the same executable SHA-256 as `build/Release/taze.exe`, and the local server remains on that build.

The audit now accepts an empty generated string as valid Luau. `Items.Sarung.SarungScript` contains 37 bytes of bytecode and correctly produces empty source when headers are disabled; the previous audit incorrectly classified that result as an input error. One initial bridge disconnect on `NewShop.UIAnimationsController` passed on retry before the final sweep. The audit records and retries a transport disconnect/send/connection failure at most once per target instead of hiding it.

Final local validation passed all six suites: 1,601 round-trip checks, 1,659 pipeline checks, 13,921 stress checks across 3,978 configurations, 235 inline-recovery checks, 40 WebSocket checks, and seven batch checks. Source fixtures had zero fallback. Forty-eight fallback invocations remained in deliberately irregular raw-bytecode configurations. The 48 generated predicate-DAG fixtures verify destination and exact `__eq` visitation order against an independent graph-walk oracle. All six CameraModule source optimization/debug combinations recompiled with fallback disabled.

Live checks only decompile and compile generated source. They do not execute game modules or prove complete runtime equivalence. Local behavioral tests execute controlled fixtures in the Luau VM across optimization/debug levels, simplification settings, forced fallback, and capture modes. Deliberately irregular bytecode keeps fallback where native source constructs would change behavior.

## Performance

The final comparison uses the same saved inputs against `bcf0d4a`, with headers disabled and five warmups. Four alternating-order runs per build/input contain 300 measured samples each. These are medians of the four run medians, in milliseconds; raw runs and earlier exploratory measurements are in ignored `artifacts/universal-cf/`.

| Input | Baseline | Current | Change |
| --- | ---: | ---: | ---: |
| Captured CameraModule, 31 functions | 4.6867 | 4.8436 | 3.3% slower |
| Generated corpus, 201 functions | 7.7509 | 7.8062 | 0.7% slower |
| Capture-lifetime fixture | 0.2680 | 0.2872 | 7.2% slower |
| Linear 20-condition fixture | 0.1594 | 0.1690 | 6.1% slower |
| Nonlinear 16-group predicate fixture | 0.7223 | 0.3024 | 58.1% faster |

The nonlinear case changes from fallback to structured source and is about 2.4 times as fast. Extra correctness analysis has a measurable cost on some small inputs. Earlier individual runs varied more, including reversed results on the capture fixture; neither a universal speedup nor universal bug freedom is established. The generated corpus, capture fixture, and linear-condition source are byte-identical to their baseline outputs.

## Reproduction

```powershell
./build.ps1 -Test
./build/Release/taze-control-flow-tests.exe universal
./build/Release/taze-control-flow-tests.exe bytecode_threaded
./build/Release/taze-control-flow-tests.exe bytecode_modified
./build/Release/taze-control-flow-tests.exe bytecode_predicate_dag_
./build/Release/taze-control-flow-tests.exe scope_
```

With the local server and bridge installed, execute `tools/AuditControlFlow.luau` through Roblox MCP. It silently inventories ModuleScripts and LocalScripts in the game and `getscripts()`, excluding CoreGui/CorePackages. Inspect `getgenv().TazeControlFlowAudit` for progress, compilation failures, fallback reasons, input errors, empty outputs, and transport retries. Set `TazeControlFlowAuditOptions = {}` to use the bridge's current defaults; otherwise the audit uses a compact comment-free configuration. Set the report's `Cancelled` field to stop after the current request. Generated game source and bytecode are not added to Git.
