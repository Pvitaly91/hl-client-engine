#include <hlclient/client/client_world_state.hpp>

namespace hlclient::client {

void ClientWorldState::reset() noexcept
{
    elapsed_seconds_ = 0.0;
    connection_requested_ = false;
}

void ClientWorldState::advance(const std::chrono::duration<double> elapsed) noexcept
{
    if (elapsed.count() > 0.0) {
        elapsed_seconds_ += elapsed.count();
    }
}

void ClientWorldState::set_connection_requested(const bool requested) noexcept
{
    connection_requested_ = requested;
}

double ClientWorldState::elapsed_seconds() const noexcept
{
    return elapsed_seconds_;
}

bool ClientWorldState::connection_requested() const noexcept
{
    return connection_requested_;
}

} // namespace hlclient::client
