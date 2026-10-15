# Validation record

## Current readability/configuration follow-up — 2026-10-15

- All four CTest suites pass: 1,601 core checks, 1,642 pipeline checks, 40 WebSocket checks, and 7 batch CLI checks.
- New behavior cases cover mutable scalar captures, closed-register reuse, per-iteration closures, nested reference propagation, captured-value snapshots, implicit method receivers, and nested receiver scopes. They run at O0/1/2, D0/2, native/boxed settings, and structured/forced-state-machine modes.
- Eight direct-bytecode checks cover mutation and recapture after CLOSEUPVALS without a new register write, preserving the detached value independently of compiler-specific instruction shapes.
- Bridge mocks verify zero installation printing, live replacement of TazeOptions and TazeConfig, per-call isolation, unique recursive lookup, duplicate-descendant explicit paths, duplicate-sibling suppression, escaped names, reconnect/timeout/error behavior, and hook restoration.
- Transport checks cover JSON options, duplicate keys, malformed scalars, reserved keys, trailing data, host limits, the exact two-line header, and independent require/GC flags. Legacy protocols remain covered.
- All six local CameraModule O0–2/D0–2 configurations decompiled with fallback disabled and recompiled successfully. Header-free output remains deterministic; elapsed headers are intentionally variable.

### Live connected-game checks

Bridge v6 was installed against the local server through Roblox MCP after explicit approval for local-server bytecode testing. The silent `TestReadability.luau` harness reported success for **eight targets × eight configurations**, plus eleven timed compiles and a settings-isolation comparison per target:

| Target | Class | Header-free bytes | Closing boxes | GC-only locators |
| --- | --- | --- | --- | --- |
| GunModules.Projectile | ModuleScript | 3,878 | 0 | 3 |
| GunModules.GunFramework.Projectile | ModuleScript | 3,904 | 0 | 3 |
| PlayerModule.CameraModule | ModuleScript | 35,459 | 0 | 32 |
| PlayerModule.ControlModule | ModuleScript | 38,049 | 0 | 42 |
| PlayerScripts.Data | ModuleScript | 1,226 | 0 | 0 |
| PlayerScripts.Load | LocalScript | 450 | 0 | 0 |
| PlayerScripts.InputMangaer | LocalScript | 5,576 | 0 | 8 |
| PlayerScripts.DisableCoreLeaderboard | LocalScript | 115 | 0 | 0 |

Both Projectile modules had four closing-box assignments before the change. Their duplicate names correctly retain explicit paths. CameraModule and ControlModule use identity-checked unique descendant lookups within LocalPlayer. Require-disabled variants contain no require comments and still include GC comments for functions, including exported functions.

The final build repeated all eight targets/configurations successfully. PlayerScripts was absent at that point, so the harness used the corresponding StarterPlayerScripts instances; output sizes and zero-box counts were unchanged. The separate CameraModule harness also passed 17 variants and four concurrent header-free comparisons. The final v6 bridge and rebuilt server were left connected.

These live checks **compile** recovered source; they do not execute or replace game modules and do not prove complete semantic equivalence of third-party scripts. Behavioral equivalence is tested on the local executable fixtures. Results remain in `getgenv().TazeReadabilityValidation`; no live game bytecode was exported through MCP into the repository for this follow-up.

## Earlier local 0.3.0 rewrite — 2026-10-14

This section preserves the earlier architecture rewrite's release/session evidence and historical counts.

- Release build: ClangCL 22.1.3, Windows SDK 10.0.26100; all four CTest suites pass.
- Existing semantic/format/version suite: **1,593 passed, 0 failed**.
- New pipeline suite: **1,433 passed, 0 failed**, covering 576 combinations of casing/naming/type names/function spelling/pass count/debug/state-machine mode, plus locator interactions, strict option errors, resource caps, statistics, serial/parallel ordering and failures, and 512 deterministic byte mutations. Mutated bytecode is never executed.
- WebSocket suite: **30 checks**, retaining legacy/TAZE2 behavior and adding TAZE3 options, bad/duplicate/reserved keys, cap escalation rejection, per-request output limits, and recovery without settings leakage.
- Batch CLI: **7 checks** for ordered mixed success/failure, identical results, overwrite policy, preflight collisions, invalid options, and worker caps.
- Bridge mocks cover all six line-probe modes, cached function restoration, configuration overlay isolation, error inspection/raising, lazy probing, NaN timeouts, pending limits, and timeout recovery.

The four principal benchmark inputs retain byte-identical default output with zero fallback functions. All six compiled variants of the supplied CameraModule source recompile with fallback disabled. Three debug-rich variants have one intentional capture-binding rename (`PropertyName2Capture` → `PropertyNameCapture2`); references and capture comments change consistently. See [performance measurements and reproduction](PERFORMANCE.md).

### Final live MCP validation

