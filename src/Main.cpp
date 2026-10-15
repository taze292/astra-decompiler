#include "Taze/Decompiler.hpp"
#include "WebSocketServer.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>

int main(int Argc, char **Argv)
{
    try
    {
        Taze::Options Settings;
        std::string Input, Output;
        bool Assembly = false;
        bool Server = false;
        bool Stats = false;
        unsigned Port = 8877;
        for (int I = 1; I < Argc; ++I)
        {
            std::string Arg = Argv[I];
            auto Value = [&]() -> std::string
            {
                if (++I == Argc)
                    throw Taze::Error("Missing value for " + Arg);
                return Argv[I];
            };
            if (Arg == "--help" || Arg == "-h")
            {
                std::cout << "Taze Luau decompiler\nUsage: taze INPUT.luac [-o OUTPUT.luau] [options]\n\n"
                             "  --serve                      Start the local WebSocket bridge (Windows)\n"
                             "  --port N                     Bridge port (default: 8877)\n"
                             "  --encoding auto|plain|roblox   Decode opcode bytes (default: auto)\n"
                             "  --disassemble                Print bytecode instructions\n"
                             "  --state-machine              Force explicit control-flow lowering\n"
                             "  --indent N                   Spaces per indentation level (1..16)\n"
                             "  --no-header                  Omit generator header\n"
                             "  --no-lines                   Omit function line comments\n"
                             "  --no-filtergc                Omit inline function lookup expressions\n"
                             "  --filter-line Line|StartLine  Select the executor's filtergc line field\n"
                             "  --module-path EXPR           Module Instance expression for require lookups\n"
                             "  --no-upvalues                Omit capture comments\n"
                             "  --casing pascal|camel|snake|preserve\n"
                             "  --naming inferred|debug|generic\n"
                             "  --no-gc-locators             Omit filtergc lookups only\n"
                             "  --no-require-locators        Omit require lookups only\n"
                             "  --no-type-names              Use ValueN for anonymous locals\n"
                             "  --no-preserve-function-names Apply casing to function bindings too\n"
                             "  --optimization-passes N      Simplification passes (0..8; default 5)\n"
                             "  --no-fallback                Fail if structured output is unavailable\n"
                             "  --max-input-bytes N          Input cap (1..268435456)\n"
                             "  --max-output-bytes N         Source cap (1..268435456)\n"
                             "  --max-analysis-cells N       Per-function analysis cap (1..64000000)\n"
                             "  --max-nesting N              Closure nesting cap (1..128)\n"
                             "  --stats                      Print stage timings and counts to stderr\n"
                             "Boolean options also accept --KEY true|false. See docs/OPTIONS.md.\n";
                return 0;
            }
            if (Arg == "-o" || Arg == "--output")
                Output = Value();
            else if (Arg == "--disassemble")
                Assembly = true;
            else if (Arg == "--serve")
                Server = true;
            else if (Arg == "--stats")
                Stats = true;
            else if (Arg == "--no-filtergc")
                Settings.IncludeFunctionLocators = false;
            else if (Arg == "--state-machine")
                Settings.ForceStateMachine = true;
            else if (Arg == "--port")
            {
                auto Text = Value();
                std::size_t Used = 0;
                auto Number = std::stoul(Text, &Used);
                if (Used != Text.size() || Number == 0 || Number > 65535)
                    throw Taze::Error("Invalid port");
                Port = unsigned(Number);
            }
            else if (Arg.starts_with("--no-"))
            {
                static const std::set<std::string> Flags = {"header", "lines", "upvalues", "locators", "gc-locators", "require-locators", "type-names", "preserve-function-names", "fallback"};
                auto Key = Arg.substr(5);
                if (!Flags.count(Key)) throw Taze::Error("Unknown flag " + Arg);
                Taze::ApplyOption(Settings, Key, "false");
            }
            else if (Arg.starts_with("--"))
                Taze::ApplyOption(Settings, Arg.substr(2), Value());
            else if (Arg.starts_with('-'))
                throw Taze::Error("Unknown option " + Arg);
            else if (Input.empty())
                Input = Arg;
            else
                throw Taze::Error("Expected exactly one input file");
        }
        Taze::ValidateOptions(Settings);
        if (Server)
        {
            if (!Input.empty() || !Output.empty() || Assembly)
                throw Taze::Error("--serve cannot be combined with file input/output or --disassemble");
            return Taze::ServeWebSocket(Port, Settings);
        }
        if (Input.empty())
            throw Taze::Error("Usage: taze INPUT.luac [-o OUTPUT.luau]; see --help");
        if (std::filesystem::file_size(Input) > Settings.MaxInputBytes)
            throw Taze::Error("Input exceeds configured byte limit");
        std::ifstream File(Input, std::ios::binary);
        if (!File)
            throw Taze::Error("Cannot open input: " + Input);
        std::string Bytes((std::istreambuf_iterator<char>(File)), {});
        std::string Source;
        if (Assembly)
            Source = Taze::Disassemble(Bytes, Settings);
        else
        {
            auto Result = Taze::Decompile(Bytes, Settings);
            if (Stats)
                std::cerr << "taze: decode_ms=" << Result.Stats.DecodeMilliseconds
                          << " reconstruct_ms=" << Result.Stats.ReconstructionMilliseconds
                          << " finalize_ms=" << Result.Stats.FinalizationMilliseconds
                          << " instructions=" << Result.Stats.InstructionCount << " symbols=" << Result.Stats.SymbolCount
                          << " fallbacks=" << Result.StateMachineFunctions << '\n';
            Source = std::move(Result.Source);
            for (const auto &Diagnostic : Result.Diagnostics)
                std::cerr << "taze: " << Diagnostic << '\n';
        }
        if (Output.empty())
            std::cout << Source;
        else
        {
            if (std::filesystem::exists(Output) && std::filesystem::equivalent(Input, Output))
                throw Taze::Error("Input and output must be different files");
            std::ofstream Out(Output, std::ios::binary);
            if (!Out || !Out.write(Source.data(), std::streamsize(Source.size())))
                throw Taze::Error("Cannot write output: " + Output);
        }
        return 0;
    }
    catch (const std::exception &Error)
    {
        std::cerr << "taze: " << Error.what() << '\n';
        return 1;
    }
}
