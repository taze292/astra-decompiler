#include "ControlFlowCases.hpp"
#include "Luau/BytecodeBuilder.h"
#include "Luau/Compiler.h"
#include "OptimizerCases.hpp"
#include "Taze/Decompiler.hpp"
#include "lua.h"
#include "lualib.h"

#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
struct Fixture
{
    std::string Name;
    std::string Source;
    bool Serialized = false;
};

unsigned Checks = 0;

void Check(bool Success, const std::string &Message)
{
    if (!Success)
        throw std::runtime_error(Message);
    ++Checks;
}

std::string Compile(const std::string &Source, int Optimization, int Debug)
{
    Luau::CompileOptions Options;
    Options.optimizationLevel = Optimization;
    Options.debugLevel = Debug;
    auto Bytes = Luau::compile(Source, Options);
    if (Bytes.empty() || Bytes.front() == 0)
        throw std::runtime_error("Compilation failed: " + Bytes.substr(Bytes.empty() ? 0 : 1));
    ++Checks;
    return Bytes;
}

std::string Execute(const std::string &Bytes)
{
    std::unique_ptr<lua_State, decltype(&lua_close)> State(luaL_newstate(), lua_close);
    if (!State)
        throw std::runtime_error("Unable to allocate Luau state");
    luaL_openlibs(State.get());
    unsigned Budget = 50000;
    lua_callbacks(State.get())->userdata = &Budget;
    lua_callbacks(State.get())->interrupt = [](lua_State *L, int Gc)
    {
        if (Gc < 0 && --*static_cast<unsigned *>(lua_callbacks(L)->userdata) == 0)
            luaL_error(L, "Control-flow fixture exceeded its execution budget");
    };
    if (luau_load(State.get(), "control-flow-stress", Bytes.data(), Bytes.size(), 0) || lua_pcall(State.get(), 0, LUA_MULTRET, 0))
        throw std::runtime_error(lua_tostring(State.get(), -1));
    std::string Result;
    for (int Index = 1; Index <= lua_gettop(State.get()); ++Index)
    {
        Result += std::to_string(lua_type(State.get(), Index)) + ':';
        if (lua_isboolean(State.get(), Index))
            Result += lua_toboolean(State.get(), Index) ? "true" : "false";
        else if (!lua_isnil(State.get(), Index))
        {
            if (!lua_isnumber(State.get(), Index) && !lua_isstring(State.get(), Index))
                throw std::runtime_error("Fixture returned a non-scalar value");
            std::size_t Size = 0;
            const char *Text = lua_tolstring(State.get(), Index, &Size);
            Result += std::to_string(Size) + ':' + std::string(Text, Size);
        }
        Result += ';';
    }
    return Result;
}

