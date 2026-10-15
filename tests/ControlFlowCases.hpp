#pragma once

#include <string>
#include <utility>
#include <vector>

inline std::vector<std::pair<std::string, std::string>> ControlFlowCases()
{
    std::vector<std::pair<std::string, std::string>> Cases = {
        {"guard_before_generic_loop", R"(
local function run(a,b)
    local log=''
    if a then if b then log..='A' else return 'B' end else log..='C' end
    for k,v in {2,3,5} do log..=tostring(k*v) end
    return log
end
return run(false,false),run(false,true),run(true,false),run(true,true)
)"},
        {"nested_guard_loop_return", R"(
local function run(a,b,c)
    local log=''
    if a then
        if b then log..='A' elseif c then return 'B' else log..='C' end
    else
        if b then return 'D' elseif c then log..='E' else log..='F' end
    end
    for i=1,3 do if i==2 then continue end log..=tostring(i) end
    return log
end
local out={}
for _,a in {false,true} do for _,b in {false,true} do for _,c in {false,true} do
    out[#out+1]=run(a,b,c)
end end end
return table.concat(out,'|')
)"},
        {"repeat_multiple_latches", R"(
local function run(a,b)
    local n,sum=0,0
    repeat
        n+=1
        if n==2 then continue end
        if a and n==3 then sum+=10; continue end
        if b and n==4 then break end
        sum+=n
    until n>=6
    return n..':'..sum
end
return run(false,false),run(false,true),run(true,false),run(true,true)
)"},
        {"while_branch_nested_loop", R"(
local function run(a)
    local n,sum=0,0
    while n<5 do
        n+=1
        if n==2 or (a and n==3) then continue end
        if n==4 then
            for j=1,3 do if j==2 then continue end sum+=j end
        else
            sum+=n
        end
    end
    return sum
end
return run(false),run(true)
)"},
        {"shared_return_after_loop", R"(
local function run(a,b)
    local n=0
    if a then
        while n<5 do n+=1; if b and n==2 then return n end end
    elseif b then
        for i=1,3 do n+=i end
    else return -1 end
    return n+10
end
return run(false,false),run(false,true),run(true,false),run(true,true)
)"},
        {"repeat_scoped_condition", R"(
local n,sum=0,0
repeat
    n+=1
    local value=n*2
    if n==2 then continue end
    sum+=value
until value>=8
return n,sum
)"},
        {"nested_repeat_continue", R"(
local n,sum=0,0
repeat
    n+=1
    local j=0
    repeat
        j+=1
        if j==2 then continue end
        if n==2 and j==3 then break end
        sum+=n*j
    until j>=4
    if n==2 then continue end
    sum+=10
until n>=3
return n,sum
)"},
        {"loop_condition_effect_order", R"(
local log=''
local function test(x) log..=tostring(x); return x<4 end
local n=0
while test(n) and test(n+1) do
    n+=1
    if n==2 then continue end
    log..='B'
end
repeat
    n+=1
    if n==4 then continue end
    log..='R'
until not test(n) or not test(n+1)
return n,log
)"},
        {"generic_loop_elseif_return", R"(
local function run(a)
    local sum=0
    for k,v in {3,4,5,6} do
        if v==3 then sum+=1 elseif v==4 then
            if a then continue else sum+=2 end
        elseif v==5 then
            if a then return sum else sum+=3 end
        else sum+=4 end
    end
    return sum
end
return run(false),run(true)
)"},
        {"flattened_state_dispatch", R"(
local function run(n)
    local pc,total=1,0
    while true do
        if pc==1 then if n<=0 then pc=4 else pc=2 end
        elseif pc==2 then total+=n; pc=3
        elseif pc==3 then n-=1; pc=1
        elseif pc==4 then return total
        else error('bad state') end
    end
end
return run(0),run(1),run(8)
)"},
        {"iterator_yield", R"(
local run=coroutine.wrap(function()
    local function step(_,last)
        if last==nil then coroutine.yield('yielded'); return 1,'value' end
    end
    for k,v in step do return k,v end
end)
local a=run()
local b,c=run()
return a,b,c
)"},
        {"iterator_coroutine_identity", R"(
local run=coroutine.wrap(function()
    local own=coroutine.running()
    local function step(_,last)
        if last==nil then return 1,coroutine.running()==own end
    end
    for k,v in step do return k,v end
end)
return run()
)"},
        {"generalized_iterator_yield", R"(
local run=coroutine.wrap(function()
    local object=setmetatable({}, {__metatable='locked', __iter=function()
        local function step(_,last)
            if last==nil then coroutine.yield('yielded'); return 1,'value' end
        end
        return step,nil,nil
    end})
    for k,v in object do return k,v end
end)
local a=run()
local b,c=run()
return a,b,c
)"},
        {"generalized_iterator_coroutine_identity", R"(
local run=coroutine.wrap(function()
    local own=coroutine.running()
    local object=setmetatable({}, {__metatable='locked', __iter=function()
        local function step(_,last)
            if last==nil then return 1,coroutine.running()==own end
        end
        return step,nil,nil
    end})
    for k,v in object do return k,v end
end)
return run()
)"},
        {"generalized_nested_dispatch", R"(
local function objects()
    return setmetatable({}, {__iter=function() return next,{2,3,5},nil end})
end
local sum=0
for k,v in objects() do
    if k==2 then continue end
    for j,w in objects() do
        if j==3 then break end
        sum+=v*w
    end
end
return sum
)"},
    };
    for (unsigned Length : {20u, 80u})
    {
        std::string Or, And;
        for (unsigned I = 1; I <= Length; ++I)
        {
            Or += (I == 1 ? "" : " or ") + std::string("x==") + std::to_string(I);
            And += (I == 1 ? "" : " and ") + std::string("x~=") + std::to_string(I);
        }
        Cases.emplace_back("long_or_loop_" + std::to_string(Length),
                           "local function run(x) if " + Or +
                               " then local sum=0; for i=1,3 do sum+=i end return sum else return 0 end end "
                               "return run(0),run(1),run(" +
                               std::to_string(Length) + "),run(" + std::to_string(Length + 1) + ")");
        Cases.emplace_back("long_and_loop_" + std::to_string(Length), "local function run(x) local sum=0; if " + And +
                                                                          " then for _,v in {2,3,4} do sum+=v end end return sum end "
                                                                          "return run(0),run(1),run(" +
                                                                          std::to_string(Length) + "),run(" + std::to_string(Length + 1) +
                                                                          ")");
        Cases.emplace_back("long_or_continue_" + std::to_string(Length), "local sum=0; for x=0," + std::to_string(Length + 2) + " do if " +
                                                                             Or + " then continue end sum+=x end return sum");
    }
    return Cases;
}
