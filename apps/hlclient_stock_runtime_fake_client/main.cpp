#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
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

struct Options final {
    std::uint16_t port{0U};
    std::uint32_t timeout_ms{2'000U};
    std::uint32_t exit_code{0U};
};

[[nodiscard]] std::optional<Options> parse_options(
    const int argc,
    char** argv) noexcept
{
    Options options;
    bool port_seen = false;
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) return std::nullopt;
        const std::string_view name{argv[index]};
        const std::string_view value{argv[index + 1]};
        unsigned int parsed = 0U;
        const auto result = std::from_chars(
            value.data(), value.data() + value.size(), parsed, 10);
        if (result.ec != std::errc{} ||
            result.ptr != value.data() + value.size()) return std::nullopt;
        if (name == "--port" && !port_seen && parsed >= 1'024U &&
            parsed <= 65'535U) {
            options.port = static_cast<std::uint16_t>(parsed);
            port_seen = true;
        } else if (name == "--timeout-ms" && parsed > 0U &&
                   parsed <= 30'000U) {
            options.timeout_ms = parsed;
        } else if (name == "--exit-code" && parsed <= 255U) {
            options.exit_code = parsed;
        } else {
            return std::nullopt;
        }
    }
    return port_seen ? std::optional<Options>{options} : std::nullopt;
}

} // namespace

int main(const int argc, char** argv)
{
    const auto options = parse_options(argc, argv);
    if (!options) return 2;
    WSADATA data{};
    if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) return 3;
    const SOCKET socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == INVALID_SOCKET) {
        static_cast<void>(::WSACleanup());
        return 3;
    }
    DWORD timeout = options->timeout_ms;
    static_cast<void>(::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                                  reinterpret_cast<const char*>(&timeout),
                                  sizeof(timeout)));
    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_addr.S_un.S_addr = htonl(0x7f000001U);
    peer.sin_port = htons(options->port);
    constexpr std::string_view request =
        "HLCLIENT_FAKE_ORCHESTRATION_REQUEST_V1";
    if (::sendto(socket, request.data(), static_cast<int>(request.size()), 0,
                 reinterpret_cast<const sockaddr*>(&peer), sizeof(peer)) ==
        SOCKET_ERROR) {
        static_cast<void>(::closesocket(socket));
        static_cast<void>(::WSACleanup());
        return 4;
    }
    std::array<char, 256U> response{};
    const int received = ::recvfrom(
        socket, response.data(), static_cast<int>(response.size()), 0,
        nullptr, nullptr);
    static_cast<void>(::closesocket(socket));
    static_cast<void>(::WSACleanup());
    constexpr std::string_view expected =
        "HLCLIENT_FAKE_ORCHESTRATION_RESPONSE_V1";
    if (received != static_cast<int>(expected.size()) ||
        std::string_view{response.data(), static_cast<std::size_t>(
                                             (std::max)(received, 0))} != expected) {
        return 5;
    }
    std::cout << "[hlclient-fake-client] ready=true\n";
    return static_cast<int>(options->exit_code);
}
