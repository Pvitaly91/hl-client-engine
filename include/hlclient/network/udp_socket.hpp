#pragma once

#include <hlclient/network/network_address.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hlclient::network {

class NetworkRuntime;

struct Datagram {
    NetworkAddress source;
    std::vector<std::byte> payload;
};

enum class SocketNativeErrorDomain {
    none,
    winsock,
    posix,
};

struct SocketNativeError final {
    SocketNativeErrorDomain domain{SocketNativeErrorDomain::none};
    std::uint32_t code{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return domain != SocketNativeErrorDomain::none;
    }
};

enum class ReceiveStatus {
    received,
    would_block,
    truncated,
    error,
};

struct ReceiveResult {
    ReceiveStatus status{ReceiveStatus::would_block};
    std::optional<Datagram> datagram;
    std::string error;
    std::optional<NetworkAddress> source;
    std::size_t payload_size_lower_bound{0};
    SocketNativeError native_error{};
};

class UdpSocket final {
public:
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&&) noexcept;
    UdpSocket& operator=(UdpSocket&&) noexcept;

    [[nodiscard]] static std::optional<UdpSocket> open_ipv4(
        const NetworkRuntime& runtime,
        std::string& error);

    [[nodiscard]] bool bind(const NetworkAddress& local_address, std::string& error);
    [[nodiscard]] std::optional<NetworkAddress> local_address(std::string& error) const;
    [[nodiscard]] bool send_to(
        const NetworkAddress& destination,
        std::span<const std::byte> payload,
        std::string& error,
        SocketNativeError* native_error = nullptr);
    [[nodiscard]] ReceiveResult receive(std::size_t maximum_size = 65'507);
    // Explicit, one-shot native teardown. Empty error means successfully
    // closed/already empty; a native error remains cached on repeated close.
    // No packet, handshake or network wait is performed here.
    [[nodiscard]] SocketNativeError close() noexcept;

private:
    struct Impl;
    explicit UdpSocket(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> implementation_;
    bool close_requested_{};
    SocketNativeError close_error_{};
};

} // namespace hlclient::network