- Client: `0.743.0.7431129`, Baseplate.
- Selected path: `game.StarterPlayer.PlayerModule.CameraModule`.
- Bytecode: **13,112 bytes**, version **14**, type metadata **3**.
- Final bridge: **v5**, verified **StartLine** filter key, connected to the locally built server.
- Default source: **28,079 bytes**, **30 GC lookups**, **0 require lookups**; local reconstruction of the captured input has **0 fallback functions**.
- **17** source variants compiled in Roblox, covering every casing/naming pair, disabled comments, zero simplification passes, forced state-machine output, and GC suppression.
- **4** concurrent requests returned byte-identical default source. Invalid request options failed without affecting the next valid request.
- All **30** GC expressions compiled/evaluated without error; **28** matched live functions and **2** anonymous prototypes (lines 609/612) returned no function in this session. Lookup metadata does not guarantee a matching live closure.
- The project behavioral fixture executed before/after decompilation with identical results: `TazeTestResult.Passed = true`.
- Final server restart, reconnection, and bridge reinstallation succeeded. The final local server and bridge were left available for use.

CameraModule was compiled, not executed or substituted for the camera controller. Its 17 compile checks do not establish complete behavior equivalence. Behavioral assertions apply to executable regression fixtures. The live results are in `TazeRewriteValidation`; bytecode, generated source, measurements, and server logs remain ignored under `artifacts/rewrite/`.

The initial architecture-only commit produced identical output before optimizations were introduced. The final rewrite retains its conservative semantic transformations and fallback; the measured gains come from data structures, graph reuse, final source expansion, deferred naming, and independent chunk concurrency. No claim of universal optimality or support for every Luau program is made.

## Historical validation

The final Windows Release build used ClangCL 22.1.3, CMake, and the installed Windows SDK.

`build/Release/taze-tests.exe` reports **1,593 passed, 0 failed**, and CTest passes against the current Luau snapshot. These are individual fixture/configuration, metadata and input-validation checks, not distinct source programs. The suite compares execution results across 55 behavioral fixtures, optimization levels 0–2, stripped/full debug data, structured/fallback emission, versions 9 and 11–14, and plain/Roblox opcode encodings. A dedicated v12/v13 feature-boundary check exercises float and double vector constants rather than changing only the serialized version byte.

New regression fixtures cover chained enum tests, shared branch suffixes, early returns, mixed boolean precedence, false/nil values, side effects in conditions, loop exits, scoped captures, method installation, field/method evaluation order, naming collisions, and parenthesized call statements. These fixtures explicitly reject state-machine fallback in normal mode. The output for the fixtures is also checked for statement semicolons.

The Luau dependency header checksum and supported/unsupported features are documented in `README.md`.

## Live Roblox check

- Client version: `0.737.0.7371584`.
- User-selected input: `game.StarterPlayer.StarterPlayerScripts.PlayerModule.CameraModule` through `getscriptbytecode` (the `LocalPlayer.PlayerScripts` clone was absent in this session).
- Input: 13,117 bytes, bytecode version 13, type metadata version 3.
- The input parsed and decompiled through the newly rebuilt local server.
- Roblox's `loadstring` accepted the generated CameraModule source.

The v13 CameraModule output uses structured output throughout; none requires a state machine. The current bridge output is 28,158 bytes with spacing, `Upvalues:` labels, and **31** inline `StartLine` lookup comments; Roblox compiled it without an error.

The user-supplied, slightly older `CameraModule.luau` comparison source was also compiled at optimization levels 0–2 and debug levels 0/2. All six versions decompiled without fallback and their recovered source compiled successfully. Generated sizes ranged from 24,935 to 26,520 bytes. This source is excluded from Git, and no module-specific source template or replacement is built into the decompiler.

CameraModule's top-level code was not executed, and the running camera controller was not replaced. These CameraModule checks establish parsing and compilation compatibility; they do not prove its entire behavior is equivalent. Behavioral equivalence checks apply to the executable regression fixtures.

The captured bytecode, generated game-module source, and local harness are deliberately excluded from Git under `artifacts/`.

## WebSocket bridge

The Windows integration test starts a temporary loopback server and runs **20 transport checks**, including the RFC handshake hash, CLI/source parity, fragmented requests, invalid hex/bytecode followed by valid requests on the same socket, payloads above 65,535 bytes, simultaneous clients, reconnection, ping/pong, closing frames, missing masks, and oversized frame rejection. Metadata tests cover module export paths, the `StartLine` override, invalid filter keys, and newline rejection in module paths. CTest runs this test alongside the decompiler checks when PowerShell is available.

The executor bridge was installed through the connected Roblox client. Its replacement `decompile` called `getscriptbytecode` on the authorized CameraModule, sent the result to the C++ server, and returned **25,048 bytes**. Roblox's `loadstring` accepted the returned source, which contained no semicolons. The native executor sends a `ws://127.0.0.1:port` Origin header; this is accepted, while browser HTTP(S) origins are rejected.

The live bridge also recovered after the server process was restarted, restored the original executor function through `TazeBridge.Stop()`, and completed two concurrent CameraModule requests with matching response lengths. It was reinstalled and left connected after validation.

## Formatting and function lookups

