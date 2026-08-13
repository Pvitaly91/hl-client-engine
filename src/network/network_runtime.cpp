#include <hlclient/network/network_runtime.hpp>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <winsock2.h>
#endif

namespace hlclient::network {

struct NetworkRuntime::Impl final {
    Impl()
    {
#ifdef _WIN32
        WSADATA data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0) {
            error_message = "WSAStartup failed with error " + std::to_string(result);
            return;
        }
        if (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 2) {
            error_message = "Winsock 2.2 is unavailable";
            WSACleanup();
            return;
        }
#endif
        initialized = true;
    }

    ~Impl()
    {
#ifdef _WIN32
        if (initialized) {
            WSACleanup();
        }
#endif
    }

    bool initialized{false};
    std::string error_message;
};

NetworkRuntime::NetworkRuntime() : implementation_{std::make_shared<Impl>()} {}

NetworkRuntime::~NetworkRuntime() = default;

bool NetworkRuntime::valid() const noexcept
{
    return implementation_->initialized;
}

const std::string& NetworkRuntime::error_message() const noexcept
{
    return implementation_->error_message;
}

} // namespace hlclient::network
