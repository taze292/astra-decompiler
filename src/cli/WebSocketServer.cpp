#include "WebSocketServer.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bcrypt.h>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <syncstream>
#include <thread>
#include <vector>

namespace Astra
{
namespace
{
struct Socket
{
    SOCKET Value;
    explicit Socket(SOCKET Value) : Value(Value) {}
    ~Socket()
    {
        if (Value != INVALID_SOCKET)
            closesocket(Value);
    }
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
};

void Read(SOCKET Socket, char *Data, std::size_t Size)
{
    while (Size)
    {
        int Count = recv(Socket, Data, int(std::min<std::size_t>(Size, 65536)), 0);
        if (Count <= 0)
            throw Error("WebSocket disconnected or receive timed out");
        Data += Count;
        Size -= Count;
    }
}

void Send(SOCKET Socket, std::string_view Data)
{
    while (!Data.empty())
    {
        int Count = send(Socket, Data.data(), int(std::min<std::size_t>(Data.size(), 65536)), 0);
        if (Count <= 0)
            throw Error("WebSocket send failed");
        Data.remove_prefix(Count);
    }
}

std::string Base64(std::string_view Data)
{
    constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string Result;
    for (std::size_t I = 0; I < Data.size(); I += 3)
    {
        unsigned Bits = unsigned(static_cast<unsigned char>(Data[I])) << 16;
        if (I + 1 < Data.size())
            Bits |= unsigned(static_cast<unsigned char>(Data[I + 1])) << 8;
        if (I + 2 < Data.size())
            Bits |= static_cast<unsigned char>(Data[I + 2]);
        Result += Alphabet[(Bits >> 18) & 63];
        Result += Alphabet[(Bits >> 12) & 63];
        Result += I + 1 < Data.size() ? Alphabet[(Bits >> 6) & 63] : '=';
        Result += I + 2 < Data.size() ? Alphabet[Bits & 63] : '=';
    }
    return Result;
}

std::string AcceptKey(std::string Key)
{
    Key += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    BCRYPT_ALG_HANDLE Algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0)
        throw Error("Cannot initialize WebSocket SHA-1");
    std::array<unsigned char, 20> Hash{};
    auto Status =
        BCryptHash(Algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(Key.data()), ULONG(Key.size()), Hash.data(), ULONG(Hash.size()));
    BCryptCloseAlgorithmProvider(Algorithm, 0);
    if (Status < 0)
        throw Error("Cannot hash WebSocket handshake");
    return Base64({reinterpret_cast<const char *>(Hash.data()), Hash.size()});
}

std::string Lower(std::string Text)
{
    for (auto &C : Text)
        C = char(std::tolower(static_cast<unsigned char>(C)));
    return Text;
}

std::string Trim(std::string Text)
{
    auto Start = Text.find_first_not_of(" \t\r\n");
    if (Start == std::string::npos)
        return {};
    return Text.substr(Start, Text.find_last_not_of(" \t\r\n") - Start + 1);
}

bool HasToken(const std::string &Header, const std::string &Token)
{
    std::istringstream Input(Lower(Header));
    std::string Part;
    while (std::getline(Input, Part, ','))
        if (Trim(Part) == Token)
            return true;
    return false;
}

void Handshake(SOCKET Client)
{
    std::string Header;
    while (!Header.ends_with("\r\n\r\n"))
    {
        if (Header.size() >= 8192)
            throw Error("WebSocket handshake exceeds 8 KiB");
        char C;
        Read(Client, &C, 1);
        Header += C;
    }
    std::istringstream Input(Header);
    std::string Line;
    std::getline(Input, Line);
    if (Line != "GET /decompile HTTP/1.1\r")
        throw Error("Expected GET /decompile HTTP/1.1");
    std::map<std::string, std::string> Fields;
    while (std::getline(Input, Line) && Line != "\r")
    {
        auto Colon = Line.find(':');
        if (Colon == std::string::npos)
            throw Error("Malformed handshake header");
        auto Name = Lower(Trim(Line.substr(0, Colon)));
        if (!Fields.emplace(Name, Trim(Line.substr(Colon + 1))).second)
            throw Error("Duplicate handshake header");
    }
    auto Key = Fields["sec-websocket-key"];
    bool ValidKey = Key.size() == 24 && Key.ends_with("==") &&
                    std::all_of(Key.begin(), Key.begin() + 22, [](unsigned char C) { return std::isalnum(C) || C == '+' || C == '/'; });
    if (!ValidKey || Fields["sec-websocket-version"] != "13" || Lower(Fields["upgrade"]) != "websocket" ||
        !HasToken(Fields["connection"], "upgrade") || Fields["host"].empty())
        throw Error("Invalid WebSocket upgrade");
    // Native executors may send their ws:// loopback origin; ordinary browser pages use http(s):// origins.
    if (Fields.count("origin") && Fields["origin"] != "ws://" + Fields["host"])
        throw Error("Browser-origin connections are not supported");
    Send(Client, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
                     AcceptKey(Key) + "\r\n\r\n");
}

void Frame(SOCKET Client, unsigned Opcode, std::string_view Payload)
{
    std::string Header(1, char(0x80 | Opcode));
    auto Size = std::uint64_t(Payload.size());
    if (Size < 126)
        Header += char(Size);
    else
    {
        unsigned Bytes = Size <= 65535 ? 2 : 8;
        Header += char(Bytes == 2 ? 126 : 127);
        for (unsigned I = Bytes; I-- > 0;)
            Header += char(Size >> (I * 8));
    }
    Send(Client, Header);
    Send(Client, Payload);
}

// Share CPU workers across connections. The receive loop must stay available for
// other requests, pings and disconnects while a script is being reconstructed.
class Jobs
{
    std::mutex Mutex;
    std::condition_variable Ready;
    std::deque<std::pair<std::size_t, std::function<void()>>> Queue;
    std::vector<std::thread> Workers;
    std::size_t Bytes = 0;
    std::size_t Count = 0;
    bool Stopping = false;

public:
    Jobs()
    {
        auto ThreadCount = std::clamp(std::thread::hardware_concurrency(), 1u, 16u);
        try
        {
            for (unsigned I = 0; I < ThreadCount; ++I)
                Workers.emplace_back([this]()
                {
                    for (;;)
                    {
                        std::pair<std::size_t, std::function<void()>> Job;
                        {
                            std::unique_lock Lock(Mutex);
                            Ready.wait(Lock, [&]() { return Stopping || !Queue.empty(); });
                            if (Stopping && Queue.empty())
                                return;
                            Job = std::move(Queue.front());
                            Queue.pop_front();
                        }
                        Job.second();
                        {
                            std::lock_guard Lock(Mutex);
                            Bytes -= Job.first;
                            --Count;
                        }
                    }
                });
        }
        catch (...)
        {
            {
                std::lock_guard Lock(Mutex);
                Stopping = true;
            }
            Ready.notify_all();
            for (auto &Worker : Workers)
                Worker.join();
            throw;
        }
    }
    ~Jobs()
    {
        {
            std::lock_guard Lock(Mutex);
            Stopping = true;
        }
        Ready.notify_all();
        for (auto &Worker : Workers)
            Worker.join();
    }
    bool Submit(std::size_t Size, std::function<void()> Job)
    {
        std::lock_guard Lock(Mutex);
        // Account for running jobs too; one connection cannot allocate an
        // unbounded backlog by sending faster than the decompiler can run.
        if (Stopping || Count >= 128 || Size > 256 * 1024 * 1024 - Bytes)
            return false;
        Queue.emplace_back(Size, std::move(Job));
        Bytes += Size;
        ++Count;
        Ready.notify_one();
        return true;
    }
};

struct SessionState
{
    Socket Client;
    std::mutex SendMutex;
    std::mutex PendingMutex;
    std::set<std::string> Pending;
    std::atomic<bool> Closed = false;

