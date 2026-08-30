#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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

template<typename Integer>
[[nodiscard]] bool parse_integer(const std::string_view value, Integer& output)
{
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), output, 10);
    return !value.empty() && result.ec == std::errc{} &&
           result.ptr == value.data() + value.size();
}

struct Options final {
    std::uint16_t port{0U};
    std::uint32_t duration_ms{2'000U};
    std::uint32_t emit_bytes{0U};
    std::uint32_t exit_code{0U};
    std::uint32_t ready_delay_ms{0U};
    bool suppress_ready{false};
    std::optional<std::filesystem::path> create_file;
    std::optional<std::filesystem::path> attempt_child_create_file;
};

[[nodiscard]] std::optional<Options> parse_options(
    const int argc,
    char** argv)
{
    Options options;
    bool port_seen = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view name{argv[index]};
        if (name == "--suppress-ready" && !options.suppress_ready) {
            options.suppress_ready = true;
            continue;
        }
        if (index + 1 >= argc) return std::nullopt;
        const std::string_view value{argv[++index]};
        if (name == "--port" && !port_seen) {
            unsigned int parsed = 0U;
            if (!parse_integer(value, parsed) || parsed < 1'024U ||
                parsed > 65'535U) return std::nullopt;
            options.port = static_cast<std::uint16_t>(parsed);
            port_seen = true;
        } else if (name == "--duration-ms") {
            if (!parse_integer(value, options.duration_ms) ||
                options.duration_ms > 30'000U) return std::nullopt;
        } else if (name == "--emit-bytes") {
            if (!parse_integer(value, options.emit_bytes) ||
                options.emit_bytes > 32U * 1'024U * 1'024U) return std::nullopt;
        } else if (name == "--exit-code") {
            if (!parse_integer(value, options.exit_code) ||
                options.exit_code > 255U) return std::nullopt;
        } else if (name == "--ready-delay-ms") {
            if (!parse_integer(value, options.ready_delay_ms) ||
                options.ready_delay_ms > 30'000U) return std::nullopt;
        } else if (name == "--create-file" && !options.create_file) {
            options.create_file = std::filesystem::path{value};
        } else if (name == "--attempt-child-create-file" &&
                   !options.attempt_child_create_file) {
            options.attempt_child_create_file = std::filesystem::path{value};
        } else {
            return std::nullopt;
        }
    }
    return port_seen ? std::optional<Options>{std::move(options)}
                     : std::nullopt;
}

} // namespace

int main(const int argc, char** argv)
{
    const auto options = parse_options(argc, argv);
    if (!options) return 2;
    if (options->create_file) {
        std::ofstream output{*options->create_file, std::ios::binary | std::ios::trunc};
        if (!output) return 3;
        output << "hlclient fake orchestration file\n";
    }
    WSADATA data{};
    if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) return 4;
    const SOCKET socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == INVALID_SOCKET) {
        static_cast<void>(::WSACleanup());
        return 4;
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.S_un.S_addr = htonl(0x7f000001U);
    local.sin_port = htons(options->port);
    if (::bind(socket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) ==
        SOCKET_ERROR) {
        static_cast<void>(::closesocket(socket));
        static_cast<void>(::WSACleanup());
        return 4;
    }
    const HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == nullptr || output == INVALID_HANDLE_VALUE) {
        static_cast<void>(::closesocket(socket));
        static_cast<void>(::WSACleanup());
        return 5;
    }
    const auto write_all = [output](const std::string_view bytes) {
        std::size_t offset = 0U;
        while (offset < bytes.size()) {
            DWORD written = 0U;
            const auto count = static_cast<DWORD>((std::min<std::size_t>)(
                bytes.size() - offset, 4'096U));
            if (!::WriteFile(output, bytes.data() + offset, count, &written,
                             nullptr) || written == 0U) {
                return false;
            }
            offset += written;
        }
        return true;
    };
    if (options->attempt_child_create_file) {
        std::wstring executable(32'768U, L'\0');
        const DWORD executable_size = ::GetModuleFileNameW(
            nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (executable_size == 0U || executable_size >= executable.size()) {
            static_cast<void>(::closesocket(socket));
            static_cast<void>(::WSACleanup());
            return 6;
        }
        executable.resize(executable_size);
        std::wstring command = L"\"" + executable +
            L"\" --port " + std::to_wstring(options->port) +
            L" --duration-ms 0 --create-file \"" +
            options->attempt_child_create_file->wstring() + L"\"";
        std::vector<wchar_t> mutable_command(command.begin(), command.end());
        mutable_command.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        if (::CreateProcessW(
                executable.c_str(), mutable_command.data(), nullptr, nullptr,
                FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                &child) != FALSE) {
            static_cast<void>(::WaitForSingleObject(child.hProcess, 2'000U));
            static_cast<void>(::TerminateProcess(child.hProcess, 6U));
            static_cast<void>(::CloseHandle(child.hThread));
            static_cast<void>(::CloseHandle(child.hProcess));
            static_cast<void>(write_all(
                "[hlclient-fake-server] child-create=unexpected-success\n"));
            static_cast<void>(::closesocket(socket));
            static_cast<void>(::WSACleanup());
            return 6;
        }
        const DWORD child_error = ::GetLastError();
        if (!write_all(
                "[hlclient-fake-server] child-create=denied;native-error=" +
                std::to_string(child_error) + "\n")) {
            static_cast<void>(::closesocket(socket));
            static_cast<void>(::WSACleanup());
            return 5;
        }
    }
    std::string emitted(options->emit_bytes, 'F');
    for (std::uint32_t count = 78U; count < options->emit_bytes; count += 79U) {
        emitted[count] = '\n';
    }
    if (!write_all(emitted)) {
        static_cast<void>(::closesocket(socket));
        static_cast<void>(::WSACleanup());
        return 5;
    }
    if (!options->suppress_ready) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds{options->ready_delay_ms});
        if (!write_all(
                "[hlclient-fake-server] fake-profile=project-owned-v1\n"
                "[hlclient-fake-server] ready=true\n")) {
            static_cast<void>(::closesocket(socket));
            static_cast<void>(::WSACleanup());
            return 5;
        }
    }
    DWORD timeout = 100U;
    static_cast<void>(::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                                  reinterpret_cast<const char*>(&timeout),
                                  sizeof(timeout)));
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds{options->duration_ms};
    std::array<char, 256U> buffer{};
    while (std::chrono::steady_clock::now() < deadline) {
        sockaddr_in peer{};
        int peer_size = sizeof(peer);
        const int received = ::recvfrom(
            socket, buffer.data(), static_cast<int>(buffer.size()), 0,
            reinterpret_cast<sockaddr*>(&peer), &peer_size);
        if (received <= 0) continue;
        constexpr std::string_view response =
            "HLCLIENT_FAKE_ORCHESTRATION_RESPONSE_V1";
        static_cast<void>(::sendto(
            socket, response.data(), static_cast<int>(response.size()), 0,
            reinterpret_cast<const sockaddr*>(&peer), peer_size));
        break;
    }
    static_cast<void>(::closesocket(socket));
    static_cast<void>(::WSACleanup());
    return static_cast<int>(options->exit_code);
}
