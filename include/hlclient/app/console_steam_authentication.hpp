#pragma once
#include <hlclient/app/steam_authentication_provider.hpp>

namespace hlclient::app {
// Application UI adapter; protocol state/socket ownership remain unchanged.
// pump_teardown runs only on the calling application thread while joining an
// outstanding non-preemptible native ABI call. It must not touch Steam.
[[nodiscard]] std::unique_ptr<auth::IAuthenticationProvider>
make_console_steam_authentication_provider(
    SteamAuthenticationProviderConfig config,
    std::function<void()> pump_teardown = {});
}