    explicit SessionState(SOCKET Client) : Client(Client) {}
    void Close()
    {
        if (!Closed.exchange(true))
            shutdown(Client.Value, SD_BOTH);
    }
    void SendFrame(unsigned Opcode, std::string_view Payload)
    {
        std::lock_guard Lock(SendMutex);
        if (!Closed)
            Frame(Client.Value, Opcode, Payload);
    }
};

std::string RequestId(std::string_view Request)
{
    auto Newline = Request.find('\n');
    if (Newline == std::string_view::npos || Newline == 0 || Newline > 16 ||
        !std::all_of(Request.begin(), Request.begin() + Newline, [](unsigned char C) { return C >= '0' && C <= '9'; }))
        throw Error("Expected numeric request ID followed by bytecode");
    return std::string(Request.substr(0, Newline));
}

// Restricted JSON options object: keys and string enum values use ASCII words,
// booleans and unsigned integers keep their types. No executable Luau is parsed.
void JsonOptions(Options &Settings, std::string_view Text)
{
    if (Text.size() > 4096)
        throw Error("Option block exceeds 4096 bytes");
    std::size_t Pos = 0;
    auto Space = [&]()
    {
        while (Pos < Text.size() && (Text[Pos] == ' ' || Text[Pos] == '\t'))
            ++Pos;
    };
    auto Expect = [&](char C)
    {
        Space();
        if (Pos >= Text.size() || Text[Pos++] != C)
            throw Error("Invalid JSON option object");
    };
    auto Word = [&]()
    {
        auto Begin = Pos;
        while (Pos < Text.size() && ((Text[Pos] >= 'a' && Text[Pos] <= 'z') || (Text[Pos] >= 'A' && Text[Pos] <= 'Z') ||
                                     (Text[Pos] >= '0' && Text[Pos] <= '9') || Text[Pos] == '-' || Text[Pos] == '_'))
            ++Pos;
        if (Pos == Begin)
            throw Error("Invalid JSON option value");
        return Text.substr(Begin, Pos - Begin);
    };
    std::set<std::string> Seen;
    Expect('{');
    Space();
    if (Pos < Text.size() && Text[Pos] != '}')
        while (true)
        {
            Expect('"');
            auto Key = CanonicalOptionKey(Word());
            Expect('"');
            Expect(':');
            Space();
            bool Quoted = Pos < Text.size() && Text[Pos] == '"';
            if (Quoted)
                ++Pos;
            auto Value = Word();
            if (Quoted)
                Expect('"');
            else if (Value != "true" && Value != "false" &&
                     (Value.empty() || !std::all_of(Value.begin(), Value.end(), [](char C) { return C >= '0' && C <= '9'; }) ||
                      (Value.size() > 1 && Value.front() == '0')))
                throw Error("Invalid JSON scalar");
            if (Key == "module-path" || Key == "filter-line" || !Seen.insert(std::string(Key)).second)
                throw Error("Duplicate or reserved bridge option");
            ApplyOption(Settings, Key, Value);
            Space();
            if (Pos >= Text.size() || Text[Pos] != ',')
                break;
            ++Pos;
        }
    Expect('}');
    Space();
    if (Pos != Text.size())
        throw Error("Trailing JSON option data");
}

std::string Process(std::string_view Request, const Options &Settings)
{
    std::string Id = RequestId(Request);
    Request.remove_prefix(Id.size() + 1);
    try
    {
        Options RequestSettings = Settings;
        auto Digit = [](char C) -> unsigned
        {
            if (C >= '0' && C <= '9')
                return unsigned(C - '0');
            if (C >= 'a' && C <= 'f')
                return unsigned(C - 'a' + 10);
            if (C >= 'A' && C <= 'F')
                return unsigned(C - 'A' + 10);
            throw Error("Bytecode must be hex encoded");
        };
        auto Decode = [&](std::string_view Hex, std::size_t Limit)
        {
            if (Hex.size() % 2 || Hex.size() / 2 > Limit)
                throw Error("Invalid hex payload length");
            std::string Bytes(Hex.size() / 2, '\0');
            for (std::size_t I = 0; I < Bytes.size(); ++I)
                Bytes[I] = char((Digit(Hex[I * 2]) << 4) | Digit(Hex[I * 2 + 1]));
            return Bytes;
        };
        bool Binary = Request.starts_with("ASTRA5\n");
        bool Json = Binary || Request.starts_with("ASTRA4\n");
        bool HasOptions = Json || Request.starts_with("ASTRA3\n");
        if (HasOptions || Request.starts_with("ASTRA2\n"))
        {
            Request.remove_prefix(Request.find('\n') + 1);
            auto Line = [&]()
            {
                auto End = Request.find('\n');
                if (End == std::string_view::npos)
                    throw Error("Incomplete bridge metadata");
                auto Part = Request.substr(0, End);
                Request.remove_prefix(End + 1);
                return Part;
            };
            RequestSettings.FilterLineField = std::string(Line());
            RequestSettings.ModulePath = Decode(Line(), 8192);
            if (HasOptions)
            {
                auto Block = Json ? std::string(Line()) : Decode(Line(), 4096);
                if (Json)
                    JsonOptions(RequestSettings, Block);
                std::set<std::string> Seen;
                std::string_view Remaining = Json ? std::string_view{} : Block;
                while (!Remaining.empty())
                {
                    auto End = Remaining.find(';');
                    auto Entry = Remaining.substr(0, End);
                    auto Equals = Entry.find('=');
                    if (Equals == std::string_view::npos || Equals == 0 || Equals + 1 == Entry.size())
                        throw Error("Invalid bridge option entry");
                    auto Key = CanonicalOptionKey(Entry.substr(0, Equals));
                    if (Key == "module-path" || Key == "filter-line" || !Seen.insert(std::string(Key)).second)
                        throw Error("Duplicate or reserved bridge option");
                    ApplyOption(RequestSettings, Key, Entry.substr(Equals + 1));
                    if (End == std::string_view::npos)
                        break;
                    Remaining.remove_prefix(End + 1);
                    if (Remaining.empty())
                        throw Error("Empty trailing bridge option");
                }
                // Remote requests may lower the host's resource caps, never raise them.
                if (RequestSettings.MaxInputBytes > Settings.MaxInputBytes || RequestSettings.MaxOutputBytes > Settings.MaxOutputBytes ||
                    RequestSettings.MaxAnalysisCells > Settings.MaxAnalysisCells || RequestSettings.MaxNesting > Settings.MaxNesting ||
                    RequestSettings.MaxAnalysisWork > Settings.MaxAnalysisWork)
                    throw Error("Request options exceed server resource limits");
            }
        }
        if (Request.empty())
            throw Error("Empty bytecode payload");
        ValidateOptions(RequestSettings);
        if (Binary && Request.size() > RequestSettings.MaxInputBytes)
            throw Error("Bytecode exceeds input byte limit");
        auto Bytes = Binary ? std::string(Request) : Decode(Request, RequestSettings.MaxInputBytes);
        auto Result = Decompile(Bytes, RequestSettings);
        std::osyncstream(std::cout) << "Request " << Id << ": " << Bytes.size() << " bytecode bytes -> " << Result.Source.size()
                                    << " source bytes, " << Result.StateMachineFunctions << " fallback functions\n";
        return Id + "\nok\n" + Result.Source;
    }
    catch (const std::exception &Failure)
    {
        return Id + "\nerror\n" + Failure.what();
    }
}

void Session(const std::shared_ptr<SessionState> &State, const Options &Settings, Jobs &Scheduler)
{
    SOCKET Client = State->Client.Value;
    DWORD Timeout = 10000;
    setsockopt(Client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&Timeout), sizeof(Timeout));
    setsockopt(Client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&Timeout), sizeof(Timeout));
    Handshake(Client);
    Timeout = 600000;
    setsockopt(Client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&Timeout), sizeof(Timeout));
    std::string Message;
    bool Fragmented = false;
    unsigned MessageOpcode = 0;
    for (;;)
    {
        std::array<unsigned char, 2> Header{};
        Read(Client, reinterpret_cast<char *>(Header.data()), Header.size());
        unsigned Opcode = Header[0] & 15;
        bool Final = (Header[0] & 128) != 0;
        if ((Header[0] & 112) || !(Header[1] & 128))
            throw Error("Invalid frame flags or missing client mask");
        std::uint64_t Size = Header[1] & 127;
        auto Marker = Size;
        if (Size >= 126)
        {
            unsigned Count = Size == 126 ? 2 : 8;
            Size = 0;
            for (unsigned I = 0; I < Count; ++I)
            {
                unsigned char Byte;
                Read(Client, reinterpret_cast<char *>(&Byte), 1);
                Size = (Size << 8) | Byte;
            }
            if ((Marker == 126 && Size < 126) || (Marker == 127 && Size <= 65535) || (Size >> 63))
                throw Error("Invalid frame length encoding");
        }
        if (Opcode >= 8 && (!Final || Size > 125))
            throw Error("Invalid control frame");
        if (Size > Settings.MaxInputBytes * 2 + 32768 || Size + Message.size() > Settings.MaxInputBytes * 2 + 32768)
            throw Error("WebSocket message exceeds bytecode size limit");
        std::array<char, 4> Mask{};
        Read(Client, Mask.data(), Mask.size());
        std::string Payload(std::size_t(Size), '\0');
        Read(Client, Payload.data(), Payload.size());
        for (std::size_t I = 0; I < Payload.size(); ++I)
            Payload[I] ^= Mask[I % 4];
        if (Opcode == 8)
        {
            if (Payload.size() == 1)
                throw Error("Invalid close payload");
            State->SendFrame(8, std::string_view("\x03\xe8", 2));
            return;
        }
        if (Opcode == 9)
        {
            State->SendFrame(10, Payload);
            continue;
        }
        if (Opcode == 10)
            continue;
        if (Opcode != 0 && Opcode != 1 && Opcode != 2)
            throw Error("Unsupported WebSocket data opcode");
        if ((Opcode == 0) != Fragmented)
            throw Error("Unexpected continuation or data frame");
        if (Opcode != 0)
            MessageOpcode = Opcode;
        Message += Payload;
        Fragmented = !Final;
        if (Final)
        {
            auto Id = RequestId(Message);
            if (MessageOpcode == 2 && !std::string_view(Message).substr(Id.size() + 1).starts_with("ASTRA5\n"))
                throw Error("Binary requests require ASTRA5 metadata");
            {
                std::lock_guard Lock(State->PendingMutex);
                if (State->Pending.contains(Id))
                    throw Error("Duplicate in-flight request ID");
                if (State->Pending.size() >= 128)
                {
                    State->SendFrame(1, Id + "\nerror\nAstra pending request limit reached");
                    Message.clear();
                    continue;
                }
                State->Pending.insert(Id);
            }
            auto Size = Message.size();
            auto Work = [State, Settings, Id, Request = std::move(Message)]()
            {
                bool Retired = false;
                try
                {
                    if (!State->Closed)
                    {
                        auto Response = Process(Request, Settings);
                        {
                            std::lock_guard Lock(State->PendingMutex);
                            State->Pending.erase(Id);
                            Retired = true;
                        }
                        // Never hold admission state while writing: a client
                        // may pipeline requests before it starts reading large
                        // replies, so the receive loop must keep draining it.
                        State->SendFrame(1, Response);
                    }
                }
                catch (const std::exception &Failure)
                {
                    State->Close();
                    std::osyncstream(std::cerr) << "astra: " << Failure.what() << '\n';
                }
                if (!Retired)
                {
                    std::lock_guard Lock(State->PendingMutex);
                    State->Pending.erase(Id);
                }
            };
            if (!Scheduler.Submit(Size, std::move(Work)))
            {
                {
                    std::lock_guard Lock(State->PendingMutex);
                    State->Pending.erase(Id);
                }
                State->SendFrame(1, Id + "\nerror\nAstra server queue is full; retry with fewer concurrent requests");
            }
            Message = {};
        }
    }
}
} // namespace

