# Astra 0.5.1 reliability and throughput update

Validated locally on 2026-10-16. The matching public bridge is version 9. Rebuild or use the local package, restart the server, and reinstall the bridge together.

## Reconstruction fixes

The reported `ReplicatedFirst.Load` was found in the connected client under its renamed Instance, `ReplicatedFirst.wa`; its debug lines and constants matched the supplied reference. The original bytecode reproduced a strict reconstruction failure. Recompiling the supplied Volt output alone did not reproduce it, because compilation changes the control-flow layout.

Three general fixes address the captured failures:

- Natural while/repeat loops now handle compiler-inlined early returns that bypass an exhaustion default. The existing liveness, capture, and overwrite proofs determine whether a default can move before the loop or needs an explicit exhaustion guard. Header writes and every latch remain part of that proof.
- An if-branch join cannot use a postdominator reached only through a later loop iteration when that would truncate a current-iteration arm.
- Join inference recognizes jump-threaded loop exits. This prevents repeatedly duplicating React reconciler traversal suffixes until the region budget is exhausted.

All six captured failures strictly decompile and recompile, including Load. No fallback was enabled to obtain these results. Controlled source and bytecode fixtures compare runtime values and effects; live game checks compile output without executing game code.

## Bridge and server changes

The bridge accepts script Instances, raw bytecode strings, and Volt's native `DecompilerOptions` values. Supported native settings are translated; unsupported settings appear in `LastRequest.IgnoredNativeOptions`. Unknown Astra settings still fail validation. Silent-mode failures also return an error as the second result.

Active calls and queued callers have separate bounds. Waiting for admission consumes the same request deadline and does not extract bytecode prematurely. Timeout, disconnect, and Stop wake waiting coroutines. `DecompileMany` preserves input order and returns a result or error for every entry, including cancellation before work begins.

ASTRA5 binary requests avoid hex encoding the bytecode in Lua. The server uses a shared CPU worker pool and permits out-of-order completion with serialized frames. It bounds queued plus running inputs, cleans up partial worker construction, and keeps bookkeeping locks outside blocking socket writes. The bridge resumes on responses instead of polling once per frame. ASTRA4 text mode remains available.

Successful output is cached within configured byte and entry limits. Reuse checks current bytecode, options, locator paths, and line-field metadata. Cache accounting handles concurrent requests for the same target. Locator path compression shares a 512-node search budget; larger hierarchies retain validated explicit paths instead of scanning every descendant repeatedly.

The parser additionally rejects malformed jump destinations, capture kinds, constants, fastcall registers, and class operands before reconstruction. Import validation permits forward constant references accepted by the VM. Debug-local initialization uses indexed writes, and final naming resumes suffix searches instead of restarting them for every colliding name.

## Save behavior

Volt documents asynchronous decompiler jobs scheduled across CPU workers, with saves scheduling many scripts before serialization completes. Astra now applies that scheduling approach to its own bridge. [Volt decompiler guide](https://docs.voltbz.net/docs/decompiler), [saveinstance reference](https://docs.voltbz.net/docs/miscellaneous/saveinstance).

`AstraBridge.DecompileMany(Targets, Timeout, Options)` is the direct bulk API. `PrepareSave(Roots, Timeout, Options)` inventories client-visible scripts and prewarms the cache. Use the same options and input form when the subsequent consumer calls `decompile`; Instance and raw-bytecode cache keys are distinct. The selected working set must fit the cache for prewarming to retain every result. Per-item errors must still be checked.

An isolated live probe of Volt's native `saveinstance` saved three selected roots successfully and made **zero bridge calls**. That native path bypasses the global Lua hook. Its documented options expose a native decompiler-options object, not a callback for replacing the internal backend. These changes accelerate consumers of the hooked `decompile` function; they do not claim to replace Volt's native serializer or its internal decompiler. `PrepareSave` should not be added before a native save that bypasses the hook.

## Measurements and checks

Three cold runs per mode on the same 128 retained script Instances compared bridge v8 with v9, with caching disabled, compact output, and no audit running. These include extraction, transport, and reconstruction. Timing is not an isolated engine benchmark, and the bridge versions were measured consecutively rather than in alternating order.

| Callers | Baseline median | Updated median | Reduction |
| --- | ---: | ---: | ---: |
| 1 | 1.8884 s | 1.5518 s | 17.8% |
| 16 | 0.4556 s | 0.3200 s | 29.8% |

Every measured batch completed without request failures. Output sizes were 1,758,574 and 1,757,914 bytes respectively; the reconstruction changes intentionally affect output. A separate final-package batch using the eight-worker helper took 0.4492 s cold and 0.0087 s warm, with 128 cache hits and identical returned source. These are subset measurements, not whole-game native-save timings.

The 4,096-scope collision fixture retained identical output while measured decode/reconstruction/finalization time dropped from about 2,023 ms to 101 ms; finalization alone changed from about 1,912 ms to 7 ms. This is a deliberately collision-heavy case. The six CameraModule optimization/debug variants all recompiled with no fallback and byte-identical output compared with the baseline. Their small timing changes were mixed; no universal engine speedup is claimed.

All seven CTest suites pass: 1,601 round-trip checks, 2,253 pipeline checks, 15,238 control-flow checks over 4,302 configurations, 235 inline-recovery checks, 134 core-validation checks, 86 WebSocket checks, and nine batch CLI checks. The 48 deliberately irregular raw-bytecode fallback cases remain; ordinary source fixtures use zero fallback. Bridge tests include native options, binary payloads, out-of-order completion, overflow admission, queue expiry, cache invalidation, and Stop wakeups.

Both final live sweeps inventoried 4,435 script Instances: all 4,432 with obtainable bytecode decompiled and compiled, with zero reconstruction failures, output compilation failures, or inserted fallback. The remaining three entries were executor-created ConsoleHandler, its SyntaxHighlighter, and a detached LocalScript without obtainable bytecode. Compact and default-comment configurations both passed. The sweeps ran concurrently and took 51.03 and 55.63 seconds, so those durations are coverage measurements rather than a throughput comparison. The growing inventory differs from the initial 4,405-instance baseline. Running and packaged executables match the final build's SHA-256.

Private game bytecode, generated source, measurement logs, and release packages remain under ignored `artifacts/`. The checked-in regressions use independent synthetic fixtures.
