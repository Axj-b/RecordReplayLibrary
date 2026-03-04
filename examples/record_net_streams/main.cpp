#include <recplay/recplay.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <errno.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace {

std::atomic<bool> g_stop{false};

static recplay::Timestamp now_ns() {
    using namespace std::chrono;
    return static_cast<recplay::Timestamp>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

void on_signal(int) {
    g_stop.store(true, std::memory_order_relaxed);
}

#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t k_invalid_socket = INVALID_SOCKET;
#else
using socket_t = int;
constexpr socket_t k_invalid_socket = -1;
#endif

void close_socket(socket_t s) {
#ifdef _WIN32
    if (s != INVALID_SOCKET) closesocket(s);
#else
    if (s >= 0) close(s);
#endif
}

int last_socket_error() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool is_timeout_or_wouldblock(int err) {
#ifdef _WIN32
    return err == WSAETIMEDOUT || err == WSAEWOULDBLOCK;
#else
    return err == EAGAIN || err == EWOULDBLOCK || err == ETIMEDOUT;
#endif
}

bool set_recv_timeout(socket_t s, int timeout_ms) {
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(timeout_ms);
    return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                      reinterpret_cast<const char*>(&tv), sizeof(tv)) == 0;
#else
    timeval tv{};
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

struct SocketRuntime {
#ifdef _WIN32
    WSADATA wsa{};
    bool ok = false;
    SocketRuntime() {
        ok = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    }
    ~SocketRuntime() {
        if (ok) WSACleanup();
    }
#else
    bool ok = true;
#endif
};

enum class SourceKind {
    Udp,
    Tcp,
    CanPeakStub,
};

struct SourceSpec {
    SourceKind   kind{};
    std::string  host;
    uint16_t     port = 0;
    std::string  channelName;
    std::string  rawArg;
};

struct SourceStats {
    std::atomic<uint64_t> messages{0};
    std::atomic<uint64_t> bytes{0};
};

struct RuntimeContext {
    recplay::RecorderSession*        session = nullptr;
    std::mutex*                      sessionMutex = nullptr;
    std::atomic<bool>*               running = nullptr;
    std::atomic<uint64_t>*           lastWriteTs = nullptr;
    recplay::ChannelId               channelId = recplay::INVALID_CHANNEL_ID;
    const SourceSpec*                spec = nullptr;
    SourceStats*                     stats = nullptr;
};

bool parse_port(const std::string& text, uint16_t& port) {
    if (text.empty()) return false;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
    }
    unsigned long value = std::stoul(text);
    if (value > 65535ul) return false;
    port = static_cast<uint16_t>(value);
    return true;
}

bool parse_udp_arg(const std::string& arg, std::string& hostOut, uint16_t& portOut) {
    const auto pos = arg.rfind(':');
    if (pos == std::string::npos) {
        hostOut = "0.0.0.0";
        return parse_port(arg, portOut);
    }

    hostOut = arg.substr(0, pos);
    if (hostOut.empty()) hostOut = "0.0.0.0";
    return parse_port(arg.substr(pos + 1), portOut);
}

bool parse_tcp_arg(const std::string& arg, std::string& hostOut, uint16_t& portOut) {
    const auto pos = arg.rfind(':');
    if (pos == std::string::npos) return false;
    hostOut = arg.substr(0, pos);
    if (hostOut.empty()) return false;
    return parse_port(arg.substr(pos + 1), portOut);
}

void update_last_ts(std::atomic<uint64_t>& dst, uint64_t ts) {
    uint64_t cur = dst.load(std::memory_order_relaxed);
    while (ts > cur && !dst.compare_exchange_weak(cur, ts, std::memory_order_relaxed)) {}
}

void write_payload(RuntimeContext& ctx, const uint8_t* data, uint32_t len) {
    if (len == 0) return;

    const recplay::Timestamp ts = now_ns();
    {
        std::lock_guard<std::mutex> lk(*ctx.sessionMutex);
        const auto st = ctx.session->Write(ctx.channelId, ts, data, len);
        if (st != recplay::Status::Ok) {
            std::cerr << "Write failed on channel " << ctx.spec->channelName
                      << " (status " << static_cast<int>(st) << ")\n";
            return;
        }
    }

    update_last_ts(*ctx.lastWriteTs, ts);
    ctx.stats->messages.fetch_add(1, std::memory_order_relaxed);
    ctx.stats->bytes.fetch_add(len, std::memory_order_relaxed);
}

