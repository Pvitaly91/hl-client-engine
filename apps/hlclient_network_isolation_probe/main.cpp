#include <array>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace {

class Winsock final {
public:
    Winsock() noexcept
    {
        WSADATA data{};
        started_ = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~Winsock()
    {
        if (started_) static_cast<void>(::WSACleanup());
    }
    [[nodiscard]] explicit operator bool() const noexcept { return started_; }
private:
    bool started_{false};
};

class Socket final {
public:
    explicit Socket(const SOCKET value) noexcept : value_{value} {}
    ~Socket()
    {
        if (value_ != INVALID_SOCKET) static_cast<void>(::closesocket(value_));
    }
    [[nodiscard]] SOCKET get() const noexcept { return value_; }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value_ != INVALID_SOCKET;
    }
private:
    SOCKET value_{INVALID_SOCKET};
};

struct Options final {
    std::string mode;
    std::string host;
    std::uint16_t port{0U};
};

[[nodiscard]] std::optional<Options> parse_options(
    const int argc,
    char** argv) noexcept
{
    if (argc != 7) return std::nullopt;
    Options options;
    bool mode = false;
    bool host = false;
    bool port = false;
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) return std::nullopt;
        const std::string_view name{argv[index]};
        const std::string_view value{argv[index + 1]};
        if (name == "--mode" && !mode) {
            mode = true;
            options.mode = value;
        } else if (name == "--host" && !host) {
            host = true;
            options.host = value;
        } else if (name == "--port" && !port) {
            port = true;
            unsigned int parsed = 0U;
            const auto result = std::from_chars(
                value.data(), value.data() + value.size(), parsed, 10);
            if (result.ec != std::errc{} ||
                result.ptr != value.data() + value.size() || parsed == 0U ||
                parsed > 65'535U) {
                return std::nullopt;
            }
            options.port = static_cast<std::uint16_t>(parsed);
        } else {
            return std::nullopt;
        }
    }
    const bool known = options.mode == "ipv4-loopback" ||
        options.mode == "ipv6-loopback" ||
        options.mode == "nonloopback-denied" ||
        options.mode == "restored-nonloopback";
    return mode && host && port && known
        ? std::optional<Options>{std::move(options)}
        : std::nullopt;
}

struct ConnectResult final {
    bool connected{false};
    bool timeout{false};
    int error{0};
};

[[nodiscard]] ConnectResult connect_bounded(const Options& options) noexcept
{
    const bool ipv6 = options.mode == "ipv6-loopback";
    sockaddr_storage storage{};
    int size = 0;
    if (ipv6) {
        auto* address = reinterpret_cast<sockaddr_in6*>(&storage);
        address->sin6_family = AF_INET6;
        address->sin6_port = htons(options.port);
        if (::InetPtonA(AF_INET6, options.host.c_str(), &address->sin6_addr) != 1) {
            return {false, false, WSAEINVAL};
        }
        size = sizeof(*address);
    } else {
        auto* address = reinterpret_cast<sockaddr_in*>(&storage);
        address->sin_family = AF_INET;
        address->sin_port = htons(options.port);
        if (::InetPtonA(AF_INET, options.host.c_str(), &address->sin_addr) != 1) {
            return {false, false, WSAEINVAL};
        }
        size = sizeof(*address);
    }

    Socket socket{::socket(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP)};
    if (!socket) return {false, false, ::WSAGetLastError()};
    u_long nonblocking = 1U;
    if (::ioctlsocket(socket.get(), FIONBIO, &nonblocking) == SOCKET_ERROR) {
        return {false, false, ::WSAGetLastError()};
    }
    if (::connect(socket.get(), reinterpret_cast<const sockaddr*>(&storage), size) == 0) {
        return {true, false, 0};
    }
    const int initial = ::WSAGetLastError();
    if (initial != WSAEWOULDBLOCK && initial != WSAEINPROGRESS &&
        initial != WSAEINVAL) {
        return {false, false, initial};
    }
    fd_set writable{};
    fd_set errors{};
    FD_ZERO(&writable);
    FD_ZERO(&errors);
    FD_SET(socket.get(), &writable);
    FD_SET(socket.get(), &errors);
    timeval timeout{3, 0};
    const int selected = ::select(0, nullptr, &writable, &errors, &timeout);
    if (selected == 0) return {false, true, WSAETIMEDOUT};
    if (selected == SOCKET_ERROR) return {false, false, ::WSAGetLastError()};
    int error = 0;
    int error_size = sizeof(error);
    if (::getsockopt(socket.get(), SOL_SOCKET, SO_ERROR,
                     reinterpret_cast<char*>(&error), &error_size) == SOCKET_ERROR) {
        return {false, false, ::WSAGetLastError()};
    }
    return {error == 0, false, error};
}

} // namespace

int main(const int argc, char** argv)
{
    const auto options = parse_options(argc, argv);
    if (!options) {
        std::cerr << "Usage: hlclient_network_isolation_probe --mode "
                     "ipv4-loopback|ipv6-loopback|nonloopback-denied|"
                     "restored-nonloopback --host <numeric-literal> --port <port>\n";
        return 2;
    }
    Winsock winsock;
    if (!winsock) {
        std::cout << "isolation-probe=" << options->mode
                  << ";result=failed;os-error=" << ::WSAGetLastError() << "\n";
        return 1;
    }
    const auto result = connect_bounded(*options);
    if (options->mode == "nonloopback-denied") {
        if (!result.connected && !result.timeout && result.error == WSAEACCES) {
            std::cout << "isolation-probe=nonloopback-denied;result=success;"
                         "os-error=" << result.error << "\n";
            return 0;
        }
        std::cout << "isolation-probe=nonloopback-denied;result=failed;os-error="
                  << result.error << ";timeout="
                  << (result.timeout ? "true" : "false") << "\n";
        return !result.connected && !result.timeout ? 3 : 1;
    }
    if (!result.connected) {
        std::cout << "isolation-probe=" << options->mode
                  << ";result=failed;os-error=" << result.error << "\n";
        return 1;
    }
    std::cout << "isolation-probe=" << options->mode
              << ";result=success;os-error=0\n";
    return 0;
}
