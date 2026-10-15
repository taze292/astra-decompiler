#include "Astra/Decompiler.hpp"
#include "BatchFiles.hpp"
#include "WebSocketServer.hpp"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>

int main(int Argc, char **Argv)
{
    try
    {
        Astra::Options Settings;
        std::string Input, Output;
        bool Assembly = false;
        bool Server = false;
        bool Stats = false;
        bool Batch = false, Overwrite = false;
        unsigned Jobs = 1;
        std::string OutputDirectory;
        std::vector<std::string> Inputs;
        unsigned Port = 8877;
        for (int I = 1; I < Argc; ++I)
        {
            std::string Arg = Argv[I];
            auto Value = [&]() -> std::string
            {
                if (++I == Argc)
                    throw Astra::Error("Missing value for " + Arg);
                return Argv[I];
            };
            if (Arg == "--help" || Arg == "-h")
            {
                std::cout << "Astra Luau decompiler\nUsage: astra INPUT.luac [-o OUTPUT.luau] [options]\n\n"
                             "  --serve                      Start the local WebSocket bridge (Windows)\n"
                             "  --port N                     Bridge port (default: 8877)\n"
                             "  --encoding auto|plain|roblox   Decode opcode bytes (default: auto)\n"
                             "  --disassemble                Print bytecode instructions\n"
                             "  --state-machine              Opt into forced diagnostic state-machine output\n"
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
                             "  --no-method-syntax           Keep explicit self parameters\n"
                             "  --no-native-captures         Use reference boxes for all mutable captures\n"
                             "  --no-infer-members           Disable property-name hints\n"
                             "  --no-infer-calls             Disable constructor/service/result hints\n"
                             "  --local-prefix NAME          Anonymous local prefix (default Value)\n"
                             "  --argument-prefix NAME       Parameter prefix (default Argument)\n"
                             "  --compact-upvalues true      Use one-line capture comments (default: multiline)\n"
                             "  --no-upvalue-indexes         Hide capture indexes in either comment style\n"
                             "  --no-group-spacing           Omit statement-group blank lines\n"
                             "  --compact-arrays true        Shorten safe sequential array prefixes (default true)\n"
                             "  --use-tabs true              Indent with tabs instead of spaces\n"
                             "  --blank-lines-between-groups N  Group spacing (0..3; default 1)\n"
                             "  --space-inside-table-braces true  Pad inline table braces (default true)\n"
                             "  --optimization-passes N      Simplification passes (0..8; default 5)\n"
                             "  --fallback true              Permit diagnostic state-machine output (default false)\n"
                             "  --no-fallback                Reject state-machine output (default)\n"
                             "  --max-input-bytes N          Input cap (1..268435456)\n"
                             "  --max-output-bytes N         Source cap (1..268435456)\n"
                             "  --max-analysis-cells N       Per-function analysis cap (1..64000000)\n"
                             "  --max-analysis-work N        Per-request graph/dataflow work cap\n"
                             "  --max-nesting N              Closure nesting cap (1..128)\n"
                             "  --stats                      Print stage timings and counts to stderr\n"
                             "  --batch --output-dir DIR      Decompile multiple files in input order\n"
                             "  --jobs N                     Batch workers (0=auto, 1..32; default 1)\n"
                             "  --overwrite                  Allow replacing existing batch outputs\n"
                             "Boolean options also accept --KEY true|false. See docs/OPTIONS.md.\n";
                return 0;
            }
            if (Arg == "-o" || Arg == "--output")
                Output = Value();
            else if (Arg == "--batch")
                Batch = true;
            else if (Arg == "--overwrite")
                Overwrite = true;
            else if (Arg == "--output-dir")
                OutputDirectory = Value();
            else if (Arg == "--jobs")
            {
                auto Text = Value();
                auto [End, Error] = std::from_chars(Text.data(), Text.data() + Text.size(), Jobs);
                if (Error != std::errc{} || End != Text.data() + Text.size() || Jobs > 32)
                    throw Astra::Error("Jobs must be 0..32");
            }
            else if (Arg == "--disassemble")
                Assembly = true;
            else if (Arg == "--serve")
                Server = true;
            else if (Arg == "--stats")
                Stats = true;
            else if (Arg == "--no-filtergc")
                Settings.IncludeFunctionLocators = false;
            else if (Arg == "--state-machine")
            {
                if (I + 1 < Argc && (std::string_view(Argv[I + 1]) == "true" || std::string_view(Argv[I + 1]) == "false"))
                    Astra::ApplyOption(Settings, "state-machine", Value());
                else
                    Settings.ForceStateMachine = true;
                if (Settings.ForceStateMachine)
                    Settings.AllowFallback = true;
            }
            else if (Arg == "--port")
            {
                auto Text = Value();
                std::size_t Used = 0;
                auto Number = std::stoul(Text, &Used);
                if (Used != Text.size() || Number == 0 || Number > 65535)
                    throw Astra::Error("Invalid port");
                Port = unsigned(Number);
            }
            else if (Arg.starts_with("--no-"))
            {
                static const std::set<std::string> Flags = {"header",           "lines",
                                                            "upvalues",         "locators",
                                                            "gc-locators",      "require-locators",
                                                            "type-names",       "preserve-function-names",
                                                            "fallback",         "state-machine",
                                                            "method-syntax",    "native-captures",
                                                            "infer-members",    "infer-calls",
                                                            "compact-upvalues", "upvalue-indexes",
                                                            "group-spacing",    "compact-arrays",
                                                            "use-tabs",         "space-inside-table-braces"};
                auto Key = Arg.substr(5);
                if (!Flags.count(Key))
                    throw Astra::Error("Unknown flag " + Arg);
                Astra::ApplyOption(Settings, Key, "false");
            }
            else if (Arg.starts_with("--"))
                Astra::ApplyOption(Settings, Arg.substr(2), Value());
            else if (Arg.starts_with('-'))
                throw Astra::Error("Unknown option " + Arg);
            else
                Inputs.push_back(Arg);
        }
        Astra::ValidateOptions(Settings);
        if (Batch)
        {
            if (Server || Assembly || Stats || !Output.empty())
                throw Astra::Error("--batch cannot be combined with --serve, --disassemble, --stats, or -o");
            return Astra::DecompileFiles(Inputs, OutputDirectory, Settings, Jobs, Overwrite);
        }
        if (!OutputDirectory.empty() || Overwrite || Jobs != 1)
            throw Astra::Error("Batch options require --batch");
        if (Inputs.size() > 1)
            throw Astra::Error("Multiple inputs require --batch");
        if (!Inputs.empty())
            Input = Inputs.front();
        if (Server)
        {
            if (!Input.empty() || !Output.empty() || Assembly || Stats)
                throw Astra::Error("--serve cannot be combined with file input/output, --disassemble, or --stats");
            return Astra::ServeWebSocket(Port, Settings);
        }
        if (Input.empty())
            throw Astra::Error("Usage: astra INPUT.luac [-o OUTPUT.luau]; see --help");
        if (std::filesystem::file_size(Input) > Settings.MaxInputBytes)
            throw Astra::Error("Input exceeds configured byte limit");
        std::ifstream File(Input, std::ios::binary);
        if (!File)
            throw Astra::Error("Cannot open input: " + Input);
        std::string Bytes((std::istreambuf_iterator<char>(File)), {});
        std::string Source;
        if (Assembly)
            Source = Astra::Disassemble(Bytes, Settings);
        else
        {
            auto Result = Astra::Decompile(Bytes, Settings);
            if (Stats)
                std::cerr << "astra: decode_ms=" << Result.Stats.DecodeMilliseconds
                          << " reconstruct_ms=" << Result.Stats.ReconstructionMilliseconds
                          << " finalize_ms=" << Result.Stats.FinalizationMilliseconds << " instructions=" << Result.Stats.InstructionCount
                          << " symbols=" << Result.Stats.SymbolCount << " analysis_work=" << Result.Stats.AnalysisWork
                          << " fallbacks=" << Result.StateMachineFunctions << '\n';
            Source = std::move(Result.Source);
            for (const auto &Diagnostic : Result.Diagnostics)
                std::cerr << "astra: " << Diagnostic << '\n';
        }
        if (Output.empty())
            std::cout << Source;
        else
        {
            if (std::filesystem::exists(Output) && std::filesystem::equivalent(Input, Output))
                throw Astra::Error("Input and output must be different files");
            std::ofstream Out(Output, std::ios::binary);
            if (!Out || !Out.write(Source.data(), std::streamsize(Source.size())))
                throw Astra::Error("Cannot write output: " + Output);
        }
        return 0;
    }
    catch (const std::exception &Error)
    {
        std::cerr << "astra: " << Error.what() << '\n';
        return 1;
    }
}
