#include <hlclient/goldsrc/compatibility.hpp>

#include <utility>

namespace hlclient::goldsrc {

ConnectionIntent::ConnectionIntent(network::NetworkAddress server) noexcept
    : server_{std::move(server)}
{
}

const network::NetworkAddress& ConnectionIntent::server() const noexcept
{
    return server_;
}

} // namespace hlclient::goldsrc
