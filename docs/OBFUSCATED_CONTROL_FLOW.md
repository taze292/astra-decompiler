# Obfuscated control-flow validation

This record covers the October 15, 2026 fixes for two client-context `Script` instances in `TextChatService`. It describes measured coverage, not a guarantee that arbitrary bytecode can be reconstructed perfectly.

## What caused the expansion

One script used small backward jump islands to forward control to later branches. Astra treated their physical position as a backward control-flow edge and rejected a region that was actually structured. Its automatic dispatcher then expanded that function into a large state machine.

The fix follows bounded chains of pure jumps when a backward target resolves to a forward destination. Existing forward joins keep their identity. Normalizing every edge would destroy lexical joins in other scripts, so the rule deliberately requires this proof.

A separate capture issue boxed a register's entire lifetime because a mutable reference capture could repeat. Unrelated earlier values in that register consequently became table accesses. Astra now preserves the captured binding outside proven open loops, while `CLOSEUPVALS` ends that binding's lifetime. Later register reuse and closed per-iteration captures remain separate. These lifetime constraints also participate in scope placement and optimizer equality.

## Related loop fixes

The expanded audit found preexisting errors in `GetWeaponConfig`, `MatchController`, and `UIController`:

- A successful search jumped past the loop's default result assignment.
- The exhaustion path called `error`, so moving that path before the loop would change behavior.
- An inlined return jumped out of nested numeric and generic loops to a shared continuation.

Astra follows the actual exhaustion path through jump islands. Literal or immutable SSA defaults move only when register identity, reads, writes, captures, and hidden iterator state permit it. Effectful exhaustion paths stay after the loop, guarded by a completion flag. A proven nonlocal exit propagates through enclosing loops with compact flags and native `break`, `continue`, or `return` statements. It does not introduce an instruction dispatcher. Region traversal now diagnoses an edge that would walk beyond its boundary instead of silently emitting unrelated instructions.

## Live game results

The bridge sent bytecode directly to the local server. Validation compiled the returned source with `loadstring`; it did not execute game modules or scripts. All measurements used `AllowFallback = false` and disabled header, line, and upvalue comments.

| Script | Previous output | Updated output | Result |
| --- | ---: | ---: | --- |
| `RT_445ecf3cac76423cbb4f34fe6eba28f7` | about 40,082 lines / 2,556,504 bytes | about 37,301 lines / 2,488,618 bytes | Compiles, no dispatcher |
| `27080b6f02da45bfb6a115dae3e8a3fa` | about 246,834 lines / 11,134,473 bytes | about 57,792 lines / 4,278,502 bytes | Compiles, no dispatcher |

The larger output shrank by approximately 76.6% in lines and 61.6% in bytes. It is still longer than the supplied reference output: temporary assignments, lexical scopes, and capture snapshots remain when Astra cannot safely eliminate them. Line count alone does not establish semantic equivalence.

The whole-game scan includes `ModuleScript`, `LocalScript`, and `Script` with `RunContext.Client`. Earlier scans missed that last category. Of 394 discovered instances, 391 supplied bytecode and all 391 generated compilable source without fallback. Three executor-owned instances returned no bytecode. One unused module had a 37-byte chunk and emitted an empty body in both the preserved baseline and current build. The final scan finished in approximately 5.24 seconds on this machine. This is compile coverage, not behavioral verification of every game script.

Three-run bridge measurements, including transfer and compilation, were mixed: the larger script's final median changed from approximately 1.27 to 0.80 seconds; the RT script changed from approximately 0.58 to 0.62 seconds. An earlier fixed build measured 1.09 and 0.68 seconds respectively. These short samples were taken at different times, not under an isolated performance lab. Recompiled reference benchmarks also showed some regressions. There is no claim of a universal speed improvement from these correctness fixes.

## Regression coverage

The control-flow corpus includes minimized raw bytecode for both outcomes of jump islands, search defaults, effectful exhaustion, explicit normal breaks, and exits through every numeric/generic inner/outer loop combination. Source fixtures compare the execution of shared open captures, fresh closed captures, and register reuse before a later capture. Intentional diagnostic dispatcher tests opt in explicitly.

