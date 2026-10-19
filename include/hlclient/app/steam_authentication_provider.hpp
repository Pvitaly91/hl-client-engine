#pragma once

#include <hlclient/auth/authentication_provider.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
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
    context_checked,
    material_pending,
    material_acquired,
    session_terminated,
    api_shutdown,
    failed,
    cancelled,
};

enum class SteamAuthenticationPhase {
    initialization,
    material_acquisition,
};

// Deliberately excludes the server account ID, full Steam ID, challenge and
// authentication bytes. The Steam ID fields below describe only its class.
struct SteamAuthenticationRequestSummary {
    network::NetworkAddress remote_endpoint;
    goldsrc::ProtocolVersion protocol{goldsrc::ProtocolVersion::goldsrc_48};
    bool server_id_present{false};
    bool server_id_nonzero{false};
    std::optional<std::uint32_t> server_id_universe;
    std::optional<std::uint32_t> server_id_account_type;
    std::optional<std::uint32_t> server_id_instance;
    std::optional<bool> secure;
};

struct SteamAuthenticationTraceEvent {
    SteamAuthenticationTraceStatus status{
        SteamAuthenticationTraceStatus::api_initializing};
    std::optional<auth::AuthenticationErrorCode> error;
    std::size_t material_size{0U};
    std::optional<SteamAuthenticationPhase> phase;
    std::chrono::milliseconds elapsed{0};
    std::optional<SteamAuthenticationRequestSummary> request_context;
    std::optional<std::uint32_t> actual_app_id;
    std::optional<bool> logged_on;
    std::size_t callback_pump_count{0U};
    std::optional<int> initiate_returned_length;
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
    // Fresh-material polling starts after successful API/context readiness.
    // Initialization has a separate acceptance deadline; the synchronous ABI
    // cannot be preempted here, so the managed process host retains its bound.
    std::chrono::milliseconds timeout{5'000};
    std::chrono::milliseconds initialization_timeout{30'000};
    SteamLegacyClientApiFactory api_factory;
    SteamAuthenticationNow now;
    SteamAuthenticationTraceCallback trace;
};

[[nodiscard]] std::unique_ptr<ISteamLegacyClientApi>
create_dynamic_steam_legacy_client_api(const std::filesystem::path& runtime_library);

[[nodiscard]] constexpr std::string_view to_string(
    const SteamAuthenticationPhase phase) noexcept
{
    switch (phase) {
    case SteamAuthenticationPhase::initialization:
        return "initialization";
    case SteamAuthenticationPhase::material_acquisition:
        return "material_acquisition";
    }
    return "unknown";
}

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
    case SteamAuthenticationTraceStatus::context_checked:
        return "context_checked";
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
