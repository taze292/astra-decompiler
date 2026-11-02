#include "Reconstruction.hpp"

namespace Astra::Detail
{
unsigned Function::SymbolAt(unsigned R, unsigned Pc, bool Write) const
{
    if (R >= P.Stack)
        throw Error("Invalid register during reconstruction");
    if (Cells.count(R) || Dispatch)
        return Registers[R];
    if (!ReadSymbols.empty())
        return (Write ? WriteSymbols : ReadSymbols).at(Index(Pc)).at(R);
    for (unsigned J = unsigned(P.Locals.size()); J-- > 0;)
        if (P.Locals[J].Register == R && Pc >= LocalStarts[J] && Pc < P.Locals[J].End &&
            (Write || Pc != LocalStarts[J] || P.Locals[J].Start == LocalStarts[J] || E.Symbols[LocalSymbols[J]].Parameter))
            return LocalSymbols[J];
    return Registers[R];
}
std::string Function::Read(unsigned R, unsigned Pc) const
{
    unsigned S = SymbolAt(R, Pc);
    return Token(S) + (E.Symbols[S].Cell ? "[1]" : "");
}
std::string Function::WriteName(unsigned R, unsigned Pc) const
{
    return Read(R, Pc);
}
unsigned Function::Index(unsigned Pc) const
{
    if (Pc == P.Code.size())
        return unsigned(P.Instructions.size());
    auto Instruction = P.AtPc(Pc);
    if (Instruction < 0)
        throw Unstructured("invalid region boundary");
    return unsigned(Instruction);
}

int Function::OpenBaseAt(unsigned Pc) const
{
    // Recover a tuple's fixed base through straight-line compiler setup (for
    // example GETIMPORT assert, FASTCALL, or CLOSEUPVALS). Keep the pack at its
    // original evaluation point; only remove the dynamic prefix dispatcher.
    unsigned Pos = Index(Pc);
    int HighestWrite = -1;
    for (unsigned Steps = 0; Steps < 64; ++Steps)
    {
        E.Analysis.Spend();
        if (Flow.Predecessors[Pos].size() != 1)
            return -1;
        unsigned Previous = Flow.Predecessors[Pos][0];
        if (Previous >= Pos)
            return -1;
        const auto &Producer = P.Instructions[Previous];
        if (Producer.Op == LOP_SETLIST && !Producer.C)
            return -1; // Consuming a tuple restores the VM frame top.
        if (Producer.Op == LOP_FORGPREP || Producer.Op == LOP_FORGPREP_NEXT || Producer.Op == LOP_FORGPREP_INEXT ||
            Producer.Op == LOP_FORGLOOP)
            return -1; // Iteration can invoke another function and restore top.
        if (Producer.Op == LOP_CALL || Producer.Op == LOP_CALLFB || Producer.Op == LOP_GETVARARGS)
        {
            bool Open = Producer.Op == LOP_GETVARARGS ? !Producer.B : !Producer.C;
            return Open && HighestWrite < int(Producer.A) ? int(Producer.A) : -1;
        }
        for (unsigned Reg : Writes(Producer, P))
            HighestWrite = std::max(HighestWrite, int(Reg));
        Pos = Previous;
    }
    return -1;
}

std::string Function::ConstantAt(unsigned Index, unsigned Recursion) const
{
    if (Recursion > 32)
        throw Error("Constant nesting too deep");
    const auto &K = P.Constants.at(Index);
    switch (K.Tag)
    {
    case LBC_CONSTANT_NIL:
        return "nil";
    case LBC_CONSTANT_BOOLEAN:
        return K.Index ? "true" : "false";
    case LBC_CONSTANT_NUMBER:
        return Number(K.Number);
    case LBC_CONSTANT_STRING:
        return Quote(K.Text);
    case LBC_CONSTANT_VECTOR:
    case LBC_CONSTANT_VECTORD:
        if (K.Vector[3] != 0)
            throw Error("Four-component vector constants require a four-component target runtime");
    {
        bool Single = K.Tag == LBC_CONSTANT_VECTOR;
        return E.Settings.VectorConstructor + "(" + Number(K.Vector[0], Single) + ", " + Number(K.Vector[1], Single) + ", " +
               Number(K.Vector[2], Single) + ")";
    }
    case LBC_CONSTANT_INTEGER:
        return "integer.fromstring(" + Quote((K.Negative ? "-" : "") + std::to_string(K.Integer)) + ")";
    case LBC_CONSTANT_TABLE:
    case LBC_CONSTANT_TABLE_WITH_CONSTANTS:
    {
        std::vector<std::string> Entries;
        for (auto [Key, Value] : K.Entries)
        {
            auto Name = P.Constants.at(Key);
            std::string Field =
                Name.Tag == LBC_CONSTANT_STRING && Identifier(Name.Text) ? Name.Text : "[" + ConstantAt(Key, Recursion + 1) + "]";
            Entries.push_back(Field + " = " + (Value < 0 ? "0" : ConstantAt(unsigned(Value), Recursion + 1)));
        }
        return Entries.empty() ? "{}" : "{ " + Join(Entries) + " }";
    }
    case LBC_CONSTANT_IMPORT:
    {
        std::string Path;
        for (unsigned J = 0; J < (K.Index >> 30); ++J)
        {
            unsigned Part = (K.Index >> (20 - 10 * J)) & 1023;
            Path = J ? Member(Path, Part) : Global(Part);
        }
        return Path;
    }
    case LBC_CONSTANT_CLASS_SHAPE:
        throw Error("Experimental Luau class constants cannot be represented in the stable Roblox language");
    default:
        throw Error("Constant is not a source literal");
    }
}
std::string Function::Locator() const
{
    std::vector<std::string> Constants;
    bool HasLineAndName = P.Line && !P.Name.empty();
    for (unsigned Index = 0; !HasLineAndName && Index < P.Constants.size() && Constants.size() < 5; ++Index)
    {
        const auto &K = P.Constants[Index];
        switch (K.Tag)
        {
        case LBC_CONSTANT_NUMBER:
            // NaN cannot be matched by equality. Nil is not an element of a Luau array.
            if (!std::isnan(K.Number))
                Constants.push_back(ConstantAt(Index));
            break;
        case LBC_CONSTANT_BOOLEAN:
        case LBC_CONSTANT_STRING:
        case LBC_CONSTANT_IMPORT:
        case LBC_CONSTANT_INTEGER:
        case LBC_CONSTANT_VECTOR:
        case LBC_CONSTANT_VECTORD:
            Constants.push_back(ConstantAt(Index));
            break;
        default:
            // Serialized table templates and closure prototypes do not identify live objects.
            break;
        }
    }
    auto Text = "filtergc(\"function\", { " + E.Settings.FilterLineField + " = " + (P.Line ? std::to_string(P.Line) : "nil") +
                (P.Name.empty() ? "" : ", Name = " + Quote(P.Name)) +
                (HasLineAndName ? "" : ", Constants = {" + (Constants.empty() ? "" : " " + Join(Constants) + " ") + "}") + " }, true)";
    E.Locators[Id] = Text;
    return "\x1d" + std::to_string(Id) + "\x1c";
}

std::string Function::Member(const std::string &Object, unsigned Index) const
{
    const auto &K = P.Constants.at(Index);
    return Object + (K.Tag == LBC_CONSTANT_STRING && Identifier(K.Text) ? "." + K.Text : "[" + ConstantAt(Index) + "]");
}
std::string Function::TableText(const std::vector<std::pair<std::string, std::string>> &Fields) const
{
    std::vector<std::string> Items;
    bool Multiline = false;
    for (const auto &[Key, Value] : Fields)
        if (!Value.empty())
        {
            Items.push_back(Key.empty() ? Value : Key + " = " + Value);
            Multiline |= Value.find('\n') != std::string::npos;
        }
    if (Items.empty())
        return "{}";
    if (Items.size() <= 3 && !Multiline)
        return "{ " + Join(Items) + " }";
    const std::string Indent(E.Settings.UseTabs ? 1 : E.Settings.IndentWidth, E.Settings.UseTabs ? '\t' : ' ');
    std::string Text = "{\n";
    for (auto Item : Items)
    {
        Replace(Item, "\n", "\n" + Indent);
        Text += Indent + Item + ",\n";
    }
    return Text + "}";
}
std::string Function::Global(unsigned Index) const
{
    const auto &Name = P.Constants.at(Index).Text;
    return Identifier(Name) ? Name : "getfenv()[" + Quote(Name) + "]";
}
Statement Function::Assign(unsigned R, const Instruction &I, std::string Value, bool Pure, bool Call)
{
    unsigned S = SymbolAt(R, I.Pc, true);
    if (E.Symbols[S].Cell)
        return {Kind::Raw, {}, Token(S) + "[1] = " + Value};
    return {Kind::Assign, {S}, std::move(Value), {}, {}, Pure, Call};
}

bool Function::SingleResultBuiltin(const Instruction &Call) const
{
    // At O2 Luau gives a call to a builtin that always returns one value a
    // fixed result count, even as a final argument. Such a call needs no
    // truncating parentheses. The builtin is identified by the FASTCALL whose
    // fallback is this CALL.
    for (auto Pos = Index(Call.Pc), Steps = 0u; Pos-- > 0 && Steps < 16; ++Steps)
    {
        const auto &I = P.Instructions[Pos];
        if (!Luau::isFastCall(I.Op))
            continue;
        if (I.Op == LOP_FASTPCALL || I.Op == LOP_FINCONSTRUCT || I.Pc + I.C + 1 != Call.Pc)
            return false;
        switch (LuauBuiltinFunction(I.A))
        {
        case LBF_NONE:
        case LBF_ASSERT:
        case LBF_MATH_FREXP:
        case LBF_MATH_MODF:
        case LBF_STRING_BYTE:
        case LBF_TABLE_INSERT:
        case LBF_TABLE_UNPACK:
        case LBF_TABLE_CLEAR:
        case LBF_SELECT_VARARG:
        case LBF_BUFFER_WRITEU8:
        case LBF_BUFFER_WRITEU16:
        case LBF_BUFFER_WRITEU32:
        case LBF_BUFFER_WRITEF32:
        case LBF_BUFFER_WRITEF64:
        case LBF_BUFFER_WRITEINTEGER:
            return false;
        default:
            // Builtins newer than this table may return any number of values.
            return I.A <= LBF_TABLE_CLEAR;
        }
    }
    return false;
}

std::string Function::Signature() const
{
    std::vector<std::string> Args;
    for (auto S : Parameters)
        Args.push_back(Token(S));
    if (P.Vararg)
        Args.push_back("...");
    return "(" + Join(Args) + ")";
}

bool Function::FixedCapture(unsigned Symbol, unsigned Register, unsigned Capture) const
{
    // A copy capture can name the binding directly when nothing assigns that
    // binding again after the closure exists. A loop's own iteration write
    // instead starts a fresh body-scoped local for each iteration.
    if (Dispatch || WriteSymbols.empty())
        return false;
    std::vector<bool> Seen(P.Instructions.size());
    std::vector<unsigned> Pending(Flow.Successors[Capture].begin(), Flow.Successors[Capture].end());
    while (!Pending.empty())
    {
        unsigned N = Pending.back();
        Pending.pop_back();
        if (N >= P.Instructions.size() || Seen[N])
            continue;
        Seen[N] = true;
        E.Analysis.Spend();
        const auto &I = P.Instructions[N];
        for (unsigned W : Writes(I, P))
        {
            if (W != Register || WriteSymbols[N][W] != Symbol)
                continue;
            bool Iteration = (I.Op == LOP_FORGLOOP && W >= I.A + 3) || (I.Op == LOP_FORNLOOP && W == I.A + 2);
            if (!Iteration || I.Target() < 0 || Index(unsigned(I.Target())) > Capture || Capture >= N)
                return false;
        }
        Pending.insert(Pending.end(), Flow.Successors[N].begin(), Flow.Successors[N].end());
    }
    return true;
}

std::string Function::Closure(const Instruction &I, std::vector<Statement> &Out)
{
    unsigned Child = I.Op == LOP_NEWCLOSURE ? P.Children.at(unsigned(I.D)) : P.Constants.at(unsigned(I.D)).Index;
    E.Parents[Child].insert(Id);
    std::vector<Upvalue> Captures;
    std::string Self;
    unsigned SelfSymbol = std::numeric_limits<unsigned>::max();
    for (unsigned J = 0; J < I.Captures.size(); ++J)
    {
        auto [Kind, R] = I.Captures[J];
        Upvalue U;
        std::string Argument;
        if (Kind == LCT_UPVAL)
        {
            U = Upvalues.at(R);
            if (U.Cell.empty())
            {
                Captures.push_back(std::move(U));
                continue;
            }
            Argument = U.Reference ? U.Cell : U.Value;
        }
        else
        {
            unsigned S = SymbolAt(R, I.Pc, R == I.A);
            U.Name = E.Symbols[S].Name;
            U.Reference = Kind == LCT_REF;
            if (U.Reference && !E.Symbols[S].Cell)
            {
                U.Value = Token(S);
                Captures.push_back(std::move(U));
                continue;
            }
            Argument = U.Reference ? Token(Registers[R]) : Read(R, I.Pc);
        }
        if (Kind == LCT_VAL && R == I.A)
        {
            if (Self.empty())
            {
                auto Destination = SymbolAt(I.A, I.Pc, true);
                unsigned Definitions = DefinitionCounts[Destination];
                SelfSymbol = !Dispatch && !E.Symbols[Destination].Cell && Definitions == 1
                                 ? Destination
                                 : E.New(E.C.Prototypes[Child].Name.empty() ? "RecursiveFunction" : E.C.Prototypes[Child].Name, Id, true,
                                         false, false, true);
                Self = Token(SelfSymbol);
            }
            U.Value = Self;
            U.Cell.clear();
            Captures.push_back(std::move(U));
            continue;
        }
        if (Kind == LCT_VAL && !Cells.count(R))
        {
            unsigned S = SymbolAt(R, I.Pc);
            unsigned Definitions = DefinitionCounts[S];
            bool Loop = LoopCaptures[Index(I.Pc)];
            if ((Definitions == 1 && !Loop) || FixedCapture(S, R, Index(I.Pc)))
            {
                U.Value = Read(R, I.Pc);
                U.Cell.clear();
                E.Symbols[S].Keep = true;
                E.Symbols[S].Captured = true;
                Captures.push_back(std::move(U));
                continue;
            }
        }
        // Snapshot copy captures and reference-cell identities at closure creation, including per-iteration captures.
        unsigned Capture = E.New(U.Name + "Capture", Id, true);
        E.Symbols[Capture].ReferenceAlias = U.Reference;
        Out.push_back({Kind::Assign, {Capture}, Argument});
        auto References = Tokens(Argument);
        if (!U.Reference && References.size() == 1 && Argument == Token(References.front()))
            E.ExportGraph.Alias(Capture, References.front());
        U.Cell = U.Reference ? Token(Capture) : "";
        U.Value = Token(Capture) + (U.Reference ? "[1]" : "");
        Captures.push_back(std::move(U));
    }
    Function Nested(E, Child, std::move(Captures), Depth + 1);
    if (E.AnalyzeExports())
        E.ExportGraph.Closure(SymbolAt(I.A, I.Pc, true), Nested.ExportFrame, ExportFrame, I.Pc);
    auto Next = Index(I.Next);
    if (!Nested.Parameters.empty() && E.C.Prototypes[Child].Locals.empty() && Next < P.Instructions.size())
    {
        const auto &Install = P.Instructions[Next];
        if (Install.Op == LOP_SETTABLEKS && Install.A == I.A &&
            std::any_of(Nested.P.Instructions.begin(), Nested.P.Instructions.end(), [](const auto &Code)
                        { return Code.B == 0 && (Code.Op == LOP_NAMECALL || Code.Op == LOP_SETTABLEKS || Code.Op == LOP_GETTABLEKS); }))
        {
            E.Symbols[Nested.Parameters.front()].Name = "Self";
            E.Symbols[Nested.Parameters.front()].Original = "Self";
        }
    }
    std::string Body = Nested.Generate();
    std::string Text = "function" + Nested.Signature();
    if (Nested.RecoveredInline)
        Text += " -- Uninlined by Astra";
    else if (E.Settings.IncludeLineComments)
    {
        Text += " -- Line: " + (E.C.Prototypes[Child].Line ? std::to_string(E.C.Prototypes[Child].Line) : "Unknown");
        if (E.Settings.IncludeFunctionLocators && (E.Settings.IncludeGcLocators || E.AnalyzeExports()))
            Text += Nested.Locator();
    }
    Text += "\n" + Body + "end";
    if (!Self.empty() && SelfSymbol != SymbolAt(I.A, I.Pc, true))
    {
        Out.push_back({Kind::Assign, {SelfSymbol}, Text});
        return Self;
    }
    return Text;
}

std::string Function::Condition(const Instruction &I) const
{
    std::string Left = Read(I.A, I.Pc), Right, Op;
    bool Invert = false;
    switch (I.Op)
    {
    case LOP_JUMPIF:
        return Left;
    case LOP_JUMPIFNOT:
        return "not " + Left;
    case LOP_JUMPIFEQ:
        Op = "==";
        Right = Read(I.Aux, I.Pc);
        break;
    case LOP_JUMPIFLE:
        Op = "<=";
        Right = Read(I.Aux, I.Pc);
        break;
    case LOP_JUMPIFLT:
        Op = "<";
        Right = Read(I.Aux, I.Pc);
        break;
    case LOP_JUMPIFNOTEQ:
        Op = "==";
        Right = Read(I.Aux, I.Pc);
        Invert = true;
        break;
    case LOP_JUMPIFNOTLE:
        Op = "<=";
        Right = Read(I.Aux, I.Pc);
        Invert = true;
        break;
    case LOP_JUMPIFNOTLT:
        Op = "<";
        Right = Read(I.Aux, I.Pc);
        Invert = true;
        break;
    case LOP_JUMPXEQKNIL:
        Right = "nil";
        Op = "==";
        Invert = (I.Aux >> 31) != 0;
        break;
    case LOP_JUMPXEQKB:
        Right = (I.Aux & 1) ? "true" : "false";
        Op = "==";
        Invert = (I.Aux >> 31) != 0;
        break;
    case LOP_JUMPXEQKN:
    case LOP_JUMPXEQKS:
        Right = ConstantAt(I.Aux & 0xffffff);
        Op = "==";
        Invert = (I.Aux >> 31) != 0;
        break;
    default:
        throw Unstructured("unsupported conditional branch");
    }
    auto Expression = Left + " " + Op + " " + Right;
    return Invert ? "not (" + Expression + ")" : Expression;
}

void Function::Simple(const Instruction &I, std::vector<Statement> &Out)
{
    auto Indent = [&]() { return std::string(E.Settings.UseTabs ? 1 : E.Settings.IndentWidth, E.Settings.UseTabs ? '\t' : ' '); };
    auto R = [&](unsigned Reg) { return Read(Reg, I.Pc); };
    auto Emit = [&](std::string Text) { Out.push_back({Kind::Raw, {}, std::move(Text)}); };
    auto Set = [&](std::string Text, bool Pure = false, bool Call = false) { Out.push_back(Assign(I.A, I, std::move(Text), Pure, Call)); };
    auto Hint = [&](std::string Name, bool Keep = false, bool Enabled = true)
    {
        unsigned S = SymbolAt(I.A, I.Pc, true);
        Hints[S] = Name;
        if (Enabled && !Name.empty() && !E.Symbols[S].Keep)
        {
            auto Nice = E.New(Name, Id, Keep);
            E.Rename(S, Nice);
            E.Symbols[S].Keep = Keep;
        }
    };
    auto Many = [&](unsigned Base, unsigned Count, const std::string &Text, bool MultiValue = true)
    {
        std::vector<unsigned> Lhs;
        bool Box = false;
        for (unsigned J = 0; J < Count; ++J)
        {
            auto S = SymbolAt(Base + J, I.Pc, true);
            Lhs.push_back(S);
            Box |= E.Symbols[S].Cell;
        }
        if (!Count)
            Emit(Text);
        else if (Box)
        {
            std::vector<std::string> Names;
            for (auto S : Lhs)
                Names.push_back(Token(S) + (E.Symbols[S].Cell ? "[1]" : ""));
            Emit(Join(Names) + " = " + Text);
        }
        else
            Out.push_back({Kind::Assign, std::move(Lhs), Text, {}, {}, false, MultiValue});
    };
    auto CanInlineOpen = [&]()
    {
        auto Next = Index(I.Next);
        if (Dispatch || Next >= P.Instructions.size())
            return false;
        // Luau schedules NAMECALL after argument evaluation. An open result
        // feeding that pair can stay a natural last argument, without packing
        // every register or emitting a dynamic tuple-copy dispatcher.
        unsigned Receiver = P.Stack;
        unsigned ReceiverPc = I.Pc;
        if (P.Instructions[Next].Op == LOP_NAMECALL || P.Instructions[Next].Op == LOP_NAMECALLUDATA)
        {
            const auto &Method = P.Instructions[Next];
            if (Method.A + 2 > I.A || Method.B >= I.A || Flow.Predecessors[Next].size() != 1)
                return false;
            Receiver = Method.B;
            ReceiverPc = Method.Pc;
            Next = Index(Method.Next);
        }
        // Luau places a builtin's FASTCALL, and then the fallback's function
        // import, between the final argument and the CALL that consumes it.
        // Closing native captures before a return leaves the open values intact.
        unsigned Imported = P.Stack;
        while (Receiver == P.Stack && Next < P.Instructions.size() && Flow.Predecessors[Next].size() == 1)
        {
            const auto &Between = P.Instructions[Next];
            if (Between.Op == LOP_GETIMPORT && Imported == P.Stack && Between.A < I.A)
                Imported = Between.A;
            else if (Between.Op == LOP_CLOSEUPVALS && Imported == P.Stack)
            {
                if (std::any_of(Cells.begin(), Cells.end(), [&](unsigned Reg) { return Reg >= Between.A; }))
                    return false;
            }
            else if (!Luau::isFastCall(Between.Op) || Between.Op == LOP_FINCONSTRUCT || Imported != P.Stack)
                break;
            Next = Index(Between.Next);
        }
        if (Next >= P.Instructions.size() || Flow.Predecessors[Next].size() != 1)
            return false;
        const auto &Consumer = P.Instructions[Next];
        if (Imported != P.Stack && Consumer.A != Imported)
            return false;
        bool Call = (Consumer.Op == LOP_CALL || Consumer.Op == LOP_CALLFB) && !Consumer.B && Consumer.A < I.A;
        bool Return = Consumer.Op == LOP_RETURN && !Consumer.B && Consumer.A <= I.A;
        bool List = Consumer.Op == LOP_SETLIST && !Consumer.C && Consumer.B <= I.A && Imported == P.Stack;
        if (!Call && !Return && !List)
            return false;
        // Moving a call into an expression must not move a mutable capture read
        // before that call. Ordinary SSA locals already snapshot these values.
        auto Stable = [&](unsigned Reg, unsigned Pc)
        {
            const auto &S = E.Symbols[SymbolAt(Reg, Pc)];
            return !S.Cell && !S.ReferenceAlias;
        };
        if (Receiver != P.Stack && !Stable(Receiver, ReceiverPc))
            return false;
        unsigned Start = List ? Consumer.B : Receiver == P.Stack ? Consumer.A : Consumer.A + 2;
        for (unsigned Reg = Start; Reg < I.A; ++Reg)
            if (!Stable(Reg, Consumer.Pc))
                return false;
        return true;
    };
    switch (I.Op)
    {
    case LOP_NOP:
    case LOP_BREAK:
    case LOP_COVERAGE:
    case LOP_PREPVARARGS:
    case LOP_FASTCALL:
    case LOP_FASTCALL1:
    case LOP_FASTCALL2:
    case LOP_FASTCALL2K:
    case LOP_FASTCALL3:
    case LOP_FASTPCALL:
        break; // Execute the compiler's ordinary fallback path for optimization hints.
    case LOP_NATIVECALL:
        throw Error("NATIVECALL is a runtime patch; supply original serialized bytecode");
    case LOP_LOADNIL:
        Set("nil", true);
        break;
    case LOP_LOADB:
        Set(I.B ? "true" : "false", true);
        break;
    case LOP_LOADN:
        Set(std::to_string(I.D), true);
        break;
    case LOP_LOADK:
        if (P.Constants.at(unsigned(I.D)).Tag == LBC_CONSTANT_STRING)
            Hints[SymbolAt(I.A, I.Pc, true)] = P.Constants[unsigned(I.D)].Text;
        Set(ConstantAt(unsigned(I.D)), true);
        break;
    case LOP_LOADKX:
        Set(ConstantAt(I.Aux), true);
        break;
    case LOP_MOVE:
        Hints[SymbolAt(I.A, I.Pc, true)] = Hints[SymbolAt(I.B, I.Pc)];
        Set(R(I.B), true);
        break;
    case LOP_GETGLOBAL:
        Hints[SymbolAt(I.A, I.Pc, true)] = P.Constants[I.Aux].Text;
        Set(Global(I.Aux));
        break;
    case LOP_SETGLOBAL:
        Emit(Global(I.Aux) + " = " + R(I.A));
        break;
    case LOP_GETUPVAL:
        Set(Upvalues.at(I.B).Value, true);
        break;
    case LOP_SETUPVAL:
        if (!Upvalues.at(I.B).Reference)
            throw Error("SETUPVAL targets a copy capture");
        Emit(Upvalues[I.B].Value + " = " + R(I.A));
        break;
    case LOP_CLOSEUPVALS:
        for (unsigned Reg : Cells)
            if (Reg >= I.A)
                Emit(Token(Registers[Reg]) + " = { " + Token(Registers[Reg]) + "[1] }");
        break;
    case LOP_GETIMPORT:
    {
        std::string Path;
        for (unsigned J = 0; J < (I.Aux >> 30); ++J)
        {
            unsigned Part = (I.Aux >> (20 - 10 * J)) & 1023;
            Path = J ? Member(Path, Part) : Global(Part);
        }
        Hints[SymbolAt(I.A, I.Pc, true)] = Path;
        ImportPaths.insert(Path);
        // Imports resolve once when the closure loads; Luau itself reorders
        // them freely around argument evaluation.
        Set(Path, true);
        break;
    }
    case LOP_GETTABLE:
        Set(R(I.B) + "[" + R(I.C) + "]");
        break;
    case LOP_SETTABLE:
        Emit(R(I.B) + "[" + R(I.C) + "] = " + R(I.A));
        break;
    case LOP_GETTABLEKS:
    case LOP_GETUDATAKS:
    {
        unsigned Key = I.Op == LOP_GETUDATAKS ? I.Aux & 65535 : I.Aux;
        auto Name = P.Constants[Key].Text;
        Hint(Name == "new" ? Hints[SymbolAt(I.B, I.Pc)] + ".new" : Name, false, E.Settings.InferMemberNames);
        Set(Member(R(I.B), Key));
        break;
    }
    case LOP_SETTABLEKS:
    case LOP_SETUDATAKS:
        Emit(Member(R(I.B), I.Op == LOP_SETUDATAKS ? I.Aux & 65535 : I.Aux) + " = " + R(I.A));
        break;
    case LOP_GETTABLEN:
        Set(R(I.B) + "[" + std::to_string(I.C + 1) + "]");
        break;
    case LOP_SETTABLEN:
        Emit(R(I.B) + "[" + std::to_string(I.C + 1) + "] = " + R(I.A));
        break;
    case LOP_NEWCLOSURE:
    case LOP_DUPCLOSURE:
    {
        unsigned S = SymbolAt(I.A, I.Pc, true);
        E.Symbols[S].Keep = true;
        unsigned Child = I.Op == LOP_NEWCLOSURE ? P.Children.at(unsigned(I.D)) : P.Constants.at(unsigned(I.D)).Index;
        const auto &FunctionName = E.C.Prototypes[Child].Name;
        if (!FunctionName.empty() &&
            (E.Symbols[S].Name.starts_with("Value") || (Identifier(FunctionName) && E.Symbols[S].Name == Pascal(FunctionName))))
        {
            unsigned Nice = E.New(FunctionName, Id, true, false, false, true);
            E.Rename(S, Nice);
        }
        Set(Closure(I, Out));
        break;
    }
    case LOP_NAMECALL:
    case LOP_NAMECALLUDATA:
    {
        unsigned Key = I.Op == LOP_NAMECALLUDATA ? I.Aux & 65535 : I.Aux;
        PendingMethod = P.Constants.at(Key).Text;
        PendingReceiver = R(I.B);
        PendingBase = I.A;
        // NAMECALL is paired with CALL; preserve the colon syntax for Roblox __namecall.
        break;
    }
    case LOP_CALL:
    case LOP_CALLFB:
    {
        bool Method = !PendingMethod.empty() && PendingBase == I.A;
        unsigned Start = I.A + (Method ? 2 : 1);
        std::vector<std::string> Args;
        if (I.B)
            for (unsigned J = Start; J < I.A + I.B; ++J)
                Args.push_back(R(J));
        else if (!OpenExpression.empty())
        {
            for (unsigned J = Start; J < OpenExpressionBase; ++J)
                Args.push_back(R(J));
            Args.push_back(std::move(OpenExpression));
            OpenExpression.clear();
        }
        else if (int Base = OpenBaseAt(I.Pc); Base >= int(Start))
        {
            NeedsPack = true;
            for (unsigned J = Start; J < unsigned(Base); ++J)
                Args.push_back(R(J));
            Args.push_back("table.unpack(" + Token(PackSymbol) + ", 1, " + Token(PackSymbol) + ".n)");
        }
        else
        {
            // Open arguments consist of fixed registers followed by the most recent open result tuple.
            NeedsPack = true;
            NeedsDynamicTop = true;
            unsigned Arguments = E.New("CallArguments", Id, true);
            auto TupleIndex = Token(E.New("TupleIndex", Id, true, true));
            Emit("local " + Token(Arguments) + " = {}");
            // Dynamic prefixes are expanded by a statically generated choice, preserving nils.
            for (unsigned J = Start; J < P.Stack; ++J)
                Emit("if " + Token(TopSymbol) + " > " + std::to_string(J) + " then " + Token(Arguments) + "[" +
                     std::to_string(J - Start + 1) + "] = " + R(J) + " end");
            Emit("for " + TupleIndex + " = 1, " + Token(PackSymbol) + ".n do\n" + Indent() + Token(Arguments) + "[" + Token(TopSymbol) +
                 " - " + std::to_string(Start) + " + " + TupleIndex + "] = " + Token(PackSymbol) + "[" + TupleIndex + "]\nend");
            Args.push_back("table.unpack(" + Token(Arguments) + ", 1, " + Token(TopSymbol) + " - " + std::to_string(Start) + " + " +
                           Token(PackSymbol) + ".n)");
        }
        std::string Callable = Method ? PendingReceiver + ":" + PendingMethod : R(I.A);
        if (Method && !Identifier(PendingMethod))
            throw Error("NAMECALL contains a method name that cannot be expressed with colon syntax");
        std::string Call = Callable + "(" + Join(Args) + ")";
        std::string ResultHint;
        // Services and modules are conventionally bound to locals; other
        // inferred names only label a value that may still be inlined.
        bool Conventional = Method && PendingMethod == "GetService";
        if (Method && (PendingMethod == "GetService" || PendingMethod == "WaitForChild" || PendingMethod == "FindFirstChild") && I.B > 2)
            ResultHint = Hints[SymbolAt(Start, I.Pc)];
        else if (Method && (PendingMethod == "Connect" || PendingMethod == "Once"))
            ResultHint = "Connection";
        else if (Method && (PendingMethod == "Raycast" || PendingMethod == "Blockcast" || PendingMethod == "Spherecast"))
            ResultHint = "RaycastResult";
        else if (!Method)
        {
            auto CallableHint = Hints[SymbolAt(I.A, I.Pc)];
            if (CallableHint == "require")
            {
                ResultHint = I.B ? Hints[SymbolAt(I.A + 1, I.Pc)] : OpenHint;
                Conventional = true;
            }
            else if (CallableHint.ends_with(".new"))
                ResultHint = CallableHint.substr(0, CallableHint.size() - 4);
            if (CallableHint == "Instance.new" && I.B > 1 && !Hints[SymbolAt(Start, I.Pc)].empty())
                ResultHint = Hints[SymbolAt(Start, I.Pc)];
        }
        // A getter called with a single name-like string is named after it:
        // Data:Get("Coins"), getUserFlag("Flag"), FindFirstChildOfClass("Humanoid").
        if (ResultHint.empty() && E.Settings.InferCallNames && I.C == 2 && I.B == (Method ? 3u : 2u))
        {
            std::string Callee = Method ? PendingMethod : Hints[SymbolAt(I.A, I.Pc)];
            Callee = Callee.substr(Callee.rfind('.') == std::string::npos ? 0 : Callee.rfind('.') + 1);
            bool Getter = (Callee.starts_with("Get") || Callee.starts_with("get") || Callee.starts_with("Find") ||
                           Callee.starts_with("find")) &&
                          !Callee.ends_with("Signal") && !Callee.ends_with("Event") && Callee != "GetService";
            for (auto Pos = Index(I.Pc); Getter && Pos-- > 0;)
            {
                const auto &Load = P.Instructions[Pos];
                if (Load.Op == LOP_NAMECALL || Luau::isFastCall(Load.Op))
                    continue;
                if (Load.Op == LOP_LOADK && Load.A == Start && P.Constants[unsigned(Load.D)].Tag == LBC_CONSTANT_STRING)
                {
                    const auto &Text = P.Constants[unsigned(Load.D)].Text;
                    if (!Text.empty() && Text.size() <= 40 && std::isalpha(static_cast<unsigned char>(Text[0])) &&
                        std::all_of(Text.begin(), Text.end(), [](unsigned char C) { return std::isalnum(C) || C == '_' || C == ' '; }))
                        ResultHint = Text;
                }
                break;
            }
        }
        if (!ResultHint.empty())
            Hint(ResultHint, I.C == 2 && Conventional, E.Settings.InferCallNames);
        // `local self = setmetatable({}, Class)` is the constructor idiom.
        if (E.Settings.InferCallNames && !Method && Depth > 0 && I.C == 2 && I.B == 3 && Hints[SymbolAt(I.A, I.Pc)] == "setmetatable")
        {
            unsigned S = SymbolAt(I.A, I.Pc, true);
            if (!E.Symbols[S].Keep && !E.Symbols[S].Debug)
            {
                E.Rename(S, E.New("self", Id, false, false, false, true));
                Hints[S] = "self";
            }
        }
        if (E.Settings.InferCallNames && Method && PendingMethod == "GetService" && I.C == 2 && I.B == 3)
        {
            auto Previous = Index(I.Pc);
            if (Previous)
            {
                const auto &Load = P.Instructions[Previous - 1];
                if (Load.Op == LOP_NAMECALL && Previous > 1)
                    Previous--;
                const auto &Arg = P.Instructions[Previous - 1];
                if (Arg.Op == LOP_LOADK && P.Constants[unsigned(Arg.D)].Tag == LBC_CONSTANT_STRING)
                {
                    unsigned S = SymbolAt(I.A, I.Pc, true);
                    if (!E.Symbols[S].Keep)
                    {
                        auto New = E.New(P.Constants[unsigned(Arg.D)].Text, Id, true);
                        E.Rename(S, New);
                        E.Symbols[S].Keep = true;
                    }
                }
            }
        }
        PendingMethod.clear();
        PendingReceiver.clear();
        if (I.C)
            Many(I.A, I.C - 1, Call, !SingleResultBuiltin(I));
        else if (CanInlineOpen())
        {
            OpenExpression = Call;
            OpenExpressionBase = I.A;
            OpenHint = ResultHint;
        }
        else
        {
            NeedsPack = true;
            Emit(Token(PackSymbol) + " = table.pack(" + Call + ")");
            Emit(Token(TopSymbol) + " = " + std::to_string(I.A));
            // Fixed consumers can read the prefix of an open result tuple.
            for (unsigned J = I.A; J < P.Stack; ++J)
                Out.push_back(Assign(J, I, Token(PackSymbol) + "[" + std::to_string(J - I.A + 1) + "]", true));
        }
        break;
    }
    case LOP_RETURN:
    {
        std::vector<std::string> Values;
        if (I.B)
            for (unsigned J = 0; J + 1 < I.B; ++J)
                Values.push_back(R(I.A + J));
        else if (!OpenExpression.empty())
        {
            for (unsigned J = I.A; J < OpenExpressionBase; ++J)
                Values.push_back(R(J));
            Values.push_back(std::move(OpenExpression));
            OpenExpression.clear();
        }
        else if (int Base = OpenBaseAt(I.Pc); Base >= int(I.A))
        {
            NeedsPack = true;
            for (unsigned J = I.A; J < unsigned(Base); ++J)
                Values.push_back(R(J));
            Values.push_back("table.unpack(" + Token(PackSymbol) + ", 1, " + Token(PackSymbol) + ".n)");
        }
        else
        {
            NeedsPack = true;
            NeedsDynamicTop = true;
            unsigned ReturnValues = E.New("ReturnValues", Id, true);
            auto TupleIndex = Token(E.New("TupleIndex", Id, true, true));
            Emit("local " + Token(ReturnValues) + " = {}");
            for (unsigned J = I.A; J < P.Stack; ++J)
                Emit("if " + Token(TopSymbol) + " > " + std::to_string(J) + " then " + Token(ReturnValues) + "[" +
                     std::to_string(J - I.A + 1) + "] = " + R(J) + " end");
            Emit("for " + TupleIndex + " = 1, " + Token(PackSymbol) + ".n do\n" + Indent() + Token(ReturnValues) + "[" + Token(TopSymbol) +
                 " - " + std::to_string(I.A) + " + " + TupleIndex + "] = " + Token(PackSymbol) + "[" + TupleIndex + "]\nend");
            Values.push_back("table.unpack(" + Token(ReturnValues) + ", 1, " + Token(TopSymbol) + " - " + std::to_string(I.A) + " + " +
                             Token(PackSymbol) + ".n)");
        }
        Out.push_back({Kind::Return, {}, Join(Values)});
        break;
    }
    case LOP_ADD:
    case LOP_SUB:
    case LOP_MUL:
    case LOP_DIV:
    case LOP_MOD:
    case LOP_POW:
    case LOP_ADDK:
    case LOP_SUBK:
    case LOP_MULK:
    case LOP_DIVK:
    case LOP_MODK:
    case LOP_POWK:
    case LOP_AND:
    case LOP_OR:
    case LOP_ANDK:
    case LOP_ORK:
    case LOP_IDIV:
    case LOP_IDIVK:
    case LOP_SUBRK:
    case LOP_DIVRK:
    {
        std::string Left = R(I.B < P.Stack ? I.B : 0), Right, Op;
        static const char *Arithmetic[] = {"+", "-", "*", "/", "%", "^"};
        if (I.Op >= LOP_ADD && I.Op <= LOP_POW)
        {
            Op = Arithmetic[I.Op - LOP_ADD];
            Right = R(I.C);
        }
        else if (I.Op >= LOP_ADDK && I.Op <= LOP_POWK)
        {
            Op = Arithmetic[I.Op - LOP_ADDK];
            Right = ConstantAt(I.C);
        }
        else if (I.Op == LOP_IDIV || I.Op == LOP_IDIVK)
        {
            Op = "//";
            Right = I.Op == LOP_IDIV ? R(I.C) : ConstantAt(I.C);
        }
        else if (I.Op == LOP_SUBRK || I.Op == LOP_DIVRK)
        {
            Op = I.Op == LOP_SUBRK ? "-" : "/";
            Left = ConstantAt(I.B);
            Right = R(I.C);
        }
        else
        {
            Op = (I.Op == LOP_AND || I.Op == LOP_ANDK) ? "and" : "or";
            Right = (I.Op == LOP_ANDK || I.Op == LOP_ORK) ? ConstantAt(I.C) : R(I.C);
        }
        Set("(" + Left + " " + Op + " " + Right + ")");
        break;
    }
    case LOP_NOT:
        Set("(not " + R(I.B) + ")", true);
        break;
    case LOP_MINUS:
        Set("(-" + R(I.B) + ")");
        break;
    case LOP_LENGTH:
        Set("(#" + R(I.B) + ")");
        break;
    case LOP_CONCAT:
    {
        std::vector<std::string> Parts;
        for (unsigned J = I.B; J <= I.C; ++J)
            Parts.push_back(R(J));
        Set("(" + Join(Parts, " .. ") + ")");
        break;
    }
    case LOP_NEWTABLE:
    case LOP_DUPTABLE:
    {
        std::vector<std::pair<std::string, std::string>> Fields;
        if (I.Op == LOP_DUPTABLE)
            for (auto [Key, Value] : P.Constants.at(unsigned(I.D)).Entries)
            {
                const auto &Name = P.Constants.at(Key);
                Fields.emplace_back(Name.Tag == LBC_CONSTANT_STRING && Identifier(Name.Text) ? Name.Text : "[" + ConstantAt(Key) + "]",
                                    Value < 0 ? std::string() : ConstantAt(unsigned(Value)));
            }
        auto Node = Assign(I.A, I, TableText(Fields));
        if (Node.Type == Kind::Assign)
        {
            Node.Constructor = true;
            Node.Fields = std::move(Fields);
        }
        Out.push_back(std::move(Node));
        break;
    }
    case LOP_SETLIST:
    {
        auto Item = [&](unsigned Index, std::string Value, bool Expand = false)
        {
            Statement Store{Kind::Raw, {}, R(I.A) + "[" + std::to_string(Index) + "] = " + Value};
            Store.ListItem = true;
            Store.Expand = Expand;
            Out.push_back(std::move(Store));
        };
        if (I.C)
            for (unsigned J = 0; J + 1 < I.C; ++J)
                Item(I.Aux + J, R(I.B + J));
        else if (!OpenExpression.empty() && OpenExpressionBase >= I.B)
        {
            for (unsigned J = I.B; J < OpenExpressionBase; ++J)
                Item(I.Aux + J - I.B, R(J));
            Item(I.Aux + OpenExpressionBase - I.B, std::move(OpenExpression), true);
            OpenExpression.clear();
        }
        else if (int Base = OpenBaseAt(I.Pc); Base >= int(I.B))
        {
            NeedsPack = true;
            for (unsigned J = I.B; J < unsigned(Base); ++J)
                Emit(R(I.A) + "[" + std::to_string(I.Aux + J - I.B) + "] = " + R(J));
            auto TupleIndex = Token(E.New("TupleIndex", Id, true, true));
            Emit("for " + TupleIndex + " = 1, " + Token(PackSymbol) + ".n do\n" + Indent() + R(I.A) + "[" +
                 std::to_string(I.Aux + unsigned(Base) - I.B - 1) + " + " + TupleIndex + "] = " + Token(PackSymbol) + "[" + TupleIndex +
                 "]\nend");
        }
        else
        {
            NeedsPack = true;
            NeedsDynamicTop = true;
            auto TupleIndex = Token(E.New("TupleIndex", Id, true, true));
            for (unsigned J = I.B; J < P.Stack; ++J)
                Emit("if " + Token(TopSymbol) + " > " + std::to_string(J) + " then " + R(I.A) + "[" + std::to_string(I.Aux + J - I.B) +
                     "] = " + R(J) + " end");
            Emit("for " + TupleIndex + " = 1, " + Token(PackSymbol) + ".n do\n" + Indent() + R(I.A) + "[" + std::to_string(I.Aux) + " + " +
                 Token(TopSymbol) + " - " + std::to_string(I.B) + " + " + TupleIndex + " - 1] = " + Token(PackSymbol) + "[" + TupleIndex +
                 "]\nend");
        }
        break;
    }
    case LOP_GETVARARGS:
        if (!I.B && CanInlineOpen())
        {
            OpenExpression = "...";
            OpenExpressionBase = I.A;
            break;
        }
        // A fixed vararg load is an ordinary adjusted list: `local a, b = ...`.
        if (I.B > 1)
        {
            Many(I.A, I.B - 1, "...");
            break;
        }
        NeedsVarargs = true;
        if (I.B)
            Many(I.A, I.B - 1, "table.unpack(" + Token(VarargsSymbol) + ", 1, " + std::to_string(I.B - 1) + ")");
        else
        {
            NeedsPack = true;
            Emit(Token(PackSymbol) + " = " + Token(VarargsSymbol));
            Emit(Token(TopSymbol) + " = " + std::to_string(I.A));
            for (unsigned J = I.A; J < P.Stack; ++J)
                Out.push_back(Assign(J, I, Token(PackSymbol) + "[" + std::to_string(J - I.A + 1) + "]", true));
        }
        break;
    case LOP_NEWCLASS:
    case LOP_NEWCLASSMEMBER:
    case LOP_CONSTRUCT:
    case LOP_FINCONSTRUCT:
    case LOP_GETPRIVATEFIELD:
    case LOP_SETPRIVATEFIELD:
        throw Error("Experimental class bytecode is parsed, but stable Roblox has no equivalent source construct");
    default:
        throw Unstructured("unhandled control instruction " + OpcodeName(I.Op));
    }
}

} // namespace Astra::Detail
