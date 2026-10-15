# Recovering compiler-inlined calls

Astra preserves an ordinary call when bytecode retains the call's generic fallback. It does not invent a call solely because an arithmetic sequence resembles a function body.

Function bodies reconstructed through a retained `CMPPROTO` fallback use a single provenance comment: `-- Uninlined by Astra`. Their own line/GC/require annotations and upvalue lists are suppressed in both output modes and both upvalue styles. The marker remains when ordinary metadata comments are disabled. An ordinary nested function retains its own normal comment policy. A recovered top-level chunk places the marker in its body; the configurable file header remains separate. The state-machine fallback by itself is not classified as uninlining.

## Guarded runtime inlining

Luau's `CMPPROTO` instruction compares a callable against a runtime prototype id. A match enters a specialized body; a mismatch jumps to an ordinary call. Astra reconstructs the generic fallback, avoiding dependence on runtime-specific prototype ids. This works in structured output and in the state-machine fallback.

This recovery requires valid guarded bytecode with an equivalent ordinary fallback. `CMPPROTO` alone does not prove arbitrary hand-written arms equivalent. Astra follows the compiler/runtime convention; it does not prove the two arms equivalent or reconstruct deleted argument expressions from nothing.

The dedicated `InlineRecovery` test executes real guarded bytecode and recompiles and executes its decompilation. It covers matching original closures, replacement closures, built-in functions, tables with `__call`, forced mismatches, single and multiple results including nil, zero/one/five optimization passes, and both output modes. It checks the original VM result against an explicit expected value as well as the regenerated result.

## Static optimization at level 2

Static inlining can erase the callsite and specialize or remove arguments. The local Luau compiler records `inlineFrames` and debug remarks while compiling, but does not serialize an inline-call map. Relevant source locations in the supplied checkout are:

- `luau-master/luau-master/Compiler/src/Compiler.cpp`: `tryCompileInlinedCall`, `compileInlinedCall`, and `inlineFrames`.
- `luau-master/luau-master/Bytecode/src/BytecodeBuilder.cpp`: `addDebugRemark` and `writeFunction`.
- `luau-master/luau-master/Common/include/Luau/Bytecode.h`: `CMPPROTO` operands and behavior.

For example, these alternatives can produce identical complete bytecode chunks with optimization level 2 and debug level 0 or 1 when written on the same source line:

```luau
local function twice(value) return value * 2 end
local function run(value) return twice(value) + 1 end
return run(...)
```

```luau
local function twice(value) return value * 2 end
local function run(value) return value * 2 + 1 end
return run(...)
```

The ambiguity remains even though the helper prototype survives and debug level 1 preserves line and function-name information. The test suite verifies byte-for-byte equality of the one-line forms using the supplied compiler. Consequently, those fields cannot establish whether a source-level call existed. Debug level 2 may provide additional local ranges, but these are not a general callsite mapping.

Recovering a helper call by matching expressions would be a new refactoring, with additional requirements around callable identity, captures, argument evaluation, live intermediate values, tuples, and metamethod effects. Astra currently retains the equivalent inlined computation when explicit call metadata is absent.
