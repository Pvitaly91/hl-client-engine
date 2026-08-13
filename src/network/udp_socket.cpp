#include <hlclient/network/udp_socket.hpp>
#include <hlclient/network/network_runtime.hpp>

#include <limits>
#include <utility>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <cerrno>
#    include <cstring>
#    include <fcntl.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <sys/uio.h>
#    include <unistd.h>
#endif

namespace hlclient::network {
namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
inline constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
inline constexpr NativeSocket kInvalidSocket = -1;
#endif

[[nodiscard]] std::string socket_error(const std::string_view operation)
{
#ifdef _WIN32
    return std::string{operation} + " failed with Winsock error " + std::to_string(WSAGetLastError());
#else
    return std::string{operation} + " failed: " + std::strerror(errno);
#endif
}

void close_socket(const NativeSocket socket) noexcept
{
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

[[nodiscard]] sockaddr_in to_native(const NetworkAddress& address) noexcept
{
    sockaddr_in native{};
    native.sin_family = AF_INET;
    native.sin_port = htons(address.port());
    native.sin_addr.s_addr = htonl(address.ipv4_host_order());
    return native;
}

[[nodiscard]] NetworkAddress from_native(const sockaddr_in& address) noexcept
{
    return NetworkAddress{ntohl(address.sin_addr.s_addr), ntohs(address.sin_port)};
}

[[nodiscard]] bool is_would_block() noexcept
{
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

} // namespace

struct UdpSocket::Impl final {
    explicit Impl(std::shared_ptr<NetworkRuntime::Impl> requested_runtime) noexcept
        : runtime{std::move(requested_runtime)}
    {
    }

    ~Impl()
    {
        if (handle != kInvalidSocket) {
            close_socket(handle);
        }
    }

    std::shared_ptr<NetworkRuntime::Impl> runtime;
    NativeSocket handle{kInvalidSocket};
};

UdpSocket::UdpSocket(std::unique_ptr<Impl> implementation) noexcept
    : implementation_{std::move(implementation)}
{
}

UdpSocket::~UdpSocket() = default;
UdpSocket::UdpSocket(UdpSocket&&) noexcept = default;
UdpSocket& UdpSocket::operator=(UdpSocket&&) noexcept = default;

std::optional<UdpSocket> UdpSocket::open_ipv4(
    const NetworkRuntime& runtime,
    std::string& error)
{
    error.clear();
    if (!runtime.valid()) {
        error = "Network runtime is unavailable: " + runtime.error_message();
        return std::nullopt;
    }

    auto implementation = std::make_unique<Impl>(runtime.implementation_);
    const NativeSocket handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == kInvalidSocket) {
        error = socket_error("socket");
        return std::nullopt;
    }
    implementation->handle = handle;

#ifdef _WIN32
    u_long non_blocking = 1;
    if (ioctlsocket(handle, FIONBIO, &non_blocking) == SOCKET_ERROR) {
        error = socket_error("ioctlsocket(FIONBIO)");
        return std::nullopt;
    }
#else
    const int current_flags = fcntl(handle, F_GETFL, 0);
    if (current_flags < 0 || fcntl(handle, F_SETFL, current_flags | O_NONBLOCK) < 0) {
        error = socket_error("fcntl(O_NONBLOCK)");
        return std::nullopt;
    }
#endif

    return UdpSocket{std::move(implementation)};
}

bool UdpSocket::bind(const NetworkAddress& local_address, std::string& error)
{
    error.clear();
    if (!implementation_) {
        error = "Cannot bind a moved-from UDP socket";
        return false;
    }
    const auto native = to_native(local_address);
    if (::bind(
            implementation_->handle,
            reinterpret_cast<const sockaddr*>(&native),
            static_cast<int>(sizeof(native))) != 0) {
        error = socket_error("bind");
        return false;
    }
    return true;
}

std::optional<NetworkAddress> UdpSocket::local_address(std::string& error) const
{
    error.clear();
    if (!implementation_) {
        error = "Cannot query a moved-from UDP socket";
        return std::nullopt;
    }
    sockaddr_in native{};
#ifdef _WIN32
    int size = sizeof(native);
#else
    socklen_t size = sizeof(native);
#endif
    if (getsockname(
            implementation_->handle,
            reinterpret_cast<sockaddr*>(&native),
            &size) != 0) {
        error = socket_error("getsockname");
        return std::nullopt;
    }
    return from_native(native);
}

bool UdpSocket::send_to(
    const NetworkAddress& destination,
    const std::span<const std::byte> payload,
    std::string& error)
{
    error.clear();
    if (!implementation_) {
        error = "Cannot send with a moved-from UDP socket";
        return false;
    }
    if (payload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        error = "Datagram is too large for the socket API";
        return false;
    }

    const auto native = to_native(destination);
#ifdef _WIN32
    const int sent = sendto(
        implementation_->handle,
        reinterpret_cast<const char*>(payload.data()),
        static_cast<int>(payload.size()),
        0,
        reinterpret_cast<const sockaddr*>(&native),
        static_cast<int>(sizeof(native)));
    if (sent == SOCKET_ERROR) {
#else
    const auto sent = sendto(
        implementation_->handle,
        payload.data(),
        payload.size(),
        0,
        reinterpret_cast<const sockaddr*>(&native),
        sizeof(native));
    if (sent < 0) {
#endif
        error = socket_error("sendto");
        return false;
    }

    if (static_cast<std::size_t>(sent) != payload.size()) {
        error = "sendto completed with a partial datagram";
        return false;
    }
    return true;
}

ReceiveResult UdpSocket::receive(const std::size_t maximum_size)
{
    if (!implementation_) {
        return ReceiveResult{
            ReceiveStatus::error,
            std::nullopt,
            "Cannot receive with a moved-from UDP socket",
        };
    }
    if (maximum_size == 0 || maximum_size > 65'507) {
        return ReceiveResult{ReceiveStatus::error, std::nullopt, "Invalid UDP receive buffer size"};
    }

    Datagram datagram;
    datagram.payload.resize(maximum_size);
    sockaddr_in source{};
#ifdef _WIN32
    int source_size = sizeof(source);
    const int received = recvfrom(
        implementation_->handle,
        reinterpret_cast<char*>(datagram.payload.data()),
        static_cast<int>(datagram.payload.size()),
        0,
        reinterpret_cast<sockaddr*>(&source),
        &source_size);
    if (received == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEMSGSIZE) {
            return ReceiveResult{
                ReceiveStatus::truncated,
                std::nullopt,
                "Received datagram exceeds the configured size limit",
            };
        }
#else
    socklen_t source_size = sizeof(source);
    iovec buffer{datagram.payload.data(), datagram.payload.size()};
    msghdr message{};
    message.msg_name = &source;
    message.msg_namelen = source_size;
    message.msg_iov = &buffer;
    message.msg_iovlen = 1;
    const auto received = recvmsg(implementation_->handle, &message, 0);
    if (received < 0) {
#endif
        if (is_would_block()) {
            return ReceiveResult{ReceiveStatus::would_block, std::nullopt, {}};
        }
#ifdef _WIN32
        return ReceiveResult{ReceiveStatus::error, std::nullopt, socket_error("recvfrom")};
#else
        return ReceiveResult{ReceiveStatus::error, std::nullopt, socket_error("recvmsg")};
#endif
    }

#ifndef _WIN32
    if ((message.msg_flags & MSG_TRUNC) != 0) {
        return ReceiveResult{
            ReceiveStatus::truncated,
            std::nullopt,
            "Received datagram exceeds the configured size limit",
        };
    }
#endif

    datagram.source = from_native(source);
    datagram.payload.resize(static_cast<std::size_t>(received));
    return ReceiveResult{ReceiveStatus::received, std::move(datagram), {}};
}

} // namespace hlclient::network