int ServeWebSocket(unsigned Port, const Options &Settings)
{
    if (!Port || Port > 65535)
        throw Error("Port must be between 1 and 65535");
    WSADATA Data{};
    if (WSAStartup(MAKEWORD(2, 2), &Data))
        throw Error("Cannot initialize Winsock");
    struct Cleanup
    {
        ~Cleanup()
        {
            WSACleanup();
        }
    } Cleanup;
    Socket Listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (Listener.Value == INVALID_SOCKET)
        throw Error("Cannot create listener socket");
    BOOL Exclusive = TRUE;
    setsockopt(Listener.Value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&Exclusive), sizeof(Exclusive));
    sockaddr_in Address{};
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Address.sin_port = htons(static_cast<unsigned short>(Port));
    if (bind(Listener.Value, reinterpret_cast<sockaddr *>(&Address), sizeof(Address)) == SOCKET_ERROR)
        throw Error("Cannot bind 127.0.0.1:" + std::to_string(Port) + " (port may already be in use)");
    if (listen(Listener.Value, 8) == SOCKET_ERROR)
        throw Error("Cannot listen on socket");
    std::cout << "Astra listening at ws://127.0.0.1:" << Port
              << "/decompile\nRun tools/Decompile.luau in your executor. Ctrl+C stops the server.\n"
              << std::flush;
    static std::atomic<unsigned> Clients = 0;
    // Retained with the detached sessions until the process exits.
    auto Scheduler = std::make_shared<Jobs>();
    for (;;)
    {
        SOCKET Client = accept(Listener.Value, nullptr, nullptr);
        if (Client == INVALID_SOCKET)
            throw Error("Accept failed");
        if (Clients.fetch_add(1) >= 8)
        {
            --Clients;
            closesocket(Client);
            continue;
        }
        std::shared_ptr<SessionState> State;
        try
        {
            BOOL NoDelay = TRUE;
            setsockopt(Client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&NoDelay), sizeof(NoDelay));
            State = std::make_shared<SessionState>(Client);
            std::thread(
                [State, Scheduler, Settings]()
                {
                    struct Release
                    {
                        ~Release()
                        {
                            --Clients;
                        }
                    } Release;
                    try
                    {
                        Session(State, Settings, *Scheduler);
                    }
                    catch (const std::exception &Failure)
                    {
                        try
                        {
                            State->SendFrame(8, std::string_view("\x03\xea", 2));
                        }
                        catch (...)
                        {
                        }
                        std::osyncstream(std::cerr) << "astra: " << Failure.what() << '\n';
                    }
                    State->Close();
                })
                .detach();
        }
        catch (...)
        {
            --Clients;
            if (State)
                State->Close();
            else
                closesocket(Client);
            throw;
        }
    }
}
} // namespace Astra
#else
namespace Astra
{
int ServeWebSocket(unsigned, const Options &)
{
    throw Error("WebSocket server mode currently requires Windows");
}
} // namespace Astra
#endif
