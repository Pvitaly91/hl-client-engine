#pragma once

#include <hlclient/network/network_address.hpp>

#include <cstddef>
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
        std::string& error);
    [[nodiscard]] ReceiveResult receive(std::size_t maximum_size = 65'507);

private:
    struct Impl;
    explicit UdpSocket(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> implementation_;
};

} // namespace hlclient::network
