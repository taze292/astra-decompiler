#include "Reconstruction.hpp"

namespace Taze::Detail
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
        return "vector.create(" + Number(K.Vector[0]) + ", " + Number(K.Vector[1]) + ", " + Number(K.Vector[2]) + ")";
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

std::string Function::Signature() const
{
    std::vector<std::string> Args;
    for (auto S : Parameters)
        Args.push_back(Token(S));
    if (P.Vararg)
        Args.push_back("...");
    return "(" + Join(Args) + ")";
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
            Argument = U.Reference ? U.Cell : U.Value;
        }
        else
        {
            unsigned S = SymbolAt(R, I.Pc, R == I.A);
            U.Name = E.Symbols[S].Name;
            U.Reference = Kind == LCT_REF;
            Argument = U.Reference ? Token(Registers[R]) : Read(R, I.Pc);
        }
        if (Kind == LCT_VAL && R == I.A)
        {
            if (Self.empty())
            {
                auto Destination = SymbolAt(I.A, I.Pc, true);
                unsigned Definitions = E.Symbols[Destination].Parameter ? 1u : 0u;
                for (const auto &Code : P.Instructions)
                    for (auto Reg : Writes(Code, P))
                        if (SymbolAt(Reg, Code.Pc, true) == Destination)
                            ++Definitions;
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
            unsigned Definitions = E.Symbols[S].Parameter ? 1u : 0u;
            for (const auto &Code : P.Instructions)
                for (unsigned Reg : Writes(Code, P))
                    if (SymbolAt(Reg, Code.Pc, true) == S)
                        Definitions++;
            bool Loop = false;
            for (const auto &Code : P.Instructions)
                if (Code.Target() >= 0 && Code.Target() <= int(I.Pc) && Code.Pc >= I.Pc && Code.Target() < int(Code.Pc))
                    Loop = true;
            if (Definitions == 1 && !Loop)
            {
                U.Value = Read(R, I.Pc);
                U.Cell.clear();
                E.Symbols[S].Keep = true;
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
            E.Symbols[Nested.Parameters.front()].Base = "Self";
            E.Symbols[Nested.Parameters.front()].Original = "Self";
        }
    }
    std::string Body = Nested.Generate();
    std::string Text = "function" + Nested.Signature();
    if (E.Settings.IncludeLineComments)
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
    auto R = [&](unsigned Reg) { return Read(Reg, I.Pc); };
    auto Emit = [&](std::string Text) { Out.push_back({Kind::Raw, {}, std::move(Text)}); };
    auto Set = [&](std::string Text, bool Pure = false, bool Call = false) { Out.push_back(Assign(I.A, I, std::move(Text), Pure, Call)); };
    auto Hint = [&](std::string Name, bool Keep = false)
    {
        unsigned S = SymbolAt(I.A, I.Pc, true);
        Hints[S] = Name;
        if (!Name.empty() && !E.Symbols[S].Keep)
        {
            auto Nice = E.New(Name, Id, Keep);
            E.Rename(S, Nice);
            E.Symbols[S].Keep = Keep;
        }
    };
    auto Many = [&](unsigned Base, unsigned Count, const std::string &Text)
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
            Out.push_back({Kind::Assign, std::move(Lhs), Text, {}, {}, false, true});
    };
    auto CanInlineOpen = [&]()
    {
        auto Next = Index(I.Next);
        if (Dispatch || Next >= P.Instructions.size())
            return false;
        const auto &Consumer = P.Instructions[Next];
        return ((Consumer.Op == LOP_CALL || Consumer.Op == LOP_CALLFB) && !Consumer.B && Consumer.A < I.A) ||
               (Consumer.Op == LOP_RETURN && !Consumer.B && Consumer.A <= I.A);
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
        Set(Path);
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
        Hint(Name == "new" ? Hints[SymbolAt(I.B, I.Pc)] + ".new" : Name);
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
            (E.Symbols[S].Name.starts_with("Value") || (Identifier(FunctionName) && E.Symbols[S].Base == Pascal(FunctionName))))
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
        else
        {
            // Open arguments consist of fixed registers followed by the most recent open result tuple.
            NeedsPack = true;
            unsigned Arguments = E.New("CallArguments", Id, true);
            auto TupleIndex = Token(E.New("TupleIndex", Id, true, true));
            Emit("local " + Token(Arguments) + " = {}");
            // Dynamic prefixes are expanded by a statically generated choice, preserving nils.
            for (unsigned J = Start; J < P.Stack; ++J)
                Emit("if " + Token(TopSymbol) + " > " + std::to_string(J) + " then " + Token(Arguments) + "[" +
                     std::to_string(J - Start + 1) + "] = " + R(J) + " end");
            Emit("for " + TupleIndex + " = 1, " + Token(PackSymbol) + ".n do\n    " + Token(Arguments) + "[" + Token(TopSymbol) + " - " +
                 std::to_string(Start) + " + " + TupleIndex + "] = " + Token(PackSymbol) + "[" + TupleIndex + "]\nend");
            Args.push_back("table.unpack(" + Token(Arguments) + ", 1, " + Token(TopSymbol) + " - " + std::to_string(Start) + " + " +
                           Token(PackSymbol) + ".n)");
        }
        std::string Callable = Method ? PendingReceiver + ":" + PendingMethod : R(I.A);
        if (Method && !Identifier(PendingMethod))
            throw Error("NAMECALL contains a method name that cannot be expressed with colon syntax");
        std::string Call = Callable + "(" + Join(Args) + ")";
        std::string ResultHint;
        if (Method && (PendingMethod == "GetService" || PendingMethod == "WaitForChild" || PendingMethod == "FindFirstChild") && I.B > 2)
            ResultHint = Hints[SymbolAt(Start, I.Pc)];
        else if (!Method)
        {
            auto CallableHint = Hints[SymbolAt(I.A, I.Pc)];
            if (CallableHint == "require")
                ResultHint = I.B ? Hints[SymbolAt(I.A + 1, I.Pc)] : OpenHint;
            else if (CallableHint.ends_with(".new"))
                ResultHint = CallableHint.substr(0, CallableHint.size() - 4);
        }
        if (!ResultHint.empty())
            Hint(ResultHint, I.C == 2);
        if (Method && PendingMethod == "GetService" && I.C == 2 && I.B == 3)
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
            Many(I.A, I.C - 1, Call);
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
                Out.push_back(Assign(J, I, Token(PackSymbol) + "[" + std::to_string(J - I.A + 1) + "]"));
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
        else
        {
            NeedsPack = true;
            unsigned ReturnValues = E.New("ReturnValues", Id, true);
            auto TupleIndex = Token(E.New("TupleIndex", Id, true, true));
            Emit("local " + Token(ReturnValues) + " = {}");
            for (unsigned J = I.A; J < P.Stack; ++J)
                Emit("if " + Token(TopSymbol) + " > " + std::to_string(J) + " then " + Token(ReturnValues) + "[" +
                     std::to_string(J - I.A + 1) + "] = " + R(J) + " end");
            Emit("for " + TupleIndex + " = 1, " + Token(PackSymbol) + ".n do\n    " + Token(ReturnValues) + "[" + Token(TopSymbol) + " - " +
                 std::to_string(I.A) + " + " + TupleIndex + "] = " + Token(PackSymbol) + "[" + TupleIndex + "]\nend");
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
        Set("{}");
        break;
    case LOP_DUPTABLE:
        Set(ConstantAt(unsigned(I.D)));
        break;
    case LOP_SETLIST:
        if (I.C)
            for (unsigned J = 0; J + 1 < I.C; ++J)
                Emit(R(I.A) + "[" + std::to_string(I.Aux + J) + "] = " + R(I.B + J));
        else
        {
            NeedsPack = true;
            auto TupleIndex = Token(E.New("TupleIndex", Id, true, true));
            for (unsigned J = I.B; J < P.Stack; ++J)
                Emit("if " + Token(TopSymbol) + " > " + std::to_string(J) + " then " + R(I.A) + "[" + std::to_string(I.Aux + J - I.B) +
                     "] = " + R(J) + " end");
            Emit("for " + TupleIndex + " = 1, " + Token(PackSymbol) + ".n do\n    " + R(I.A) + "[" + std::to_string(I.Aux) + " + " +
                 Token(TopSymbol) + " - " + std::to_string(I.B) + " + " + TupleIndex + " - 1] = " + Token(PackSymbol) + "[" + TupleIndex +
                 "]\nend");
        }
        break;
    case LOP_GETVARARGS:
        if (!I.B && CanInlineOpen())
        {
            OpenExpression = "...";
            OpenExpressionBase = I.A;
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
                Out.push_back(Assign(J, I, Token(PackSymbol) + "[" + std::to_string(J - I.A + 1) + "]"));
        }
        break;
    case LOP_NEWCLASS:
    case LOP_NEWCLASSMEMBER:
        throw Error("Experimental class bytecode is parsed, but stable Roblox has no equivalent source construct");
    default:
        throw Unstructured("unhandled control instruction " + OpcodeName(I.Op));
    }
}


} // namespace Taze::Detail
