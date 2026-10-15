#pragma once

#include <utility>

// Explicit conditional assignments exercise reconstruction of logical values,
// then substitution into receivers and larger expressions. The whole logical
// value must remain grouped even when its first operand has parentheses.
inline constexpr std::pair<const char *, const char *> StatementSyntaxCases[] = {
    {"syntax_fallback_method_statement", R"(
local log=""
local fallback={add=function(_,value) log..="F"..value end}
local provided={add=function(_,value) log..="P"..value end}
local function run(receiver)
    local target=receiver
    if not target then target=fallback end
    target:add(3)
end
run(nil)
run(provided)
return log
)"},
    {"syntax_fallback_function_statement", R"(
local log=""
local function fallback(value) log..="F"..value end
local functions={custom=function(value) log..="C"..value end}
local function run(key)
    local callable=functions[key]
    if not callable then callable=fallback end
    callable(5)
end
run("missing")
run("custom")
return log
)"},
    {"syntax_fallback_function_result", R"(
local function fallback(value) return value+10 end
local function other(value) return value*3 end
local function run(selected)
    local callable=selected
    if not callable then callable=fallback end
    return callable(5)
end
return run(nil),run(other)
)"},
    {"syntax_fallback_member_assignment", R"(
local fallback={value=1}
local provided={value=2}
local function run(receiver,value)
    local target=receiver
    if not target then target=fallback end
    target.value=value
end
run(nil,7)
run(provided,9)
return fallback.value,provided.value
)"},
    {"syntax_fallback_index_assignment", R"(
local fallback={1,2}
local provided={3,4}
local function run(receiver,index,value)
    local target=receiver
    if not target then target=fallback end
    target[index]=value
end
run(nil,2,7)
run(provided,1,9)
return fallback[1],fallback[2],provided[1],provided[2]
)"},
    {"syntax_fallback_member_read", R"(
local fallback={value=11}
local function run(receiver)
    local target=receiver
    if not target then target=fallback end
    return target.value
end
return run(nil),run({value=13})
)"},
    {"syntax_fallback_index_read", R"(
local fallback={17,19}
local function run(receiver,index)
    local target=receiver
    if not target then target=fallback end
    return target[index]
end
return run(nil,2),run({23,29},1)
)"},
    {"syntax_logical_addition_precedence", R"(
local function run(first,second)
    local value=first
    if not value then value=second end
    return 10+value,value*2,3-value
end
local a,b,c=run(nil,7)
return a,b,c,run(4,9)
)"},
    {"syntax_logical_not_precedence", R"(
local function run(first,second)
    local value=first
    if not value then value=second end
    return not value
end
return run(false,true),run(false,false),run(nil,0),run(true,false)
)"},
    {"syntax_logical_condition_precedence", R"(
local function run(a,b,c)
    local value=a
    if not value then value=b end
    if not value and c then return 1 end
    return 2
end
return run(false,true,true),run(false,false,true),run(nil,nil,false),run(true,false,true)
)"},
    {"syntax_and_fallback_callable", R"(
local log=""
local function fallback(value) log..="F"..value end
local function selected(value) log..="S"..value end
local function run(flag)
    local callable=flag
    if callable then callable=selected end
    if not callable then callable=fallback end
    callable(4)
end
run(true)
run(false)
return log
)"},
    {"syntax_fallback_nested_calls", R"(
local log=""
local function fallback(value)
    log..="F"
    return function(other) log..="G" return value+other end
end
local function run(selected)
    local callable=selected
    if not callable then callable=fallback end
    return callable(3)(7)
end
return run(nil),log
)"},
    {"syntax_fallback_call_member_suffix", R"(
local log=""
local function fallback(value) log..="F" return {value=value} end
local function run(selected)
    local callable=selected
    if not callable then callable=fallback end
    return callable(13).value
end
return run(nil),log
)"},
    {"syntax_parenthesized_literal_member", R"(
local function run(flag)
    local value=flag
    if value then value={tag="chosen"} end
    if not value then value={tag="fallback"} end
    return value.tag
end
return run(true),run(false)
)"},
    {"syntax_literal_callable_errors", R"(
local function numberCall() local value=3 value() end
local function stringCall() local value="text" value() end
local function booleanCall() local value=false value() end
local function nilCall() local value=nil value() end
local a,ea=pcall(numberCall)
local b,eb=pcall(stringCall)
local c,ec=pcall(booleanCall)
local d,ed=pcall(nilCall)
return a,type(ea),b,type(eb),c,type(ec),d,type(ed)
)"},
    {"syntax_constructor_callable_error", R"(
local function run()
    local value={}
    value()
end
local ok,err=pcall(run)
return ok,type(err)
)"},
    {"syntax_expression_callable_error", R"(
local function run(input)
    local value=input+2
    value()
end
local ok,err=pcall(run,3)
return ok,type(err)
)"},
    {"syntax_callable_literal_arguments", R"(
local log=""
local function argument() log..="A" return 1 end
local function run()
    local value=3
    value(argument())
end
local ok,err=pcall(run)
return ok,type(err),log
)"},
};
