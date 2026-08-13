#pragma once

#include <hlclient/network/network_address.hpp>

#include <string_view>

namespace hlclient::goldsrc {

inline constexpr std::string_view kHalfLifeSdkRevision =
    "b1b5cf5892918535619b2937bb927e46cb097ba1";

class ConnectionIntent final {
public:
    explicit ConnectionIntent(network::NetworkAddress server) noexcept;

    [[nodiscard]] const network::NetworkAddress& server() const noexcept;
    [[nodiscard]] static constexpr std::string_view status() noexcept
    {
        return "GoldSrc connection protocol is not implemented yet";
    }

private:
    network::NetworkAddress server_;
};

} // namespace hlclient::goldsrc