Ten additional assertions check inline lookup metadata, string escaping (including newlines, quotes, backslashes and NUL), constant imports, exclusion of upvalues, local grouping, return separation, whitespace-free blank lines, comment suppression options, and start-line retention with stripped local names. Extracted lookup expressions are compiled and evaluated against a stub to verify their actual values.

The previous formatting revision returned the 32,734-byte CameraModule source in-game. It compiled successfully, and all **31** emitted `filtergc` expressions compiled and returned live function matches without errors. The `ShouldUseVehicleCamera` lookup was additionally checked for its exact debug name and start line 320. The root prototype has no function declaration comment, so it does not produce a lookup.

## Export lookups, capture cleanup, and line probing

Twelve module fixtures at optimization levels 0–2 add 36 checks: direct exports, nested tables, numeric and quoted keys, constructor/metatable instances, directly returned functions, discarded instances, ambiguous returns, conditional or overwritten fields, escaped tables, and cycles. Positive lookup expressions are executed against the module value and must resolve the expected callable function. Ambiguous cases must retain `filtergc`.

Six mock executor modes exercise the actual bridge source: `Line`, `StartLine`, both keys, ignored keys, missing `filtergc`, and hidden executor closures. The probe requires a matching closure at the correct line and no match at a deliberately wrong line, then defaults to `Line` if neither key is verified. These tests also exercise request metadata, path escaping, LocalScript handling, reconnection, reinstallation, and restoring the original function.

The behavioral fixtures now reject generated immediately invoked capture factories. Existing scoped-capture, recursive-function, reference-mutation, and evaluation-order cases continue to execute equivalently after replacing factories with explicit local snapshots.

The latest live bridge reported `{ FilterLineField = "StartLine", LineProbe = "verified" }`. Its 32,900-byte CameraModule result compiled in Roblox, contained no capture-factory calls, and the `ShouldUseVehicleCamera` lookup returned the expected named function. CameraModule calls its constructor and returns `{}`, so none of its methods is reachable through `require`; retaining GC locators here is intentional. The project behavioral harness was rerun and returned `{ Passed = true, Version = "0.739.0.7390687" }`. The updated server and bridge were left connected.

## BulletHandler and categorized names

In the user's subsequent game session, reinstalling the current bridge enabled the existing export analysis for `game.ReplicatedStorage.ModuleScripts.GunModules.BulletHandler`. The emitted `require(...).Fire` expression was evaluated and compared by identity with the module's exported `Fire` function; they matched. No export-analysis change was needed for this case. The bridge now exposes `Version = 2` and prints its export-lookup mode and detected line field during installation.

Seven additional checks cover stripped table, number, string, boolean, and anonymous function names, mixed-type branch fallback, and collisions with global names. The live BulletHandler output compiled successfully and contained `local Table1 = {}`, `local Table2 = {}`, `function Table1.Fire(Self)` with a `require(...).Fire` locator, and `return Table1`. The exported function was inspected, not called. The updated server and bridge were left running in that session.

## Bridge v3 and named GC lookups

The next live investigation found a version-2 bridge present but a different global decompiler wrapper returning an HTTP/TLS error. Global reassignment did not cover saved references. Version 3 hooks the original executor function and the currently installed wrapper where supported, preserves successful hook targets across reinstallation, and restores prior implementations on stop. The mock harness checks unavailable/rejected hooks, cached native and wrapper calls, repeated installation, restoration, cloned references, and detached module path diagnostics.

Live BulletHandler decompilation through the global function, direct bridge entry point, saved wrapper, and saved native function all emitted `require(...).Fire`, including after repeated installation. A cloned BulletHandler reference retained the same export path; evaluating the locator returned the identical exported function. BulletHandler and CameraModule output compiled. Projectile and ProjectileRender also compiled, each with two recovered export lookups.

GC locators now include original serialized function names when available. The executable metadata test verifies `Options.Name == "Probe"`, and an additional assertion rejects invented names in stripped bytecode. In-game, the CameraModule lookup containing `StartLine = 335, Name = "ShouldUseVehicleCamera"` returned the expected named function. The v3 bridge and updated executable were left running.

## v0.1.1 formatting and reconstruction

The updated Release suite reports **1,592 passed, 0 failed**, plus **20 WebSocket checks**. Named GC locators now omit constants when both the original name and line are available. Other locators limit constants to five. Tests check the exact filters, preserve an original lowerCamel function binding, fold interleaved one-field tables, remove repeated stable aliases, and retain a snapshot when its source changes.

In the connected Archery Duels client, the 64,901-byte old Zone output had 19 `ValueN[1]` references and 59 constants tables in GC comments. The updated preview output compiled at 57,043 bytes, with no `ValueN[1]` references; reference-capture boxes were named `TableN`. Only 16 GC comments needed a constants table. The named `Zone.new` lookup uses its original `new` name and start line. The module has an early return of a different required value, so its local `Zone.new` is not guaranteed to be reachable through `require(Zone)`; the GC locator remains correct. Remaining one-element tables around closed upvalues preserve capture identity.
