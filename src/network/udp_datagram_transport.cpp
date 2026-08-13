#include <hlclient/network/datagram_transport.hpp>

#include <utility>

namespace hlclient::network {

UdpDatagramTransport::UdpDatagramTransport(UdpSocket socket) noexcept
    : socket_{std::move(socket)}
{
}

DatagramLocalAddressResult UdpDatagramTransport::local_address() const
{
    std::string error;
    auto address = socket_.local_address(error);
    return DatagramLocalAddressResult{std::move(address), std::move(error)};
}

DatagramSendResult UdpDatagramTransport::send_to(
    const NetworkAddress& destination,
    const std::span<const std::byte> payload)
{
    std::string error;
    if (!socket_.send_to(destination, payload, error)) {
        return DatagramSendResult{DatagramSendStatus::error, std::move(error)};
    }
    return DatagramSendResult{DatagramSendStatus::sent, {}};
}

DatagramTransportReceiveResult UdpDatagramTransport::receive(const std::size_t maximum_size)
{
    if (maximum_size == 0U || maximum_size > 65'507U) {
        return DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::error,
            std::nullopt,
            std::nullopt,
            0U,
            "Invalid datagram transport receive limit",
        };
    }

    auto result = socket_.receive(maximum_size);
    switch (result.status) {
    case ReceiveStatus::received: {
        if (!result.datagram) {
            return DatagramTransportReceiveResult{
                DatagramTransportReceiveStatus::error,
                std::nullopt,
                std::nullopt,
                0U,
                "UDP socket reported a datagram without payload storage",
            };
        }

        const auto source = result.datagram->source;
        const auto payload_size = result.datagram->payload.size();
        if (payload_size > maximum_size) {
            return DatagramTransportReceiveResult{
                DatagramTransportReceiveStatus::truncated,
                std::nullopt,
                source,
                payload_size,
                "Received datagram exceeds the configured protocol size limit",
            };
        }
        return DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::received,
            std::move(result.datagram),
            source,
            payload_size,
            {},
        };
    }
    case ReceiveStatus::would_block:
        return DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::would_block,
            std::nullopt,
            std::nullopt,
            0U,
            {},
        };
    case ReceiveStatus::truncated:
        return DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::truncated,
            std::nullopt,
            result.source,
            result.payload_size_lower_bound,
            std::move(result.error),
        };
    case ReceiveStatus::error:
        return DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::error,
            std::nullopt,
            std::nullopt,
            0U,
            std::move(result.error),
        };
    }

    return DatagramTransportReceiveResult{
        DatagramTransportReceiveStatus::error,
        std::nullopt,
        std::nullopt,
        0U,
        "UDP socket returned an unknown receive status",
    };
}

} // namespace hlclient::network
