# Control-flow and evaluation-order audit

This audit follows `e91ccc2`. Changes stay in local commits; no publication or push is part of the work.

## Process and acceptance gates

1. Preserve baseline binaries before changing reconstruction. Investigate fresh live examples and synthetic equivalents independently in structuring, optimization, and differential validation.
2. Establish a failing or unnecessarily expanded example before each fix. Compare original bytecode execution against recompiled output, including evaluation order, nil positions, and result count. Keep source-compatibility checks separate from behavioral evidence.
3. Recover control flow before simplifying expressions. Keep a conservative, diagnosed fallback where a source loop cannot preserve observable bytecode state.
4. Validate every change across compiler optimization/debug levels, zero/default simplification passes, and explicit state-machine output. Include hand-assembled streams to exercise states that the current compiler normally optimizes away.
5. Recheck the connected game's modules and LocalScripts, compare baseline timings and output sizes, document limits, and create reviewable local commits.

## Confirmed defects and fixes

- **Duplicated boolean branches:** long OR/AND chains reconstructed shared arms repeatedly, exhausting the visit budget and producing a large state machine. Same-target predicates now combine before recursive region construction. Only dead, uncaptured primitive literal loads can move into a condition; calls, property lookups, and metamethod-sensitive work retain their order.
- **Multi-result dispatcher noise:** an open result followed by `NAMECALL` was missed by adjacent-call recognition. The resulting `CallArguments`, `ResultBase` checks, and tuple-copy loops are replaced with a natural final argument when safe. A bounded unique-predecessor analysis also recovers fixed tuple bases through setup instructions and closing upvalues. Where timing requires a snapshot, a packed tuple remains at its original evaluation point and uses an explicitly bounded `table.unpack`.
- **Mutable tuple snapshots:** internal result storage is marked mutable. Fixed result reads cannot move across a later pack replacement. Unused plain pack reads can be removed without retaining every stack register as a local.
- **Changed evaluation order:** simplification could move a call behind a mutable receiver/return-prefix read or into a short-circuit branch that skipped it. Substitution now checks preceding expressions and mutable dependencies. Alias expansion updates use counts so a still-used source definition cannot disappear.
- **Invalid method syntax:** parentheses cleanup mistook call arguments for redundant grouping, turning a nested method call into `receiver:methodargument.field`. Call argument parentheses now survive. This fixed an actual Invisicam compilation failure.
- **Iterator context:** generic iteration in the fallback previously ran inside another coroutine. Canonical loops now use native iteration surrounding a nested dispatcher; yielding, `coroutine.running()`, and protected `__iter` retain the calling context. Noncanonical function iterators use direct calls.
- **Hidden loop state:** native loop reconstruction now checks outside entries and observable control registers. Unusual streams that read a loop's final internal state use explicit reconstruction. `CLOSEUPVALS` liveness tracks actual reference captures instead of every register above its operand.
- **Fallback state:** pending method and open-expression state is cleared before retrying a function as a state machine.
- **Malformed inline guard:** the decoder validates the `CMPPROTO` register before liveness/dataflow use.

The earlier numbered multiline upvalue block is the default again. `upvalue-indexes` defaults to true independently of `compact-upvalues`; both are available through the C++ API, CLI, and global/per-call Luau option tables. See [OPTIONS.md](OPTIONS.md).

## Inline-call recovery

The retained generic call behind `CMPPROTO` is reconstructed in both output modes and now has a dedicated executable regression suite. Static O2 inlining can erase the callsite completely: the tests demonstrate two distinct source forms producing byte-identical chunks even with function names and line information. Taze retains those inlined computations rather than asserting an original call it cannot establish. See [INLINE_RECOVERY.md](INLINE_RECOVERY.md) for evidence and supported cases.

## Validation and limits

Final Release results on 2026-10-15: all six CTest suites passed, with **12,020 checks** in total:

| Suite | Checks |
| --- | ---: |
| RoundTrip | 1,601 |
| Pipeline | 1,659 |
| ControlFlowStress | 8,502 |
| InlineRecovery | 211 |
| WebSocketBridge | 40 |
| BatchCLI | 7 |

The stress suite covers **137 source fixtures plus six raw-bytecode fixtures**, **2,502 configurations**, and zero failures. Its 16 normal-mode function fallbacks comprise eight source-fixture and eight deliberately observable raw-loop-state cases. All six CameraModule source configurations (O0/O1/O2 × debug 0/2) decompile without fallback and recompile successfully.

The `ControlFlowStress` executable runs source fixtures and hand-assembled bytecode through original execution, decompilation, recompilation, and execution again. It compares typed scalar return tuples, including nil slots, and checks deterministic source/diagnostics. A VM interrupt budget bounds nonterminating fixtures. Source tests use O0/O1/O2, debug levels 0/2, and three reconstruction modes. Raw fixtures also vary native/boxed captures and assert an independent expected reference result.

The live audit compiles generated output without executing game modules. It does not establish full gameplay equivalence. Some valid functions still use state-machine output. Noncanonical generalized iteration can retain a coroutine adapter with different yield/identity behavior; a diagnostic identifies that limitation. Custom streams that reuse VM top after consuming a tuple with `SETLIST` remain outside the demonstrated compiler-generated coverage.

