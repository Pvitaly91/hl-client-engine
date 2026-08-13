#pragma once

#include <chrono>

namespace hlclient::client {

class ClientWorldState final {
public:
    void reset() noexcept;
    void advance(std::chrono::duration<double> elapsed) noexcept;
    void set_connection_requested(bool requested) noexcept;

    [[nodiscard]] double elapsed_seconds() const noexcept;
    [[nodiscard]] bool connection_requested() const noexcept;

private:
    double elapsed_seconds_{0.0};
    bool connection_requested_{false};
};

} // namespace hlclient::client
