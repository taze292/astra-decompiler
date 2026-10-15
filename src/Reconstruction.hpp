#pragma once

#include "Bytecode.hpp"
#include "ControlFlow.hpp"
#include "Exports.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_map>

namespace Taze::Detail
{
bool Identifier(std::string_view Text);
std::string Pascal(std::string_view Text);
std::string CaseName(std::string_view Text, VariableCase Casing);
std::string Token(unsigned Id);
std::vector<unsigned> Tokens(const std::string &Text);
void Replace(std::string &Text, const std::string &From, const std::string &To);
std::string Join(const std::vector<std::string> &Items, const std::string &Separator = ", ");
std::size_t Closing(const std::string &Text, std::size_t Start);
std::string Unwrap(std::string Text);
bool PrefixExpression(const std::string &Text);
std::string CleanExpression(std::string Text);
std::string Negate(std::string Text);
std::string CleanCondition(std::string Text);
std::vector<std::string> SplitLogical(const std::string &Text, const std::string &Operator);
std::string FormatCondition(std::string Text, unsigned ParentPrecedence = 0);
bool Jump(LuauOpcode Op);
bool Conditional(LuauOpcode Op);

enum class Kind
{
    Assign,
    Raw,
    Return,
    If,
    While,
    Repeat,
    For,
    Break,
    Continue,
    Function
};
struct Statement
{
    Kind Type = Kind::Raw;
    std::vector<unsigned> Left;
    std::string Text;
    std::vector<Statement> Body, Else;
    bool Pure = false, Call = false;
    Statement(Kind K = Kind::Raw, std::vector<unsigned> L = {}, std::string T = {}, std::vector<Statement> B = {},
              std::vector<Statement> Other = {}, bool P = false, bool C = false)
        : Type(K), Left(std::move(L)), Text(std::move(T)), Body(std::move(B)), Else(std::move(Other)), Pure(P), Call(C)
    {
    }
};
struct Symbol
{
    std::string Name;
    std::string Base;
    bool Keep = false, Parameter = false, Cell = false;
    unsigned Owner = 0;
    bool ReferenceAlias = false;
    std::string Category = {};
    std::string Original;
    bool Preserve = false;
    bool Debug = false;
};
struct Upvalue
{
    std::string Value;
    std::string Cell;
    std::string Name;
    bool Reference = false;
};
struct Unstructured : Error
{
    using Error::Error;
};

class Engine;
class Function
{
    Engine &E;
    const Prototype &P;
    ControlFlow Flow;
    unsigned Id, Depth;
    unsigned ExportFrame = std::numeric_limits<unsigned>::max();
    std::vector<Upvalue> Upvalues;
    std::vector<unsigned> Registers;
    std::vector<unsigned> LocalSymbols;
    std::vector<unsigned> LocalStarts;
    std::vector<unsigned> Parameters;
    std::vector<std::vector<unsigned>> ReadSymbols, WriteSymbols;
    std::set<unsigned> Cells;
    std::vector<Statement> Prefix;
    unsigned PackSymbol, TopSymbol, VarargsSymbol;
    bool NeedsPack = false, NeedsVarargs = false;
    bool Dispatch = false;
    std::string PendingMethod, PendingReceiver;
    unsigned PendingBase = 0;
    std::string OpenExpression;
    unsigned OpenExpressionBase = 0;
    std::string OpenHint;
    std::map<unsigned, std::string> Hints;
    std::vector<unsigned> RegionVisits;
    std::map<const std::vector<Statement> *, std::set<unsigned>> ScopedDeclarations;
    void BuildDataflow();

    unsigned SymbolAt(unsigned R, unsigned Pc, bool Write = false) const;
    std::string Read(unsigned R, unsigned Pc) const;
    std::string WriteName(unsigned R, unsigned Pc) const;
    std::string ConstantAt(unsigned Index, unsigned Recursion = 0) const;
    std::string Locator() const;
    std::string Member(const std::string &Object, unsigned Index) const;
    std::string Global(unsigned Index) const;
    Statement Assign(unsigned R, const Instruction &I, std::string Value, bool Pure = false, bool Call = false);
    void Simple(const Instruction &I, std::vector<Statement> &Out);
    std::string Condition(const Instruction &I) const;
    std::vector<Statement> Region(unsigned Start, unsigned End, int BreakTarget = -1, int ContinueTarget = -1, unsigned Level = 0,
                                  bool IgnoreBackedge = false);
    std::vector<Statement> StateMachine();
    std::string Closure(const Instruction &I, std::vector<Statement> &Out);
    unsigned Index(unsigned Pc) const;
    void Optimize(std::vector<Statement> &Nodes);
    std::string Render(const std::vector<Statement> &Nodes, unsigned Indent, std::set<unsigned> &Declared, const std::set<unsigned> &Hoist);

  public:
    Function(Engine &Owner, unsigned Proto, std::vector<Upvalue> Captures, unsigned Nesting);
    std::string Generate(bool Main = false);
    std::string Signature() const;
};

class Engine
{
  public:
    const Chunk &C;
    const Options &Settings;
    AnalysisBudget Analysis;
    Result Output;
    std::vector<Symbol> Symbols;
    std::set<std::string> Names;
    std::set<std::string> GlobalNames;
    std::map<unsigned, std::set<unsigned>> Parents;
    Exports ExportGraph;
    unsigned MainExportFrame = std::numeric_limits<unsigned>::max();
    std::map<unsigned, std::string> Locators;
    std::size_t Work = 0;
    unsigned NextValue = 0;

    Engine(const Chunk &Bytecode, const Options &Options);
    unsigned New(std::string Name, unsigned Owner, bool Keep = false, bool Parameter = false, bool Cell = false, bool Preserve = false, bool Debug = false);
    void Rename(unsigned Target, unsigned Source);
    bool AnalyzeExports() const;
    std::string Resolve(std::string Text);
    void Budget(std::size_t Amount);
};

} // namespace Taze::Detail