void run_udp(RuntimeContext ctx) {
    socket_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == k_invalid_socket) {
        std::cerr << "UDP socket() failed for " << ctx.spec->rawArg << "\n";
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(ctx.spec->port);

#ifdef _WIN32
    addr.sin_addr.S_un.S_addr = inet_addr(ctx.spec->host.c_str());
    if (addr.sin_addr.S_un.S_addr == INADDR_NONE && ctx.spec->host != "255.255.255.255") {
        std::cerr << "UDP invalid bind address: " << ctx.spec->host << "\n";
        close_socket(s);
        return;
    }
#else
    if (inet_pton(AF_INET, ctx.spec->host.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "UDP invalid bind address: " << ctx.spec->host << "\n";
        close_socket(s);
        return;
    }
#endif

    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "UDP bind() failed for " << ctx.spec->host << ":" << ctx.spec->port
                  << " (err " << last_socket_error() << ")\n";
        close_socket(s);
        return;
    }

    set_recv_timeout(s, 200);
    std::vector<uint8_t> buf(65536);

    std::cout << "[udp] listening on " << ctx.spec->host << ":" << ctx.spec->port
              << " -> " << ctx.spec->channelName << "\n";

    while (ctx.running->load(std::memory_order_relaxed) && !g_stop.load(std::memory_order_relaxed)) {
        sockaddr_in from{};
#ifdef _WIN32
        int fromLen = static_cast<int>(sizeof(from));
        const int n = recvfrom(s, reinterpret_cast<char*>(buf.data()),
                               static_cast<int>(buf.size()), 0,
                               reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n > 0) {
            write_payload(ctx, buf.data(), static_cast<uint32_t>(n));
            continue;
        }
        const int err = WSAGetLastError();
#else
        socklen_t fromLen = static_cast<socklen_t>(sizeof(from));
        const ssize_t n = recvfrom(s, buf.data(), buf.size(), 0,
                                   reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n > 0) {
            write_payload(ctx, buf.data(), static_cast<uint32_t>(n));
            continue;
        }
        const int err = errno;
#endif
        if (n == 0) continue;
        if (is_timeout_or_wouldblock(err)) continue;
        std::cerr << "UDP recvfrom() failed on " << ctx.spec->rawArg
                  << " (err " << err << ")\n";
        break;
    }

    close_socket(s);
}

