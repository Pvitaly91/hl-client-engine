#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::network {

class NetworkRuntime;

class NetworkAddress final {
public:
    constexpr NetworkAddress() noexcept = default;
    constexpr NetworkAddress(const std::uint32_t ipv4_host_order, const std::uint16_t port) noexcept
        : ipv4_host_order_{ipv4_host_order}, port_{port}
    {
    }

    [[nodiscard]] static std::optional<NetworkAddress> parse(std::string_view endpoint) noexcept;
    [[nodiscard]] static std::optional<NetworkAddress> resolve_ipv4(
        const NetworkRuntime& runtime,
        std::string_view host,
        std::uint16_t port,
        std::string& error);
    [[nodiscard]] static constexpr NetworkAddress loopback(const std::uint16_t port) noexcept
    {
        return NetworkAddress{0x7f000001U, port};
    }

    [[nodiscard]] constexpr std::uint32_t ipv4_host_order() const noexcept
    {
        return ipv4_host_order_;
    }

    [[nodiscard]] constexpr std::uint16_t port() const noexcept
    {
        return port_;
    }

    [[nodiscard]] std::string to_string() const;

    [[nodiscard]] friend constexpr bool operator==(
        const NetworkAddress& left,
        const NetworkAddress& right) noexcept = default;

private:
    std::uint32_t ipv4_host_order_{0};
    std::uint16_t port_{0};
};

} // namespace hlclient::network
