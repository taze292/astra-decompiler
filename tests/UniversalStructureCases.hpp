#pragma once

#include <string>
#include <utility>
#include <vector>

inline std::vector<std::pair<std::string, std::string>> UniversalStructureCases()
{
    std::vector<std::pair<std::string, std::string>> Cases = {
        {"universal_empty_infinite_loop", R"(
local function forever()
    while true do end
end
return type(forever)
)"},
        {"universal_numeric_shared_epilogue", R"(
local function run(flag)
    local sum=0
    if flag then
        for i=1,3 do sum+=i end
    else sum=8 end
    return sum+10
end
return run(false),run(true)
)"},
        {"universal_numeric_shared_outer_continue", R"(
local sum=0
for i=1,4 do
    if i%2==0 then
        for j=1,3 do if j==2 then continue end sum+=i*j end
    else sum+=i end
end
return sum
)"},
        {"universal_guard_loop_shared_return", R"(
local function run(a,b)
    local total=0
    if a then
        local i=0
        while i<5 do
            i+=1
            if b and i==3 then return total end
            total+=i
        end
    elseif b then
        for i=1,3 do total+=i end
    else return -1 end
    return total+10
end
return run(false,false),run(false,true),run(true,false),run(true,true)
)"},
        {"universal_nested_shared_empty_return", R"(
local log=''
local function run(a,b,c)
    if a then
        if b then
            if c then return end
            log..='x'
        else log..='y' end
        log..='z'
    else
        if c then return end
        log..='w'
    end
    log..='q'
end
for a=0,1 do
    for b=0,1 do
        for c=0,1 do run(a==1,b==1,c==1); log..='|' end
    end
end
return log
)"},
        {"universal_nested_shared_return_cleanup", R"(
local saved,log={},''
local function run(a,b,c)
    if a then
        local value=3
        saved[#saved+1]=function() return value end
        if b then
            value=5
            if c then return end
            log..='x'
        else value=7 end
        value+=10
    else
        if c then return end
        log..='w'
    end
    log..='q'
end
for a=0,1 do
    for b=0,1 do
        for c=0,1 do run(a==1,b==1,c==1); log..='|' end
    end
end
return log,saved[1](),saved[2](),saved[3](),saved[4]()
)"},
        {"universal_inlined_membership_loop_exit", R"(
local function contains(values,target)
    if values==nil then return true end
    for _,value in values do
        if value==target then return true end
    end
    return false
end
local function run(values,target)
    local log=''
    if contains(values,target) then log..='yes' else log..='no' end
    return log
end
return run(nil,9),run({},9),run({1,2,3},2),run({1,2,3},9)
)"},
        {"universal_predicate_dag_metamethod_order", R"(
local log=''
local meta={__eq=function(a,b) log..=a.name..b.name..',' return a.value==b.value end}
local function object(name,value) return setmetatable({name=name,value=value},meta) end
local function run(a,b,c,d)
    local total=0
    local function touch(n) total+=n end
    if ((a==b or a==c) and d==b) or (a==d and b==c) then
        for i=1,5 do touch(i*3) end
    else
        for i=1,4 do touch(-i) end
    end
    touch(11); touch(12); touch(13); touch(14); touch(15)
    return total
end
local a=object('a',1)
local b=object('b',2)
local c=object('c',1)
local d=object('d',2)
local first=run(a,b,c,d)
local second=run(a,c,b,d)
local third=run(a,b,d,c)
return first,second,third,log
)"},
        {"universal_repeat_or_exit", R"(
local function run(stop)
    local i,total=0,0
    repeat
        i+=1
        if i%2==0 then total+=i else total-=i end
    until i>=7 or stop and total>=2
    return i,total
end
local a,b=run(false)
return a,b,run(true)
)"},
        {"universal_repeat_and_exit", R"(
local function run(stop)
    local i,total=0,0
    repeat
        i+=1
        if i==2 then continue end
        total+=i
    until i>=5 and (not stop or total>=12)
    return i,total
end
local a,b=run(false)
return a,b,run(true)
)"},
        {"universal_repeat_effectful_tail", R"(
local function run(limit)
    local i,total,log=0,0,''
    local function check(label,value) log..=label return value end
    repeat
        i+=1
        if i==2 then continue end
        total+=i
    until check('a',i>=limit) or check('b',i>3) and check('c',total>8)
    return i..':'..total..':'..log
end
return run(3),run(9)
)"},
        {"universal_repeat_nested_tail", R"(
local i,total,log=0,0,''
local function check(label,value) log..=label return value end
repeat
    i+=1
    local j=0
    repeat
        j+=1
        if j==2 then continue end
        total+=i*j
    until check('a',j>=3) and (check('b',i>2) or check('c',j>=5))
    if i==2 then continue end
    total+=10
until check('d',i>=3) or check('e',total>70)
return i,total,log
)"},
        {"universal_while_effectful_condition", R"(
local i,total,log=0,0,''
local function check(label,value) log..=label return value end
while check('a',i<8) and (check('b',total<7) or check('c',i<4)) do
    i+=1
    if i==2 then continue end
    if i%2==0 then total+=i else total+=1 end
end
return i,total,log
)"},
        {"universal_shared_suffix_closure", R"(
local function run(a,b,c)
    local result
    if a then
        if b then result=3 elseif c then return -1 else result=4 end
    elseif b then result=5
    elseif c then result=6
    else return -2 end
    local function add(n) result+=n return result end
    for i=1,3 do add(i) end
    return add(4)
end
return run(false,false,false),run(false,false,true),run(false,true,false),
       run(true,false,false),run(true,false,true),run(true,true,true)
)"},
        {"universal_nested_state_dispatch", R"(
local function run(limit)
    local state,i,total=1,0,0
    while true do
        if state==1 then
            if i>=limit then state=5 else state=2 end
        elseif state==2 then
            i+=1
            if i%2==0 then state=3 else state=4 end
        elseif state==3 then
            for j=1,3 do if j==2 then continue end total+=i*j end
            state=1
        elseif state==4 then
            local j=0
            repeat j+=1; total+=j until j>=2 or total>50
            state=1
        elseif state==5 then return total
        else error('bad state') end
    end
end
return run(0),run(1),run(3),run(5)
)"},
    };
    for (unsigned Length : {16u, 32u})
    {
        std::string Tests, AndTests, MemberTests;
        for (unsigned I = 1; I <= Length; ++I)
        {
            Tests += (I == 1 ? "" : " or ") + std::string("check(") + std::to_string(I) + ",x==" + std::to_string(I) + ")";
            AndTests += (I == 1 ? "" : " and ") + std::string("check(") + std::to_string(I) + ",x~=" + std::to_string(I) + ")";
            MemberTests += (I == 1 ? "" : " or ") + std::string("object[") + std::to_string(I) + "]==x";
        }
        Cases.emplace_back("universal_effectful_or_" + std::to_string(Length),
                           "local log='' local function check(label,value) log..=label..',' return value end "
                           "local function run(x) if " +
                               Tests +
                               " then local sum=0; for i=1,3 do sum+=i end return sum else return -1 end end "
                               "local a,b,c=run(0),run(1),run(" +
                               std::to_string(Length) + ") return a,b,c,log");
        Cases.emplace_back("universal_effectful_and_" + std::to_string(Length),
                           "local log='' local function check(label,value) log..=label..',' return value end "
                           "local function run(x) if " +
                               AndTests +
                               " then local sum=0; for i=1,3 do sum+=i end return sum else return -1 end end "
                               "local a,b,c=run(0),run(1),run(" +
                               std::to_string(Length) + ") return a,b,c,log");
        Cases.emplace_back("universal_member_or_" + std::to_string(Length),
                           "local log='' local object=setmetatable({}, {__index=function(_,key) log..=key..',' return key end}) "
                           "local function run(x) local sum=0; if " +
                               MemberTests +
                               " then for _,v in {2,3,4} do sum+=v end else sum=-1 end return sum end "
                               "local a,b,c=run(0),run(1),run(" +
                               std::to_string(Length) + ") return a,b,c,log");
        Cases.emplace_back("universal_effectful_value_or_" + std::to_string(Length),
                           "local log='' local function check(label,value) log..=label..',' return value and label or false end "
                           "local function run(x) local selected=" +
                               Tests +
                               "; local sum=0; if selected then for i=1,3 do sum+=i end end return selected,sum end "
                               "local a,b=run(0); local c,d=run(1); local e,f=run(" +
                               std::to_string(Length) + ") return a,b,c,d,e,f,log");
        std::string MixedTests, Suffix;
        for (unsigned I = 1; I <= Length; ++I)
        {
            MixedTests += (I == 1 ? "" : " or ") + std::string("((x==") + std::to_string(I) + " or x==" + std::to_string(I + 1) +
                          ") and y==" + std::to_string(I) + ")";
            Suffix += "touch(" + std::to_string(I) + ");";
        }
        Cases.emplace_back("universal_predicate_dag_" + std::to_string(Length),
                           "local function run(x,y) local total=0 local function touch(n) total+=n end if " + MixedTests +
                               " then for i=1,5 do touch(i*3) end else for i=1,4 do touch(-i) end end " + Suffix +
                               " return total end return run(0,0),run(1,1),run(" + std::to_string(Length + 1) + "," +
                               std::to_string(Length) + "),run(7,99)");
    }
    return Cases;
}