The bridge harness checks absent global option/configuration tables, partial overrides, invalid values, client-context `Script` inputs, and inaccessible module paths. Rendering tests cover nested upvalue blocks, compact comments, and independent index visibility. CTest also exercises transport, batch processing, pipeline behavior, and compiler-inlined function recovery.

Run the full suite with `./build.ps1 -Test`. See [developer tools](../dev/README.md) for corpus and game-audit commands.

The final Release validation passed **18,908 checks across six suites**, including **4,182 control-flow configurations**. Ordinary source fixtures used no dispatcher. The raw corpus deliberately includes irregular graphs, with 48 diagnostic fallback occurrences in explicitly enabled modes; strict-mode rejection is tested too. All new jump-island and loop-exit regressions require structured output.

Additional checks passed:

- All six CameraModule optimization/debug configurations decompile strictly and recompile.
- Live CameraModule passes 20 output variants, four concurrent requests, and failure recovery.
- Five live targets pass 50 readability/configuration variants.
- The controlled Roblox fixture preserves all ten return values; its generated CameraModule source compiles.
- The CLI, C++ library, and benchmarks build with `ASTRA_BUILD_TESTS=OFF`.
- The documentation generator checks 21 pages and 750 local links and anchors.

## Capture-analysis efficiency

Repeated reference captures can revisit the same open lifetime. A completed traversal is now reused only when its cached definition and the current capture resolve to the same union-find root. Register groups remain disjoint, and closes still stop propagation. One instruction-sized cache is reused across groups instead of allocating a register-by-instruction matrix.

Before/after generated-source comparisons were identical for CameraModule, the general corpus, and both recompiled references. Nine-sample runs in both execution orders showed small, mixed timing differences. A synthetic 200-closure shared-capture fixture reduced charged analysis work from 118,992 to 78,396 units and changed its 21-sample median from 7.98 to 7.61 milliseconds. This supports the bounded-work improvement; it does not establish a speedup for every input. Raw logs remain in ignored `artifacts/obfuscation/`.

## Medal comparison

[Medal](https://github.com/shrimp-nz/medal) is an archived Rust decompiler with separate lifting, SSA, control-flow, and restructuring components. Its useful principles include preserving shared branch tails, refining loops around their exits, and tracking upvalues until a close. Its restructuring implementation also has label/goto recovery; it is not evidence that every arbitrary graph has a natural source representation. Its older instruction coverage and repeated graph-analysis strategy are unsuitable as a direct replacement for Astra's current decoder and bounded analyses.

The comparison informed the audit and directory organization. No Medal code was copied. Relevant primary sources are its [lifter](https://github.com/shrimp-nz/medal/blob/main/luau-lifter/src/lib.rs), [conditional reconstruction](https://github.com/shrimp-nz/medal/blob/main/restructure/src/conditional.rs), [loop reconstruction](https://github.com/shrimp-nz/medal/blob/main/restructure/src/loop.rs), [upvalue SSA](https://github.com/shrimp-nz/medal/blob/main/cfg/src/ssa/upvalues.rs), and [restructuring entry point](https://github.com/shrimp-nz/medal/blob/main/restructure/src/lib.rs).

## Defaults and limits

Automatic state-machine fallback is disabled by default in both C++ and the bridge. Unsupported control flow returns a diagnostic. `AllowFallback = true` remains an explicit compatibility option, and `ForceStateMachine = true` is a diagnostic mode. Malformed inputs and resource-limit failures remain errors.

Omitted options use matching internal defaults. Missing module-path metadata disables require locators; enabled GC locators can still be emitted. This configuration fallback is independent of control-flow fallback.

These changes do not reconstruct stripped source names, comments, or arbitrary compiler-inlined functions without sufficient metadata. Behavioral equivalence is tested on the controlled fixture corpus; live game output is checked for compilation and fallback use.