bool connect_tcp(const SourceSpec& spec, socket_t& outSock) {
    outSock = k_invalid_socket;

    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    const std::string portText = std::to_string(spec.port);
    addrinfo* results = nullptr;
    if (getaddrinfo(spec.host.c_str(), portText.c_str(), &hints, &results) != 0) {
        return false;
    }

    for (addrinfo* it = results; it; it = it->ai_next) {
        socket_t s = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (s == k_invalid_socket) continue;

        if (connect(s, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0) {
            outSock = s;
            freeaddrinfo(results);
            return true;
        }

        close_socket(s);
    }

    freeaddrinfo(results);
    return false;
}

void run_tcp(RuntimeContext ctx) {
    std::vector<uint8_t> buf(64 * 1024);
    std::cout << "[tcp] connecting to " << ctx.spec->host << ":" << ctx.spec->port
              << " -> " << ctx.spec->channelName << "\n";

    while (ctx.running->load(std::memory_order_relaxed) && !g_stop.load(std::memory_order_relaxed)) {
        socket_t s = k_invalid_socket;
        if (!connect_tcp(*ctx.spec, s)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }

        set_recv_timeout(s, 200);

        while (ctx.running->load(std::memory_order_relaxed) && !g_stop.load(std::memory_order_relaxed)) {
#ifdef _WIN32
            const int n = recv(s, reinterpret_cast<char*>(buf.data()),
                               static_cast<int>(buf.size()), 0);
            if (n > 0) {
                write_payload(ctx, buf.data(), static_cast<uint32_t>(n));
                continue;
            }
            if (n == 0) break;
            const int err = WSAGetLastError();
#else
            const ssize_t n = recv(s, buf.data(), buf.size(), 0);
            if (n > 0) {
                write_payload(ctx, buf.data(), static_cast<uint32_t>(n));
                continue;
            }
            if (n == 0) break;
            const int err = errno;
#endif
            if (is_timeout_or_wouldblock(err)) continue;
            std::cerr << "TCP recv() failed on " << ctx.spec->rawArg
                      << " (err " << err << ")\n";
            break;
        }

        close_socket(s);

        if (ctx.running->load(std::memory_order_relaxed) && !g_stop.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
}

void run_can_peak_stub(RuntimeContext ctx) {
    std::cout << "[can] PEAK stub active for '" << ctx.spec->rawArg
              << "' -> " << ctx.spec->channelName << "\n";

    while (ctx.running->load(std::memory_order_relaxed) && !g_stop.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

void print_usage(const char* exe) {
    std::cout
        << "Usage:\n"
        << "  " << exe << " <output_dir> [options]\n\n"
        << "Options:\n"
        << "  --session <name>         Session name (default: net_streams)\n"
        << "  --single-file            Write one .rec file (default: enabled)\n"
        << "  --multi-segment          Use directory+manifest mode\n"
        << "  --duration <seconds>     Stop automatically after N seconds\n"
        << "  --udp <port|ip:port>     Add UDP listener source (repeatable)\n"
        << "  --tcp <host:port>        Add TCP client source (repeatable)\n"
        << "  --can-peak <device>      Add PEAK CAN stub source (repeatable)\n"
        << "  --help                   Show help\n\n"
        << "Examples:\n"
        << "  " << exe << " recordings --udp 5000 --udp 127.0.0.1:6000\n"
        << "  " << exe << " recordings --tcp 127.0.0.1:9000 --can-peak PCAN_USBBUS1\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc <= 1) {
        print_usage(argv[0]);
        return 1;
    }

    SocketRuntime sockets;
    if (!sockets.ok) {
        std::cerr << "Socket runtime init failed\n";
        return 1;
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    std::string outputDir;
    std::string sessionName = "net_streams";
    bool singleFile = true;
    int durationSec = 0;
    std::vector<SourceSpec> specs;

    int i = 1;
    if (i < argc && std::string(argv[i]).rfind("--", 0) != 0) {
        outputDir = argv[i++];
    }

    while (i < argc) {
        const std::string arg = argv[i++];

        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        if (arg == "--session") {
            if (i >= argc) {
                std::cerr << "Missing value for --session\n";
                return 1;
            }
            sessionName = argv[i++];
            continue;
        }
        if (arg == "--single-file") {
            singleFile = true;
            continue;
        }
        if (arg == "--multi-segment") {
            singleFile = false;
            continue;
        }
        if (arg == "--duration") {
            if (i >= argc) {
                std::cerr << "Missing value for --duration\n";
                return 1;
            }
            durationSec = std::atoi(argv[i++]);
            if (durationSec < 0) durationSec = 0;
            continue;
        }
        if (arg == "--udp") {
            if (i >= argc) {
                std::cerr << "Missing value for --udp\n";
                return 1;
            }
            SourceSpec s;
            s.kind   = SourceKind::Udp;
            s.rawArg = argv[i++];
            if (!parse_udp_arg(s.rawArg, s.host, s.port)) {
                std::cerr << "Invalid --udp value: " << s.rawArg << "\n";
                return 1;
            }
            s.channelName = "net/udp/" + s.host + ":" + std::to_string(s.port);
            specs.push_back(std::move(s));
            continue;
        }
        if (arg == "--tcp") {
            if (i >= argc) {
                std::cerr << "Missing value for --tcp\n";
                return 1;
            }
            SourceSpec s;
            s.kind   = SourceKind::Tcp;
            s.rawArg = argv[i++];
            if (!parse_tcp_arg(s.rawArg, s.host, s.port)) {
                std::cerr << "Invalid --tcp value: " << s.rawArg << "\n";
                return 1;
            }
            s.channelName = "net/tcp/" + s.host + ":" + std::to_string(s.port);
            specs.push_back(std::move(s));
            continue;
        }
        if (arg == "--can-peak") {
            if (i >= argc) {
                std::cerr << "Missing value for --can-peak\n";
                return 1;
            }
            SourceSpec s;
            s.kind        = SourceKind::CanPeakStub;
            s.rawArg      = argv[i++];
            s.channelName = "can/peak_stub/" + s.rawArg;
            specs.push_back(std::move(s));
            continue;
        }

        std::cerr << "Unknown argument: " << arg << "\n";
        return 1;
    }

    if (outputDir.empty()) {
        std::cerr << "Missing <output_dir>\n";
        print_usage(argv[0]);
        return 1;
    }

    if (specs.empty()) {
        std::cerr << "No sources configured. Add at least one --udp / --tcp / --can-peak\n";
        return 1;
    }

    recplay::RecorderSession session;

    recplay::SessionConfig cfg;
    cfg.OutputDir   = outputDir;
    cfg.SessionName = sessionName;
    cfg.CrcEnabled  = true;
    cfg.SingleFile  = singleFile;

    const auto openSt = session.Open(cfg);
    if (openSt != recplay::Status::Ok) {
        std::cerr << "Failed to open session (status " << static_cast<int>(openSt) << ")\n";
        return 1;
    }

    std::vector<recplay::ChannelId> ids(specs.size(), recplay::INVALID_CHANNEL_ID);
    std::vector<SourceStats> stats(specs.size());

    for (size_t idx = 0; idx < specs.size(); ++idx) {
        recplay::ChannelConfig ch;
        ch.Name        = specs[idx].channelName;
        ch.Layer       = (specs[idx].kind == SourceKind::CanPeakStub)
            ? recplay::CaptureLayer::L7
            : recplay::CaptureLayer::L3L4;
        ch.Compression = recplay::CompressionCodec::None;

        switch (specs[idx].kind) {
            case SourceKind::Udp:        ch.Schema = "network/udp-payload"; break;
            case SourceKind::Tcp:        ch.Schema = "network/tcp-payload"; break;
            case SourceKind::CanPeakStub:ch.Schema = "can/peak-stub"; break;
        }

        const auto st = session.DefineChannel(ch, ids[idx]);
        if (st != recplay::Status::Ok) {
            std::cerr << "DefineChannel failed for " << ch.Name
                      << " (status " << static_cast<int>(st) << ")\n";
            session.Close();
            return 1;
        }

        std::cout << "channel [" << ids[idx] << "] " << ch.Name << "\n";
    }

    std::mutex writeMutex;
    std::atomic<bool> running{true};
    std::atomic<uint64_t> lastWriteTs{0};

    {
        std::lock_guard<std::mutex> lk(writeMutex);
        session.Annotate(now_ns(), "capture_started");
        for (const auto& spec : specs) {
            if (spec.kind == SourceKind::CanPeakStub) {
                const std::string label = "can_peak_stub:" + spec.rawArg;
                session.Annotate(now_ns(), label);
            }
        }
    }

    std::vector<std::thread> workers;
    workers.reserve(specs.size());

    for (size_t idx = 0; idx < specs.size(); ++idx) {
        RuntimeContext ctx;
        ctx.session = &session;
        ctx.sessionMutex = &writeMutex;
        ctx.running = &running;
        ctx.lastWriteTs = &lastWriteTs;
        ctx.channelId = ids[idx];
        ctx.spec = &specs[idx];
        ctx.stats = &stats[idx];

        switch (specs[idx].kind) {
            case SourceKind::Udp:
                workers.emplace_back([ctx]() mutable { run_udp(ctx); });
                break;
            case SourceKind::Tcp:
                workers.emplace_back([ctx]() mutable { run_tcp(ctx); });
                break;
            case SourceKind::CanPeakStub:
                workers.emplace_back([ctx]() mutable { run_can_peak_stub(ctx); });
                break;
        }
    }

    if (durationSec > 0) {
        const auto stopAt = std::chrono::steady_clock::now() + std::chrono::seconds(durationSec);
        while (!g_stop.load(std::memory_order_relaxed) && std::chrono::steady_clock::now() < stopAt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } else {
        std::cout << "Recording... press ENTER to stop.\n";
        std::string line;
        std::getline(std::cin, line);
    }

    g_stop.store(true, std::memory_order_relaxed);
    running.store(false, std::memory_order_relaxed);

    for (auto& th : workers) {
        if (th.joinable()) th.join();
    }

    const uint64_t endTs = std::max<uint64_t>(lastWriteTs.load(std::memory_order_relaxed), now_ns());

    std::string sessionPath;
    {
        std::lock_guard<std::mutex> lk(writeMutex);
        session.Annotate(endTs, "recording_end");
        sessionPath = session.SessionPath();
    }

    const auto closeSt = session.Close();
    if (closeSt != recplay::Status::Ok) {
        std::cerr << "Session close failed (status " << static_cast<int>(closeSt) << ")\n";
        return 1;
    }

    std::cout << "\nDone.\n";
    std::cout << "  session: " << sessionPath << "\n";
    for (size_t idx = 0; idx < specs.size(); ++idx) {
        std::cout << "  " << specs[idx].channelName
                  << " msgs=" << stats[idx].messages.load(std::memory_order_relaxed)
                  << " bytes=" << stats[idx].bytes.load(std::memory_order_relaxed)
                  << "\n";
    }

    return 0;
}
