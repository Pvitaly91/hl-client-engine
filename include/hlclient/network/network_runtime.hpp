#pragma once

#include <memory>
#include <string>

namespace hlclient::network {

class UdpSocket;

class NetworkRuntime final {
public:
    NetworkRuntime();
    ~NetworkRuntime();

    NetworkRuntime(const NetworkRuntime&) = delete;
    NetworkRuntime& operator=(const NetworkRuntime&) = delete;
    NetworkRuntime(NetworkRuntime&&) = delete;
    NetworkRuntime& operator=(NetworkRuntime&&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] const std::string& error_message() const noexcept;

private:
    struct Impl;
    std::shared_ptr<Impl> implementation_;

    friend class UdpSocket;
};

} // namespace hlclient::network
