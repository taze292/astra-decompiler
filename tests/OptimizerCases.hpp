#pragma once

#include <utility>

// Keep effect order observable: matching returned numbers alone misses changed
// receiver identities, skipped calls, and metamethod evaluation order.
inline constexpr std::pair<const char *, const char *> OptimizerCases[] = {
    {"optimizer_mutable_field_receiver", R"(
local old, replacement = {}, {}
local target = old
local function change() target = replacement return 7 end
local value = change()
target.field = value
return old.field, replacement.field
)"},
    {"optimizer_mutable_method_receiver", R"(
local old, replacement = {tag="old"}, {tag="new"}
local function method(self, value) return self.tag .. value end
old.method, replacement.method = method, method
local target = old
local function change() target = replacement return "!" end
local value = change()
local result = target:method(value)
return result
)"},
    {"optimizer_shortcircuit_effects", R"(
local log = ""
local function effect() log ..= "E" return true end
local function run(flag)
    local value = effect()
    if flag and value then log ..= "T" else log ..= "F" end
    return log
end
return run(false), run(true)
)"},
    {"optimizer_shortcircuit_values", R"(
local log = ""
local function effect() log ..= "E" return true end
local function run(flag)
    local value = effect()
    return flag and value
end
local a, b = run(false), run(true)
return a, b, log
)"},
    {"optimizer_shortcircuit_metamethod", R"(
local log = ""
local a = setmetatable({}, {__lt=function() log ..= "C" return false end})
local function effect() log ..= "E" return true end
local value = effect()
if a < a and value then log ..= "T" end
return log
)"},
    {"optimizer_shortcircuit_snapshot", R"(
local value = 1
local function mutate() value = 2 return true end
local function run()
    local before = value
    return mutate() and before, value
end
return run()
)"},
    {"optimizer_mutable_return_prefix", R"(
local value = 0
local function change() value = 1 return 7 end
local result = change()
return value, result
)"},
    {"optimizer_method_lookup_order", R"(
local log = ""
local target = setmetatable({}, {__index=function() log ..= "L" return function(_, x) log ..= x end end})
local function argument() log ..= "A" return "V" end
local value = argument()
target:method(value)
return log
)"},
    {"optimizer_operator_effect_order", R"(
local log = ""
local a = setmetatable({}, {__add=function() log ..= "A" return 4 end, __mul=function() log ..= "M" return 6 end})
local function effect() log ..= "E" return 2 end
local value = effect()
local answer = a * a + value
return answer, log
)"},
    {"optimizer_tuple_scalar", R"(
local function values() return 2, 3, 4 end
local function count(...) return select("#", ...), ... end
local value = values()
return count(1, value)
)"},
    {"optimizer_constructor_key_order", R"(
local log = ""
local function step(tag) log ..= tag return tag end
local value = step("V")
local key = step("K")
local target = {}
target[key] = value
return target.K, log
)"},
    {"optimizer_capture_loop_snapshot", R"(
local value = 0
local function increment() value += 1 return value end
local result = ""
for i = 1, 4 do
    local before = value
    local after = increment()
    result ..= before .. ":" .. after .. ";"
end
return result, value
)"},
    {"optimizer_alias_use_counts", R"(
local log = ""
local function effect() log ..= "E" return 2 end
local function sink(x, y) log ..= tostring(x) .. tostring(y) end
local first = effect()
local unused = 0
local alias = first
local argument = 4
sink(alias, argument)
sink(alias)
return log
)"},
    {"optimizer_method_suffix_parentheses", R"(
local value = {}
function value:cross(other) return other end
value.unit = value
local result = value:cross(value.unit):cross(value).unit
return result == value
)"},
    {"optimizer_call_suffix_parentheses", R"(
local function identity(value) return value end
local value = {number=7}
return identity(value).number
)"},
    {"optimizer_callable_suffix_parentheses", R"(
local function identity(value) return value end
local function twice(value) return value * 2 end
return identity(twice)(7)
)"},
};
