#pragma once

#include <string>
#include <utility>
#include <vector>

inline std::vector<std::pair<std::string, std::string>> ScopeCases()
{
    std::vector<std::pair<std::string, std::string>> Cases;
    std::string Tables = "local result={}\n";
    for (unsigned I = 1; I <= 320; ++I)
        Tables += "do local entry={value=" + std::to_string(I) + "}; entry.square=entry.value*entry.value; result[" + std::to_string(I) +
                  "]=entry end\n";
    Tables += "return #result,result[1].value,result[160].square,result[320].square";
    Cases.emplace_back("scope_large_table_values", std::move(Tables));

    std::string Effects = "local total=0; local sink=setmetatable({}, {__newindex=function(_,_,value) total+=value.number end})\n";
    for (unsigned I = 1; I <= 320; ++I)
        Effects += "do local value={number=" + std::to_string(I) + "}; sink[" + std::to_string(I) + "]=value end\n";
    Effects += "return total";
    Cases.emplace_back("scope_large_effectful_table_writes", std::move(Effects));

    std::string Captures = "local callbacks={}\n";
    for (unsigned I = 1; I <= 240; ++I)
        Captures += "do local value=" + std::to_string(I) + "; callbacks[" + std::to_string(I) +
                    "]=function(delta) value+=delta return value end end\n";
    Captures += "return callbacks[1](2),callbacks[120](3),callbacks[240](4),callbacks[1](5)";
    Cases.emplace_back("scope_large_mutable_closure_lifetimes", std::move(Captures));

    std::string Crossed = "local callbacks={}\n";
    for (unsigned I = 1; I <= 160; ++I)
        Crossed += "do local first={value=" + std::to_string(I) + "}; local second={value=" + std::to_string(I * 3) +
                   "}; local function read() return first.value+second.value end; callbacks[" + std::to_string(I) + "]=read end\n";
    Crossed += "return callbacks[1](),callbacks[80](),callbacks[160]()";
    Cases.emplace_back("scope_crossing_capture_intervals", std::move(Crossed));

    std::string Branches = "local values={}; local flag=false; local function nextFlag() flag=not flag return flag end\n";
    for (unsigned I = 1; I <= 240; ++I)
        Branches += "do local item; if nextFlag() then item={value=" + std::to_string(I) + "} else item={value=" + std::to_string(-int(I)) +
                    "} end; values[" + std::to_string(I) + "]=item end\n";
    Branches += "return #values,values[1].value,values[120].value,values[240].value,flag";
    Cases.emplace_back("scope_branch_merged_table_lifetimes", std::move(Branches));

    std::string DeadStores = "local total=0; local flag=false; local function nextFlag() flag=not flag return flag end\n";
    for (unsigned I = 1; I <= 240; ++I)
        DeadStores += "do local unused; if nextFlag() then unused=" + std::to_string(I) +
                      "; total+=1 else unused=" + std::to_string(-int(I)) + "; total+=2 end end\n";
    DeadStores += "return total,flag";
    Cases.emplace_back("scope_unused_branch_stores", std::move(DeadStores));

    // Each total feeds the next branch, while saved values escape through
    // independently mutable closures. Scope boundaries must preserve both the
    // crossing SSA chain and the identity of each captured local.
    std::string ChainCaptures =
        "local total=0; local flag=false; local callbacks={}\nlocal function nextFlag() flag=not flag return flag end\n";
    for (unsigned I = 1; I <= 280; ++I)
        ChainCaptures += "if nextFlag() then total+=1 else total+=2 end\ndo local saved=total; callbacks[" + std::to_string(I) +
                         "]=function(delta) saved+=delta return saved end end\n";
    ChainCaptures += "return total,callbacks[1](10),callbacks[140](20),callbacks[280](30),callbacks[1](40),flag";
    Cases.emplace_back("scope_crossing_chain_capture_exports", std::move(ChainCaptures));

    std::string ChainEffects = "local total=0; local calls=0\nlocal function step(value) calls+=1 return value+calls,value end\n";
    for (unsigned I = 1; I <= 520; ++I)
        ChainEffects += "do local previous; total,previous=step(total); if previous>total then return false end end\n";
    ChainEffects += "return total,calls";
    Cases.emplace_back("scope_crossing_chain_tuple_effects", std::move(ChainEffects));

    Cases.emplace_back("scope_terminal_branch_suffix", R"(
local total=0
for i=1,7 do
    if i%2==0 then total+=1 continue else total+=2 continue end
    total+=1000
end
return total
)");
    return Cases;
}
