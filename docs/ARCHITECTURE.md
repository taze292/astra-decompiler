# Decompilation architecture

The public entry point is intentionally short. A request validates options, reads and validates a chunk, reconstructs source, resolves annotations/names, and returns diagnostics and statistics. All mutable analysis state belongs to that request. No process-global cache or shared mutable symbol allocator is used.

| Stage | Implementation | Contract |
| --- | --- | --- |
| Configuration | `Options.cpp` | One strict parser and validator for API, CLI, and bridge |
| Decode/validation | `Bytecode.cpp/.hpp` | Validated prototypes, constants, instructions, and dense PC lookup; AUX/CAPTURE positions remain invalid branch targets |
| Control flow | `ControlFlow.cpp/.hpp` | Successors/predecessors, dominators/postdominators, liveness, blocks, and constant-time dominance queries |
| Register analysis | `Analysis.cpp` | Reaching-definition unions, local lifetimes, capture cells, and optional type-name propagation |
| Instruction lowering | `Lowering.cpp` | Symbolic statement nodes and nested closures, preserving tuple arity and evaluation order |
| Structuring | `Structure.cpp` | Natural loops/branches, bounded region traversal, and explicit state-machine fallback |
| Simplification | `Optimize.cpp` | Conservative statement/branch/table/alias transformations, controlled by pass count |
| Scope/emission | `Render.cpp` | Declaration placement, whitespace, comments, function output, fallback diagnostics |
| Expressions | `Expressions.cpp` | Escaping-aware expression and condition utilities; internal symbol token parsing |
| Names/finalization | `Symbols.cpp` | Deferred deterministic binding names and append-only, output-bounded symbol/locator expansion |
| Export annotations | `Exports.cpp/.hpp` | Optional bounded symbolic export analysis, without executing a module |
| Public orchestration | `Decompiler.cpp` | Stage ordering, result metadata, and wall-clock stage statistics |
| Independent work | `Batch.cpp` | Bounded workers with request-local engines, ordered results, and per-item errors |
| Adapters | `Main.cpp`, `BatchFiles.cpp`, `WebSocketServer.cpp`, `tools/Decompile.luau` | File/transport handling and user configuration |

`Reconstruction.hpp` defines the internal statement/symbol/function contracts. It is not a public API. The statement representation remains structured nodes with symbolic expression strings; this rewrite does not claim a complete typed expression AST. Control characters delimit internal symbol/locator IDs, and user literals are escaped before entering emitted source. Final expansion scans source once and never interprets user code.

## Order and ownership

Each Engine owns its symbols, global-name reservations, parent relationships, export graph, diagnostics, and analysis budget. Function analysis and lowering use a validated immutable Chunk. Nested closures share their request's Engine so captures and enclosing names remain consistent. They are deliberately not lowered on independent threads.

The function's graph is built once and reused for dataflow and export analysis. Loop detection inspects predecessor edges instead of rescanning the remaining instruction stream. Dominator-tree intervals replace repeated ancestor walks. PC lookup and reaching-definition root lookup use contiguous arrays instead of node-allocated maps.

Reference-capture analysis separates native lexical lifetimes from cases requiring boxes. Native capture unions begin at the captured reaching definition, not the register's first use. A forward capture-state analysis tracks which reference registers may still be open at each instruction; closed scopes do not make later register reuse falsely live. Per-iteration locals can use native capture when closing and fresh definitions separate their lifetimes. Repeated captures of an unchanged outer binding share one native local. Detached post-close reads or reinitialized loop bindings without a close retain boxes; dispatch fallback restores register cells. Copy-capture definition counts and backedge intervals are cached per function. Statement simplification stops at convergence. See [capture reconstruction](READABILITY_REWRITE.md) for the earlier invariants and [the control-flow audit](CONTROL_FLOW_AUDIT.md) for the ClientLoader correction.

Local names remain provisional until simplification finishes. Only IDs appear in intermediate expressions. Finalization allocates names for surviving references and expands symbols/locators into a separate bounded output buffer. It avoids global provisional name allocation and repeated whole-source replacement.

## Failure and fallback

Bytecode validation distinguishes executable instructions from AUX and closure-capture words and validates registers, constants, references, versions, and jumps. Resource accounting bounds input/output size, per-function analysis cells, aggregate graph/dataflow/region work, nesting, export traversal, and region duplication. An explicit structuring failure can use the existing state-machine emitter and reports the reason. Resource or malformed-input errors do not silently become successful source. `fallback=false` turns a structuring failure into an error.

Source reconstruction completes before ordinary CLI output is opened. Batch preflight rejects name collisions and aliases. File writes can still fail for ordinary filesystem reasons; batch reports each failure and continues. The implementation does not promise transactional writes across multiple files or cancellation of C++ work after bridge timeout.

## Concurrency

`DecompileBatch` uses an atomic next-input index and at most the configured number of workers. Each worker writes a unique result slot. A joined `jthread` lifetime prevents workers from outliving input views, including when later thread creation throws. There is no shared Luau VM in the decompiler. Compiler/VM test execution remains separate from concurrent decompilation.

The Windows server already handled distinct clients on separate threads; that model is retained. Messages on a single socket remain serial, avoiding response interleaving and unbounded work queues. The Luau wrapper matches request IDs and bounds outstanding calls. For CPU throughput across a known file corpus, use the batch API/CLI.

## Bridge wire protocol

All payloads are text frames. IDs are 1–16 decimal digits. Hex encoding preserves arbitrary bytecode bytes.

```text
RequestId\nTAZE4\nLineField\nHexModulePath\nJsonOptions\nHexBytecode
```

The options object is at most 4,096 bytes and contains scalar string, Boolean, or unsigned integer values. Its restricted JSON grammar accepts ASCII option identifiers/enum words, not escapes, nesting, null, or arbitrary expressions. Duplicate, unknown, reserved, and invalid options fail the request. Metadata paths are at most 8,192 decoded bytes. Resource overrides cannot exceed host caps. The server copies its base Options for each request, so overrides cannot leak into other requests.

Legacy raw, TAZE2 metadata, and TAZE3 hex-encoded semicolon options remain supported. Replies remain `RequestId\nok\nSource` or `RequestId\nerror\nMessage`. Bridge v6 requires this server for TAZE4 requests; it does not silently retry against an old server with lost settings.

## Development workflow

1. Add a minimized behavioral fixture or input-validation regression before changing semantics.
2. Build Release and run CTest, including configuration, transport, and batch checks.
3. For performance changes, compare the same corpus using the baseline and current in-process benchmark binaries.
4. Recompile the six CameraModule source configurations with `tools/ValidateCorpus.ps1`.
5. Install the local bridge and run `tools/TestCameraModule.luau` through Roblox MCP; run the behavioral harness separately.
6. Record actual results and limitations, then commit a coherent local milestone. Publishing is a separate user-directed action.

See [the original rewrite plan](REWRITE_PLAN.md), [configuration](OPTIONS.md), [performance](PERFORMANCE.md), [validation history](VALIDATION.md), and the [whole-game control-flow audit](UNIVERSAL_CONTROL_FLOW.md) for loop/predicate reconstruction proofs and current coverage.