Raw-bytecode export through MCP was blocked by automatic approval review. Live validation used the authorized localhost decompiler path, aggregate measurements, and generated-source excerpts; local bytecode fixtures came from the existing authorized corpus or the supplied compiler.

The final connected-client audit on Roblox `0.743.0.7431129` compiled **100/100 scripts**: 65 ModuleScripts and 35 LocalScripts, producing 1,019,084 source bytes. None contained dynamic `if ResultBase` checks. Six functions across four script instances retained fallback: DynamicThumbstick, PathDisplay, and two InventoryController instances sharing the same full name. The bridge remained connected to the final server.

With headers, upvalue comments, and line comments disabled consistently:

| Live module | Before bytes | Final bytes | Before tuple checks | Final tuple checks | Final compiles |
| --- | ---: | ---: | ---: | ---: | --- |
| CameraModule | 30,569 | 23,757 | 30 | 0 | yes |
| ControlModule | 31,322 | 29,181 | 9 | 0 | yes |
| ClickToMoveController | 56,149 | 55,788 | 5 | 0 | yes |
| Invisicam | 28,071 | 27,371 | 5 | 0 | yes; baseline failed |
| BaseCamera | 26,903 | 24,537 | 16 | 0 | yes |

VehicleCamera's intermediate 10,017-byte output with 31 tuple checks became 6,673 bytes with none. A separate current-build pass compiled 17 CameraModule option variants and verified four concurrent identical requests. Eight representative modules/LocalScripts passed eight presentation/reconstruction variants each; both Projectile modules had zero closing capture boxes. All four compact/multiline × numbered/unnumbered combinations compiled. A controlled local fixture also executed in Roblox before and after decompilation with all ten returned values, including nil positions, equal.

CameraModule's 11-call bridge sample measured 11.2547 ms median and 14.0454 ms sampled p95, including transport/executor overhead. This is not a before/after transport-speed comparison.

## Performance

Final in-process comparison against the saved `e91ccc2` executables: five warm-ups and 300 measured samples per input, Release build, headers disabled, deterministic source/diagnostics checked on every sample. Times include decoding, reconstruction, and finalization but exclude transport and disk I/O.

| Input | Baseline median ms | Final median ms | Baseline p95 ms | Final p95 ms | Median reduction |
| --- | ---: | ---: | ---: | ---: | ---: |
| Captured CameraModule, 31 prototypes | 5.0996 | 4.8096 | 6.9776 | 6.0771 | 5.7% |
| Generated corpus, 201 prototypes | 7.7761 | 7.3764 | 10.5217 | 9.6092 | 5.1% |
| Capture-lifetime fixture, 3 prototypes | 0.2628 | 0.2504 | 0.3982 | 0.3895 | 4.7% |
| 20-condition loop fixture | 0.4073 | 0.1865 | 0.6449 | 0.3213 | 54.2% |

The condition fixture shrank from 6,271 to 936 bytes and from one fallback to none. The corpus output is byte-identical. Capture-heavy default output can grow because the requested multiline numbered upvalue comments replace compact lists. Early samples showed a small-fixture slowdown before the final safe scalar-assignment refinement; those measurements remain in the ignored artifacts.

The separate six-configuration source comparison (150 samples each) is mixed: O0/D0 improved from 4.9513 to 4.7868 ms, while O0/D2 changed from 4.8993 to 5.0420 ms and O2/D2 from 5.0625 to 5.2813 ms. The other three variants improved by about 1–3%. This audit does not establish a speedup on every input or configuration.

Final benchmark CSVs and generated comparisons are under `artifacts/controlflow/final-benchmark/`; source-configuration runs are under `artifacts/controlflow/final-source-corpus/`. The final build log and live aggregate report are stored beside them, without adding captured game source or bytecode to Git.

## Reproduction

```powershell
./build.ps1 -Test
./build/Release/taze-control-flow-tests.exe
./build/Release/taze-control-flow-tests.exe open_method
./build/Release/taze-inline-recovery-tests.exe
./tools/BenchmarkCorpus.ps1 -Inputs @('artifacts/rewrite/CameraModule.live.luac', 'artifacts/rewrite/Corpus.luac', 'artifacts/readability/CaptureLifetimes.luac', 'artifacts/controlflow/or20.luac') -BaselineDirectory artifacts/controlflow/baseline -OutputDirectory artifacts/controlflow/final-benchmark -Samples 300
./tools/ValidateCorpus.ps1 -BaselineDirectory artifacts/controlflow/baseline -OutputDirectory artifacts/controlflow/final-source-corpus -Samples 150
```

Artifacts are ignored. A fresh checkout needs locally compiled equivalents and a baseline build from `e91ccc2`. Benchmark defaults differ in the requested upvalue presentation: the baseline uses compact comments; the new default uses numbered multiline comments. Timings compare the user-facing defaults with headers disabled, not identical output text. No universal speed or bug-free claim follows from these tests.
