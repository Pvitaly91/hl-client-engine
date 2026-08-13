#pragma once

#include <hlclient/network/udp_socket.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace hlclient::network {

enum class DatagramSendStatus {
    sent,
    error,
};

struct DatagramSendResult {
    DatagramSendStatus status{DatagramSendStatus::error};
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return status == DatagramSendStatus::sent;
    }
};

enum class DatagramTransportReceiveStatus {
    received,
    would_block,
    truncated,
    error,
};

struct DatagramTransportReceiveResult {
    DatagramTransportReceiveStatus status{DatagramTransportReceiveStatus::would_block};
    std::optional<Datagram> datagram;
    std::optional<NetworkAddress> source;
    std::size_t payload_size{0};
    std::string error;
};

struct DatagramLocalAddressResult {
    std::optional<NetworkAddress> address;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return address.has_value();
    }
};

class IDatagramTransport {
public:
    virtual ~IDatagramTransport() = default;

    IDatagramTransport(const IDatagramTransport&) = delete;
    IDatagramTransport& operator=(const IDatagramTransport&) = delete;
    IDatagramTransport(IDatagramTransport&&) = delete;
    IDatagramTransport& operator=(IDatagramTransport&&) = delete;

    [[nodiscard]] virtual DatagramLocalAddressResult local_address() const = 0;
    [[nodiscard]] virtual DatagramSendResult send_to(
        const NetworkAddress& destination,
        std::span<const std::byte> payload) = 0;
    [[nodiscard]] virtual DatagramTransportReceiveResult receive(
        std::size_t maximum_size) = 0;

protected:
    IDatagramTransport() = default;
};

class UdpDatagramTransport final : public IDatagramTransport {
public:
    explicit UdpDatagramTransport(UdpSocket socket) noexcept;
    ~UdpDatagramTransport() override = default;

    UdpDatagramTransport(const UdpDatagramTransport&) = delete;
    UdpDatagramTransport& operator=(const UdpDatagramTransport&) = delete;
    UdpDatagramTransport(UdpDatagramTransport&&) = delete;
    UdpDatagramTransport& operator=(UdpDatagramTransport&&) = delete;

    [[nodiscard]] DatagramLocalAddressResult local_address() const override;
    [[nodiscard]] DatagramSendResult send_to(
        const NetworkAddress& destination,
        std::span<const std::byte> payload) override;
    [[nodiscard]] DatagramTransportReceiveResult receive(
        std::size_t maximum_size) override;

private:
    UdpSocket socket_;
};

} // namespace hlclient::network