std::vector<Fixture> Fixtures()
{
    std::vector<Fixture> Result = {
        {"short-circuit-effects", R"(
local log = ""
local function mark(label, value) log ..= label return value end
local a = mark("a", false) and mark("b", 7) or mark("c", 0)
local b = mark("d", nil) or mark("e", "") and mark("f", false)
local c = mark("g", 0) and mark("h", "") or mark("i", 4)
return a, b, c, log
)"},
        {"truthiness-matrix", R"(
local values = {false, 0, "", 7, "x", {}}
local log = ""
local function f(a,b,c)
    local x = a and b or c
    local y = not a or b and c
    if a and (b or not c) then log ..= "a" else log ..= "b" end
    if (a or b) and (not b or c) then log ..= "c" end
    return tostring(x), tostring(y)
end
for i=1,5 do for j=1,5 do local x,y=f(values[i],values[j],nil); log ..= x .. y end end
return log
)"},
        {"diamond-shared-tail", R"(
local function f(a,b,c)
    local x = 0
    if a then
        if b then x=1 elseif c then x=2 else x=3 end
    elseif b then x=4 else x=5 end
    if x % 2 == 0 then x += 10 else x -= 2 end
    return x
end
return f(true,true,false),f(true,false,true),f(true,false,false),f(false,true,true),f(false,false,false)
)"},
        {"nested-early-return", R"(
local function f(x,y)
    if x < 0 then if y then return 1,nil,3 end return 2 end
    if x == 0 then return nil,false end
    if y then if x > 3 then return 4 end else return 5,6 end
    return 7
end
return select('#',f(-1,true)),f(-1,false),f(0,true),f(1,false),f(2,true),f(5,true)
)"},
        {"while-continue-break", R"(
local i,total,log = 0,0,""
while i < 12 do
    i += 1
    if i % 2 == 0 then log ..= "c" continue end
    if i > 8 then break end
    if i < 4 then total += i else total -= i end
    log ..= tostring(i)
end
return i,total,log
)"},
        {"while-short-circuit-condition", R"(
local calls,i,total=0,0,0
local function nextValue() calls+=1 return calls<8 end
while nextValue() and (i<5 or total<3) do
    i+=1
    if i%2==0 then continue end
    total+=i
end
return calls,i,total
)"},
        {"repeat-local-condition", R"(
local i,total = 0,0
repeat
    i += 1
    local stop = i >= 5
    if i % 2 == 0 then total += 10 else total += i end
until stop or total > 20
return i,total
)"},
        {"repeat-continue-break", R"(
local i,total=0,0
repeat
    i+=1
    if i==2 then continue end
    if i==6 then break end
    total+=i
until i>=8
return i,total
)"},
        {"nested-loop-exits", R"(
local total,log=0,""
for i=1,5 do
    if i==2 then continue end
    local j=0
    while j<5 do
        j+=1
        if j==2 then continue end
        if j==4 and i==3 then break end
        total+=i*j
        log..=i..":"..j..";"
    end
    if i==4 then break end
end
return total,log
)"},
        {"numeric-for-dynamic-step", R"(
local calls=0
local function value(x) calls+=1 return x end
local sum=0
for i=value(7),value(-3),value(-2) do
    if i==3 then continue end
    sum+=i
end
return sum,calls
)"},
        {"table-alias-branch-for", R"(
local data={a={1,2,3},b={4,5},c={6,7,8}}
local function f(x,y)
    local t=data.a
    if x then t=data.b elseif y then t=data.c end
    if #t>2 then t[1]+=1 end
    local sum=0
    for _,v in ipairs(t) do if v%2==0 then sum+=v else sum-=v end end
    return sum
end
return f(false,false),f(true,false),f(false,true),data.a[1],data.c[1]
)"},
        {"branch-table-identity", R"(
local a,b={x=1},{x=2}
local function f(flag)
    local first=a
    local second=first
    if flag then first=b else second=b end
    first.x+=3
    second.x+=5
    return first==second,first.x,second.x
end
local x,y,z=f(true)
local p,q,r=f(false)
return x,y,z,p,q,r,a.x,b.x
)"},
        {"generic-iterator-tuples", R"(
local function iter(state,index)
    index+=1
    if index<=state then return index,index*2,index%2==0 and false or nil end
end
local log=""
for i,a,b in iter,5,0 do
    if i==2 then continue end
    log..=i..":"..a..":"..tostring(b)..";"
    if i==4 then break end
end
return log
)"},
        {"generic-iterator-side-effects", R"(
local calls=0
local function make()
    calls+=1
    local function iter(_,i) if i<4 then return i+1,(i+1)*3 end end
    return iter,nil,0
end
local sum=0
for k,v in make() do if k==3 then continue end sum+=v end
return sum,calls
)"},
        {"capture-branch-mutations", R"(
local value=2
local function update(x)
    if x>0 then value+=x else value*=2 end
    return value
end
local function read() return value end
local a=update(3)
local b=read()
local c=update(-1)
return a,b,c,value
)"},
        {"capture-per-iteration-close", R"(
local fs={}
for i=1,5 do
    local n=i
    fs[#fs+1]=function(delta) n+=delta return n end
    if i%2==0 then continue end
    n+=10
end
return fs[1](1),fs[2](2),fs[1](3),fs[5](0)
)"},
        {"capture-after-branch-scope", R"(
local fs={}
for i=1,4 do
    if i%2==0 then
        local n=i
        fs[i]=function() n+=1 return n end
    else
        local n=i*10
        fs[i]=function() n-=1 return n end
    end
end
local n=100
return fs[1](),fs[2](),fs[3](),fs[4](),fs[1](),n
)"},
        {"copy-capture-loop-snapshot", R"(
local fs={}
for i=1,5 do
    local x=i*3
    fs[i]=function() return x end
end
return fs[1](),fs[2](),fs[5]()
)"},
        {"capture-close-detached-mutation", R"(
local modify,read
do
    local n=3
    modify=function() n+=7 end
    read=function() return n end
end
local n=40
modify()
return read(),n
)"},
        {"assignment-evaluation-order", R"(
local log=""
local t={10,20,30}
local function f(label,value) log..=label return value end
local i=1
t[f("a",i)],i,t[f("b",i+1)]=f("c",7),f("d",3),f("e",9)
return log,i,t[1],t[2],t[3]
)"},
        {"metamethod-read-order", R"(
local log=""
local t=setmetatable({}, {__index=function(_,key) log..=key return #log end})
local a=t.x+t.y*t.z
local b=t.q
if b<t.r then b+=t.s end
return a,b,log
)"},
        {"metamethod-branch-effects", R"(
local count=0
local t=setmetatable({}, {__index=function(_,key) count+=1 if key=="a" then return false end return count end})
local x=t.a and t.b or t.c
local y=t.d or t.e
if t.f and t.g then count+=10 end
return x,y,count
)"},
        {"call-argument-order", R"(
local log=""
local function arg(x) log..=x return #log end
local function join(...) return select('#',...),... end
local count,a,b,c=join(arg("a"),arg("b"),arg("c"))
return log,count,a,b,c
)"},
        {"vararg-and-multiple-results", R"(
local function pass(...) return ... end
local function f(flag,...)
    if flag then return pass(nil,...) end
    return pass(...)
end
return select('#',f(true,2,nil,4)),select('#',f(false,nil,3)),f(true,false,nil,5)
)"},
        {"conditional-call-tuples", R"(
local function values(flag) if flag then return 1,nil,3 end return 4,5 end
local function sum(flag)
    local a,b,c=values(flag)
    if b then a+=b else a+=10 end
    return a,b,c
end
local a,b,c=sum(true)
return a,b,c,sum(false)
)"},
        {"loop-carried-diamond", R"(
local a,b,c=1,2,3
for i=1,10 do
    if i%3==0 then a,b=b,a elseif i%2==0 then b,c=c,b else a,c=c,a end
    c+=i
end
return a,b,c
)"},
        {"empty-branch-and-zero-trip", R"(
local n=0
if n==0 then else n=9 end
for i=4,1 do n+=i end
while n<0 do n-=1 end
repeat if n>0 then break end n+=1 until true
return n
)"},
        {"boolean-join-in-loop", R"(
local function f(limit)
    local enabled=false
    local total=0
    for i=1,limit do
        if i%2==0 then enabled=not enabled end
        if enabled and i>2 or not enabled and i<4 then total+=i else total-=i end
    end
    return total,enabled
end
return f(0),f(5),f(9)
)"},
        {"nested-functions-early-return", R"(
local function outer(x)
    local function inner(y)
        if x<0 then return y-x end
        if y==0 then return x end
        return x+y
    end
    local n=0
    for i=1,4 do if i==x then return inner(i),n end n+=inner(i) end
    return inner(0),n
end
local a,b=outer(2)
return a,b,outer(-3)
)"},
        {"method-receiver-branch", R"(
local t={x=3}
function t:add(n)
    if n<0 then return self.x end
    for i=1,n do if i%2==0 then self.x+=i else self.x-=i end end
    return self.x
end
return t:add(4),t:add(-1),t.x
)"},
        {"captured-predicate-loop", R"(
local n,total=0,0
local function advance() n+=1 return n<7 end
while advance() do
    if n==3 then continue end
    total+=n
    if total>15 then break end
end
return n,total
)"},
        {"repeat-nested-loop", R"(
local i,sum=0,0
repeat
    i+=1
    for j=1,4 do if j==i then continue end if j+i>6 then break end sum+=i*j end
until i==5 or sum>70
return i,sum
)"},
        {"elseif-tail-effects", R"(
local log=""
local function f(n)
    local x
    if n==1 then x=11 elseif n==2 then x=22 elseif n==3 then x=33 else x=44 end
    log..=x..";"
    if n%2==0 then return x,log end
    return -x,log
end
local a=f(1)
local b=f(2)
local c=f(3)
return a,b,c,f(4)
)"},
        {"mutable-table-read-across-call", R"(
local t={value=1}
local function bump() t.value+=1 return t.value end
local before=t.value
local a=before+bump()+t.value
local after=t.value
if bump()>2 then t.value+=after end
return before,a,after,t.value
)"},
        {"nil-false-boolean-preservation", R"(
local function f(a,b)
    local x
    if a then x=b else x=false end
    local y=a and b
    local z=a or b
    return x,y,z
end
local a,b,c=f(nil,0)
local d,e,g=f(true,false)
return a,b,c,d,e,g,f(false,nil)
)"},
        {"open_method_nil_holes", R"(
local target={}
function target:collect(...) return select('#',...),... end
local function values() return nil,2,nil,4,nil end
return target:collect("prefix",nil,values())
)"},
        {"open_method_zero_results", R"(
local target={}
function target:collect(...) return select('#',...),... end
local function empty() end
local a=target:collect(empty())
return a,target:collect(nil,7,empty())
)"},
        {"open_method_receiver_mutation", R"(
local first,second={tag="first"},{tag="second"}
local function method(self,...) return self.tag,select('#',...),... end
first.method,second.method=method,method
local target=first
local function change() target=second return 8,nil,10 end
return target:method(change())
)"},
        {"open_method_lookup_order", R"(
local log=""
local target=setmetatable({}, {__index=function(_,key)
    log..="lookup;"
    return function(_,...) log..="call;" return log,select('#',...),... end
end})
local function values() log..="arguments;" return nil,4,nil end
return target:method(values())
)"},
        {"open_method_callee_mutation", R"(
local log=""
local target={}
target.method=function(_,...) log..="old;" return log,select('#',...),... end
local function values()
    log..="arguments;"
    target.method=function() return "new" end
    return 3,nil,5
end
return target:method(values())
)"},
        {"open_method_receiver_and_prefix_effects", R"(
local log=""
local target=setmetatable({}, {__index=function()
    log..="L"
    return function(_,...) log..="M" return log,select('#',...),... end
end})
local function receiver() log..="R" return target end
local function prefix() log..="P" return nil end
local function tail() log..="T" return false,nil,7 end
return receiver():method(prefix(),tail())
)"},
        {"open_method_nested_results", R"(
local log=""
local target={}
function target:values() log..="V" return 2,nil,4 end
function target:collect(...) log..="C" return log,select('#',...),... end
return target:collect(target:values())
)"},
        {"open_method_forward_varargs", R"(
local target={tag="target"}
function target:collect(...) return self.tag,select('#',...),... end
function target:forward(...) return self:collect(...) end
return target:forward(2,nil,false,nil,5)
)"},
        {"open_method_iterator_results", R"(
local target={calls=0}
function target:iterator(...)
    self.calls+=select('#',...)
    return function(_,i) if i<3 then return i+1,(i+1)*2 end end,nil,0
end
local function arguments() return 1,nil,3 end
local sum=0
for k,v in target:iterator(arguments()) do sum+=k*v end
return sum,target.calls
)"},
        {"open_method_yielding_argument", R"(
local first,second={tag="first"},{tag="second"}
local target=first
local function method(self,...) return self.tag,select('#',...),... end
first.method,second.method=method,method
local function values() coroutine.yield("pause") return nil,5 end
local run=coroutine.wrap(function() return target:method(values()) end)
local paused=run()
target=second
return paused,run()
)"},
        {"open_setlist_nil_holes", R"(
local function values() return 3,nil,5,nil end
local t={1,nil,values()}
return table.unpack(t,1,6)
)"},
        {"open_setlist_varargs", R"(
local function pack(...)
    local first={nil,1,...}
    local second={...}
    return first[1],first[2],first[3],first[4],first[5],second[1],second[2],second[3]
end
return pack(3,nil,7)
)"},
        {"open_setlist_successive_producers", R"(
local function values(n) return n,nil,n+1 end
local first={values(3)}
local snapshot=first[1]
local second={values(8)}
return snapshot,first[1],first[2],first[3],second[1],second[2],second[3]
)"},
        {"open_fastcall_assert", R"(
local calls=0
local function values() calls+=1 return true,nil,3,nil end
local function run() return assert(values()) end
local count=select('#',run())
return calls,count,run()
)"},
        {"open_fastcall_pcall", R"(
local function values() return 3,nil,5,nil end
local function collect(...) return select('#',...),... end
return pcall(collect,values())
)"},
        {"open_return_through_close", R"(
local n=0
local function values() n+=1 return n,nil,7 end
return n,values()
)"},
    };

    for (auto &[Name, Source] : ControlFlowCases())
        Result.push_back({std::move(Name), std::move(Source)});
    for (auto [Name, Source] : OptimizerCases)
        Result.push_back({Name, Source});

    // Deterministic generated cases combine loop-carried phi values, tables,
    // early exits and several CFG shapes. Constants vary without relying on
    // randomized iteration order or a platform-specific PRNG implementation.
    std::uint32_t Seed = 0x7a2ecf19;
    auto Next = [&]()
    {
        Seed ^= Seed << 13;
        Seed ^= Seed >> 17;
        Seed ^= Seed << 5;
        return Seed;
    };
    for (unsigned Index = 0; Index < 48; ++Index)
    {
        auto Constant = [&](unsigned Modulus, unsigned Offset) { return std::to_string(Next() % Modulus + Offset); };
        auto A = Constant(7, 1), B = Constant(5, 1), Limit = Constant(8, 6), Skip = Constant(4, 2);
        std::string Source = "local a,b,total=" + A + "," + B + ",0\nlocal t={2,3,5,7}\nlocal log=\"\"\n";
        Source += "for i=1," + Limit + " do\n";
        Source += "if i%" + Skip + "==0 then a+=i continue end\n";
        Source += "if (a+i)%3==0 then a,b=b,a elseif b%2==0 then b+=i else a-=1 end\n";
        Source += "if a>b and i>3 then total+=a elseif a==b or b<0 then total-=b else total+=b end\n";
        switch (Index % 4)
        {
        case 0:
            Source += "local j=0 while j<3 do j+=1 if j==2 then continue end total+=j*a end\n";
            break;
        case 1:
            Source += "for k,v in ipairs(t) do if k==i%4 then continue end total+=v if total>300 then break end end\n";
            break;
        case 2:
            Source += "local j=0 repeat j+=1 if j==2 then total+=b else total-=a end until j>=3 or total>150\n";
            break;
        case 3:
            Source += "local chosen=t if i%2==0 then chosen={i,a,b} end for _,v in ipairs(chosen) do total+=v end\n";
            break;
        }
        Source += "log..=a..\":\"..b..\";\"\nif total>500 then break end\nend\nreturn a,b,total,log\n";
        Result.push_back({"generated-" + std::to_string(Index), std::move(Source)});
    }
    return Result;
}

std::vector<Fixture> BytecodeFixtures()
{
    std::vector<Fixture> Result;
    // Compiler-generated source usually snapshots captured registers before
    // open calls. Exercise legal register arrangements that do not, so source
    // simplification cannot accidentally move these reads before the producer.
    for (bool ReturnPrefix : {false, true})
    {
        Luau::BytecodeBuilder Builder;
        auto Collector = Builder.beginFunction(0, true);
        Builder.emitABC(LOP_PREPVARARGS, 0, 0, 0);
        Builder.emitABC(LOP_GETVARARGS, 0, 0, 0);
        Builder.emitABC(LOP_RETURN, 0, 0, 0);
        Builder.endFunction(1, 0);
        auto Producer = Builder.beginFunction(0);
        Builder.emitAD(LOP_LOADN, 0, 1);
        Builder.emitABC(LOP_SETUPVAL, 0, 0, 0);
        Builder.emitAD(LOP_LOADN, 0, 7);
        Builder.emitABC(LOP_LOADNIL, 1, 0, 0);
        Builder.emitAD(LOP_LOADN, 2, 9);
        Builder.emitABC(LOP_RETURN, 0, 4, 0);
        Builder.endFunction(3, 1);
        auto Main = Builder.beginFunction(0);
        auto CollectorIndex = Builder.addChildFunction(Collector);
        auto ProducerIndex = Builder.addChildFunction(Producer);
        unsigned Prefix = ReturnPrefix ? 0 : 1;
        if (!ReturnPrefix)
            Builder.emitAD(LOP_NEWCLOSURE, 0, CollectorIndex);
        Builder.emitAD(LOP_LOADN, std::uint8_t(Prefix), 0);
        Builder.emitAD(LOP_NEWCLOSURE, std::uint8_t(Prefix + 1), ProducerIndex);
        Builder.emitABC(LOP_CAPTURE, LCT_REF, std::uint8_t(Prefix), 0);
        Builder.emitABC(LOP_CALL, std::uint8_t(Prefix + 1), 1, 0);
        if (!ReturnPrefix)
        {
            Builder.emitABC(LOP_CALL, 0, 0, 0);
            Builder.emitABC(LOP_CLOSEUPVALS, 0, 0, 0);
        }
        Builder.emitABC(LOP_RETURN, 0, 0, 0);
        Builder.endFunction(5, 0);
        Builder.setMainFunction(Main);
        Builder.finalize();
        Result.push_back(
            {ReturnPrefix ? "bytecode_open_mutable_return_prefix" : "bytecode_open_mutable_argument", Builder.getBytecode(), true});
    }
    {
        Luau::BytecodeBuilder Builder;
        auto Old = Builder.beginFunction(0, true);
        Builder.emitABC(LOP_PREPVARARGS, 0, 0, 0);
        Builder.emitAD(LOP_LOADN, 0, 1);
        Builder.emitABC(LOP_GETVARARGS, 1, 0, 0);
        Builder.emitABC(LOP_RETURN, 0, 0, 0);
        Builder.endFunction(2, 0);
        auto Replacement = Builder.beginFunction(0, true);
        Builder.emitABC(LOP_PREPVARARGS, 0, 0, 0);
        Builder.emitAD(LOP_LOADN, 0, 9);
        Builder.emitABC(LOP_GETVARARGS, 1, 0, 0);
        Builder.emitABC(LOP_RETURN, 0, 0, 0);
        Builder.endFunction(2, 0);
        auto Producer = Builder.beginFunction(0);
        Builder.emitAD(LOP_NEWCLOSURE, 0, Builder.addChildFunction(Replacement));
        Builder.emitABC(LOP_SETUPVAL, 0, 0, 0);
        Builder.emitAD(LOP_LOADN, 0, 7);
        Builder.emitAD(LOP_LOADN, 1, 8);
        Builder.emitABC(LOP_RETURN, 0, 3, 0);
        Builder.endFunction(2, 1);
        auto Main = Builder.beginFunction(0);
        auto OldIndex = Builder.addChildFunction(Old);
        auto ProducerIndex = Builder.addChildFunction(Producer);
        Builder.emitAD(LOP_NEWCLOSURE, 0, OldIndex);
        Builder.emitAD(LOP_NEWCLOSURE, 1, ProducerIndex);
        Builder.emitABC(LOP_CAPTURE, LCT_REF, 0, 0);
        Builder.emitABC(LOP_CALL, 1, 1, 0);
        Builder.emitABC(LOP_CALL, 0, 0, 0);
        Builder.emitABC(LOP_CLOSEUPVALS, 0, 0, 0);
        Builder.emitABC(LOP_RETURN, 0, 0, 0);
        Builder.endFunction(4, 0);
        Builder.setMainFunction(Main);
        Builder.finalize();
        Result.push_back({"bytecode_open_mutable_callee", Builder.getBytecode(), true});
    }
    {
        Luau::BytecodeBuilder Builder;
        auto First = Builder.beginFunction(0);
        Builder.emitAD(LOP_LOADN, 0, 7);
        Builder.emitABC(LOP_RETURN, 0, 2, 0);
        Builder.endFunction(1, 0);
        auto Second = Builder.beginFunction(0);
        Builder.emitAD(LOP_LOADN, 0, 9);
        Builder.emitABC(LOP_RETURN, 0, 2, 0);
        Builder.endFunction(1, 0);
        auto Main = Builder.beginFunction(0);
        auto FirstIndex = Builder.addChildFunction(First);
        auto SecondIndex = Builder.addChildFunction(Second);
        Builder.emitAD(LOP_NEWCLOSURE, 0, FirstIndex);
        Builder.emitAD(LOP_NEWCLOSURE, 1, SecondIndex);
        Builder.emitABC(LOP_MOVE, 2, 0, 0);
        Builder.emitABC(LOP_CALL, 2, 1, 0);
        Builder.emitABC(LOP_MOVE, 3, 2, 0);
        Builder.emitABC(LOP_MOVE, 4, 1, 0);
        Builder.emitABC(LOP_CALL, 4, 1, 0);
        Builder.emitABC(LOP_RETURN, 3, 2, 0);
        Builder.endFunction(5, 0);
        Builder.setMainFunction(Main);
        Builder.finalize();
        Result.push_back({"bytecode_open_pack_snapshot", Builder.getBytecode(), true});
    }
    {
        Luau::BytecodeBuilder Builder;
        auto Iterator = Builder.beginFunction(2);
        auto One = Builder.addConstantNumber(1);
        Builder.emitAD(LOP_LOADN, 2, 3);
        Builder.emitAD(LOP_JUMPIFNOTLT, 1, 3);
        Builder.emitAux(2);
        Builder.emitABC(LOP_ADDK, 0, 1, std::uint8_t(One));
        Builder.emitABC(LOP_RETURN, 0, 2, 0);
        Builder.emitABC(LOP_LOADNIL, 0, 0, 0);
        Builder.emitABC(LOP_RETURN, 0, 2, 0);
        Builder.endFunction(3, 0);
        auto Main = Builder.beginFunction(0);
        // Iterator calls own the stack above their triple; keep persistent
        // state below it, as the compiler does, instead of aliasing child locals.
        Builder.emitAD(LOP_LOADN, 0, 0);
        Builder.emitAD(LOP_NEWCLOSURE, 1, Builder.addChildFunction(Iterator));
        Builder.emitABC(LOP_LOADNIL, 2, 0, 0);
        Builder.emitAD(LOP_LOADN, 3, 0);
        Builder.emitAD(LOP_FORGPREP, 1, 1);
        Builder.emitABC(LOP_ADD, 0, 0, 4);
        Builder.emitAD(LOP_FORGLOOP, 1, -2);
        Builder.emitAux(1);
        Builder.emitABC(LOP_MOVE, 1, 3, 0);
        Builder.emitABC(LOP_RETURN, 0, 3, 0);
        Builder.endFunction(8, 0);
        Builder.setMainFunction(Main);
        Builder.finalize();
        Result.push_back({"bytecode_observable_iterator_control", Builder.getBytecode(), true});
    }
    {
        Luau::BytecodeBuilder Builder;
        auto Main = Builder.beginFunction(0);
        Builder.emitAD(LOP_LOADN, 0, 3);
        Builder.emitAD(LOP_LOADN, 1, 1);
        Builder.emitAD(LOP_LOADN, 2, 1);
        Builder.emitAD(LOP_LOADN, 3, 0);
        Builder.emitAD(LOP_FORNPREP, 0, 2);
        Builder.emitABC(LOP_ADD, 3, 3, 2);
        Builder.emitAD(LOP_FORNLOOP, 0, -2);
        Builder.emitABC(LOP_MOVE, 0, 3, 0);
        Builder.emitABC(LOP_MOVE, 1, 2, 0);
        Builder.emitABC(LOP_RETURN, 0, 3, 0);
        Builder.endFunction(4, 0);
        Builder.setMainFunction(Main);
        Builder.finalize();
        Result.push_back({"bytecode_observable_numeric_control", Builder.getBytecode(), true});
    }
    return Result;
}
} // namespace

int main(int Argc, char **Argv)
{
    unsigned Cases = 0, Failures = 0, SourceFallbacks = 0, RawFallbacks = 0;
    const std::string Filter = Argc > 1 ? Argv[1] : "";
    const std::map<std::string, std::string> RawExpected = {
        {"bytecode_open_mutable_return_prefix", "3:1:1;3:1:7;0:;3:1:9;"},
        {"bytecode_open_mutable_argument", "3:1:1;3:1:7;0:;3:1:9;"},
        {"bytecode_open_mutable_callee", "3:1:9;3:1:7;3:1:8;"},
        {"bytecode_open_pack_snapshot", "3:1:7;"},
        {"bytecode_observable_iterator_control", "3:1:6;0:;"},
        {"bytecode_observable_numeric_control", "3:1:6;3:1:4;"},
    };
    auto Corpus = Fixtures();
    for (auto &Fixture : BytecodeFixtures())
        Corpus.push_back(std::move(Fixture));
    for (const auto &Fixture : Corpus)
    {
        if (!Filter.empty() && Fixture.Name.find(Filter) == std::string::npos)
            continue;
        for (int Optimization : {0, 1, 2})
            for (int Debug : {0, 2})
            {
                if (Fixture.Serialized && (Optimization != 0 || Debug != 0))
                    continue;
                std::string Input, Expected;
                try
                {
                    Input = Fixture.Serialized ? Fixture.Source : Compile(Fixture.Source, Optimization, Debug);
                    Expected = Execute(Input);
                    if (Fixture.Serialized)
                        Check(Expected == RawExpected.at(Fixture.Name), "Handcrafted bytecode produced unexpected reference result");
                }
                catch (const std::exception &Error)
                {
                    std::cerr << "Invalid fixture " << Fixture.Name << ": " << Error.what() << '\n'
                              << (Fixture.Serialized ? Taze::Disassemble(Input) : Fixture.Source) << '\n';
                    return 1;
                }
                for (unsigned Mode = 0; Mode < (Fixture.Serialized ? 6u : 3u); ++Mode)
                {
                    ++Cases;
                    Taze::Options Options;
                    Options.IncludeHeader = false;
                    Options.IncludeFunctionLocators = false;
                    Options.OptimizationPasses = Mode % 3 == 0 ? 0 : 5;
                    Options.ForceStateMachine = Mode % 3 == 2;
                    Options.NativeCaptures = Mode < 3;
                    std::string Source;
                    try
                    {
                        auto Result = Taze::Decompile(Input, Options);
                        Source = Result.Source;
                        if (!Options.ForceStateMachine)
                            (Fixture.Serialized ? RawFallbacks : SourceFallbacks) += unsigned(Result.StateMachineFunctions);
                        if (!Options.ForceStateMachine && Fixture.Name.starts_with("long_"))
                            Check(Result.StateMachineFunctions == 0, "A regular boolean chain required state-machine fallback");
                        if (!Options.ForceStateMachine &&
                            (Fixture.Name == "open_method_nil_holes" || Fixture.Name == "open_method_zero_results" ||
                             Fixture.Name == "open_method_nested_results" || Fixture.Name == "open_method_forward_varargs"))
                        {
                            Check(Source.find("CallArguments") == std::string::npos && Source.find("ResultBase") == std::string::npos,
                                  "Ordinary open method call retained a dynamic tuple dispatcher");
                            Check(Source.size() < 8 * Fixture.Source.size(), "Ordinary method call expanded excessively");
                        }
                        auto Output = Compile(Source, 0, 0);
                        auto Actual = Execute(Output);
                        Check(Actual == Expected, "Execution mismatch\nExpected: " + Expected + "\nActual: " + Actual);
                        auto Repeat = Taze::Decompile(Input, Options);
                        Check(Source == Repeat.Source && Result.Diagnostics == Repeat.Diagnostics, "Reconstruction is not deterministic");
                    }
                    catch (const std::exception &Error)
                    {
                        ++Failures;
                        std::cerr << "FAIL " << Fixture.Name << " optimization=" << Optimization << " debug=" << Debug
                                  << " passes=" << Options.OptimizationPasses << " machine=" << Options.ForceStateMachine
                                  << " native-captures=" << Options.NativeCaptures << ": " << Error.what() << "\nOriginal:\n"
                                  << (Fixture.Serialized ? Taze::Disassemble(Input) : Fixture.Source) << "\nReconstructed:\n"
                                  << Source << '\n';
                        // Keep failures useful and output bounded. The filter
                        // argument allows investigating any fixture separately.
                        if (Failures >= 12)
                        {
                            std::cerr << "Stopped after 12 failures; " << Checks << " checks passed\n";
                            return 1;
                        }
                    }
                }
            }
    }
    if (Cases == 0)
    {
        std::cerr << "No fixture matches: " << Filter << '\n';
        return 1;
    }
    std::cout << "Control-flow stress: " << Cases << " configurations, " << Checks << " checks passed, " << Failures << " failures, "
              << SourceFallbacks + RawFallbacks << " structured-mode function fallbacks (source " << SourceFallbacks << ", bytecode "
              << RawFallbacks << ")\n";
    return Failures == 0 ? 0 : 1;
}
