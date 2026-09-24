#pragma once

#include <hlclient/auth/authentication_provider.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace hlclient::app {

inline constexpr std::uint32_t kHalfLifeSteamAppId = 70U;
inline constexpr std::size_t kSteamInitiateGameConnectionBufferSize = 2'048U;
inline constexpr std::string_view kSteamLegacyAuthenticationApi =
    "ISteamUser.v021/InitiateGameConnection";

enum class SteamAuthenticationTraceStatus {
    operation_started,
    api_initializing,
    api_initialized,
    material_pending,
    material_acquired,
    session_terminated,
    api_shutdown,
    failed,
    cancelled,
};

struct SteamAuthenticationTraceEvent {
    SteamAuthenticationTraceStatus status{
        SteamAuthenticationTraceStatus::api_initializing};
    std::optional<auth::AuthenticationErrorCode> error;
    std::size_t material_size{0U};
};

using SteamAuthenticationTraceCallback =
    std::function<void(const SteamAuthenticationTraceEvent&)>;

// Narrow seam around the official Steamworks flat C ABI. Tests inject this
// interface; the production factory loads an explicitly named runtime DLL.
class ISteamLegacyClientApi {
public:
    virtual ~ISteamLegacyClientApi() = default;

    [[nodiscard]] virtual bool initialize() noexcept = 0;
    virtual void shutdown() noexcept = 0;
    virtual void run_callbacks() noexcept = 0;
    [[nodiscard]] virtual std::uint32_t app_id() const noexcept = 0;
    [[nodiscard]] virtual bool logged_on() const noexcept = 0;
    [[nodiscard]] virtual int initiate_game_connection(
        std::span<std::byte> output,
        std::uint64_t game_server_steam_id,
        std::uint32_t server_ipv4_host_order,
        std::uint16_t server_port_host_order,
        bool secure) noexcept = 0;
    virtual void terminate_game_connection(
        std::uint32_t server_ipv4_host_order,
        std::uint16_t server_port_host_order) noexcept = 0;
};

using SteamLegacyClientApiFactory = std::function<
    std::unique_ptr<ISteamLegacyClientApi>(const std::filesystem::path&)>;
using SteamAuthenticationClock = std::chrono::steady_clock;
using SteamAuthenticationNow =
    std::function<SteamAuthenticationClock::time_point()>;

struct SteamAuthenticationProviderConfig {
    std::filesystem::path runtime_library;
    std::uint32_t required_app_id{kHalfLifeSteamAppId};
    std::chrono::milliseconds timeout{5'000};
    SteamLegacyClientApiFactory api_factory;
    SteamAuthenticationNow now;
    SteamAuthenticationTraceCallback trace;
};

[[nodiscard]] std::unique_ptr<ISteamLegacyClientApi>
create_dynamic_steam_legacy_client_api(const std::filesystem::path& runtime_library);

class SteamAuthenticationProvider final : public auth::IAuthenticationProvider {
public:
    class State;

    explicit SteamAuthenticationProvider(SteamAuthenticationProviderConfig config);
    ~SteamAuthenticationProvider() override;

    [[nodiscard]] auth::AuthenticationBeginResult
    begin(const auth::AuthenticationRequestContext& context) override;

private:
    SteamAuthenticationProviderConfig config_;
    std::shared_ptr<State> state_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const SteamAuthenticationTraceStatus status) noexcept
{
    switch (status) {
    case SteamAuthenticationTraceStatus::operation_started:
        return "operation_started";
    case SteamAuthenticationTraceStatus::api_initializing:
        return "api_initializing";
    case SteamAuthenticationTraceStatus::api_initialized:
        return "api_initialized";
    case SteamAuthenticationTraceStatus::material_pending:
        return "material_pending";
    case SteamAuthenticationTraceStatus::material_acquired:
        return "material_acquired";
    case SteamAuthenticationTraceStatus::session_terminated:
        return "session_terminated";
    case SteamAuthenticationTraceStatus::api_shutdown:
        return "api_shutdown";
    case SteamAuthenticationTraceStatus::failed:
        return "failed";
    case SteamAuthenticationTraceStatus::cancelled:
        return "cancelled";
    }
    return "unknown";
}

} // namespace hlclient::app
