#include <hlclient/app/steam_authentication_provider.hpp>

#include <hlclient/hash/md5.hpp>

#include <array>
#include <charconv>
#include <limits>
#include <random>
#include <utility>
#include <vector>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#endif

namespace hlclient::app {
namespace {

[[nodiscard]] auth::AuthenticationError error(
    const auth::AuthenticationErrorCode code,
    std::string context)
{
    return auth::AuthenticationError{code, std::move(context)};
}

void trace(
    const SteamAuthenticationTraceCallback& callback,
    const SteamAuthenticationTraceStatus status,
    const std::optional<auth::AuthenticationErrorCode> code = std::nullopt,
    const std::size_t material_size = 0U,
    const std::optional<SteamAuthenticationPhase> phase = std::nullopt,
    const std::chrono::milliseconds elapsed = std::chrono::milliseconds::zero())
{
    if (callback) {
        try {
            callback(SteamAuthenticationTraceEvent{
                status, code, material_size, phase, elapsed});
        } catch (...) {
            // Diagnostics are observational only. In particular, an injected
            // callback must not interrupt ticket/session termination or API
            // shutdown from a noexcept destructor path.
        }
    }
}

[[nodiscard]] std::optional<std::array<std::byte, 32U>> make_fresh_cdkey()
{
    try {
        std::random_device entropy;
        std::uniform_int_distribution<std::uint32_t> distribution{
            0U, 0x7fff'fffeU};
        const auto nonce = distribution(entropy);
        std::array<char, 16U> decimal{};
        const auto converted = std::to_chars(
            decimal.data(), decimal.data() + decimal.size(), nonce, 10);
        if (converted.ec != std::errc{}) {
            return std::nullopt;
        }

        hash::Md5Hasher hasher;
        const auto input = std::as_bytes(std::span{
            decimal.data(), static_cast<std::size_t>(converted.ptr - decimal.data())});
        if (!hasher.update(input)) {
            return std::nullopt;
        }
        const auto digest = hasher.finalize();
        if (!digest) {
            return std::nullopt;
        }

        constexpr std::string_view digits{"0123456789abcdef"};
        std::array<std::byte, 32U> result{};
        for (std::size_t index = 0U; index < digest->size(); ++index) {
            const auto value = std::to_integer<unsigned int>((*digest)[index]);
            result[index * 2U] =
                std::byte{static_cast<unsigned char>(digits[value >> 4U])};
            result[index * 2U + 1U] =
                std::byte{static_cast<unsigned char>(digits[value & 0x0fU])};
        }
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

#ifdef _WIN32
class DynamicSteamLegacyClientApi final : public ISteamLegacyClientApi {
public:
    explicit DynamicSteamLegacyClientApi(std::filesystem::path runtime_library)
        : runtime_library_{std::move(runtime_library)}
    {
    }

    ~DynamicSteamLegacyClientApi() override
    {
        shutdown();
    }

    [[nodiscard]] bool initialize() noexcept override
    {
        if (initialized_) {
            return true;
        }
        std::error_code filesystem_error;
        if (!runtime_library_.is_absolute() ||
            !std::filesystem::is_regular_file(runtime_library_, filesystem_error) ||
            filesystem_error) {
            return false;
        }

        module_ = LoadLibraryExW(
            runtime_library_.c_str(),
            nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module_ == nullptr || !resolve_exports()) {
            unload();
            return false;
        }
        if (!init_()) {
            unload();
            return false;
        }
        user_ = steam_user_();
        utils_ = steam_utils_();
        if (user_ == nullptr || utils_ == nullptr) {
            shutdown_();
            unload();
            return false;
        }
        initialized_ = true;
        return true;
    }

    void shutdown() noexcept override
    {
        if (initialized_) {
            shutdown_();
            initialized_ = false;
        }
        user_ = nullptr;
        utils_ = nullptr;
        unload();
    }

    void run_callbacks() noexcept override
    {
        if (initialized_) {
            run_callbacks_();
        }
    }

    [[nodiscard]] std::uint32_t app_id() const noexcept override
    {
        return initialized_ ? get_app_id_(utils_) : 0U;
    }

    [[nodiscard]] bool logged_on() const noexcept override
    {
        return initialized_ && logged_on_(user_);
    }

    [[nodiscard]] int initiate_game_connection(
        const std::span<std::byte> output,
        const std::uint64_t game_server_steam_id,
        const std::uint32_t server_ipv4_host_order,
        const std::uint16_t server_port_host_order,
        const bool secure) noexcept override
    {
        if (!initialized_ || output.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
            return 0;
        }
        return initiate_(
            user_,
            output.data(),
            static_cast<int>(output.size()),
            game_server_steam_id,
            server_ipv4_host_order,
            server_port_host_order,
            secure);
    }

    void terminate_game_connection(
        const std::uint32_t server_ipv4_host_order,
        const std::uint16_t server_port_host_order) noexcept override
    {
        if (initialized_) {
            terminate_(user_, server_ipv4_host_order, server_port_host_order);
        }
    }

private:
    using Init = bool(__cdecl*)();
    using Shutdown = void(__cdecl*)();
    using RunCallbacks = void(__cdecl*)();
    using Interface = void*(__cdecl*)();
    using LoggedOn = bool(__cdecl*)(void*);
    using GetAppId = std::uint32_t(__cdecl*)(void*);
    using Initiate = int(__cdecl*)(
        void*, void*, int, std::uint64_t, std::uint32_t, std::uint16_t, bool);
    using Terminate = void(__cdecl*)(void*, std::uint32_t, std::uint16_t);

    template<class Function>
    [[nodiscard]] bool resolve(Function& output, const char* name) noexcept
    {
        output = reinterpret_cast<Function>(GetProcAddress(module_, name));
        return output != nullptr;
    }

    [[nodiscard]] bool resolve_exports() noexcept
    {
        return resolve(init_, "SteamAPI_Init") &&
               resolve(shutdown_, "SteamAPI_Shutdown") &&
               resolve(run_callbacks_, "SteamAPI_RunCallbacks") &&
               resolve(steam_user_, "SteamAPI_SteamUser_v021") &&
               resolve(steam_utils_, "SteamAPI_SteamUtils_v010") &&
               resolve(logged_on_, "SteamAPI_ISteamUser_BLoggedOn") &&
               resolve(get_app_id_, "SteamAPI_ISteamUtils_GetAppID") &&
               resolve(
                   initiate_,
                   "SteamAPI_ISteamUser_InitiateGameConnection_DEPRECATED") &&
               resolve(
                   terminate_,
                   "SteamAPI_ISteamUser_TerminateGameConnection_DEPRECATED");
    }

    void unload() noexcept
    {
        if (module_ != nullptr) {
            FreeLibrary(module_);
            module_ = nullptr;
        }
    }

    std::filesystem::path runtime_library_;
    HMODULE module_{nullptr};
    void* user_{nullptr};
    void* utils_{nullptr};
    Init init_{nullptr};
    Shutdown shutdown_{nullptr};
    RunCallbacks run_callbacks_{nullptr};
    Interface steam_user_{nullptr};
    Interface steam_utils_{nullptr};
    LoggedOn logged_on_{nullptr};
    GetAppId get_app_id_{nullptr};
    Initiate initiate_{nullptr};
    Terminate terminate_{nullptr};
    bool initialized_{false};
};
#endif

} // namespace

class SteamAuthenticationProvider::State final {
public:
    State(
        std::unique_ptr<ISteamLegacyClientApi> api,
        SteamAuthenticationTraceCallback trace_callback)
        : api_{std::move(api)}, trace_{std::move(trace_callback)}
    {
    }

    ~State()
    {
        if (api_ && initialized_) {
            api_->shutdown();
            trace(trace_, SteamAuthenticationTraceStatus::api_shutdown);
        }
    }

    std::unique_ptr<ISteamLegacyClientApi> api_;
    SteamAuthenticationTraceCallback trace_;
    bool initialized_{false};
};

namespace {

class SteamSessionLifetime final : public auth::IAuthenticationSessionLifetime {
public:
    SteamSessionLifetime(
        std::shared_ptr<SteamAuthenticationProvider::State> state,
        const network::NetworkAddress endpoint)
        : state_{std::move(state)}, endpoint_{endpoint}
    {
    }

    ~SteamSessionLifetime() override
    {
        if (state_ && state_->api_) {
            state_->api_->terminate_game_connection(
                endpoint_.ipv4_host_order(), endpoint_.port());
            trace(
                state_->trace_,
                SteamAuthenticationTraceStatus::session_terminated);
        }
    }

private:
    std::shared_ptr<SteamAuthenticationProvider::State> state_;
    network::NetworkAddress endpoint_;
};

class SteamAuthenticationOperation final : public auth::IAuthenticationOperation {
public:
    SteamAuthenticationOperation(
        std::shared_ptr<SteamAuthenticationProvider::State> state,
        auth::AuthenticationRequestContext context,
        const std::uint32_t required_app_id,
        const std::chrono::milliseconds timeout,
        const std::chrono::milliseconds initialization_timeout,
        SteamAuthenticationNow now)
        : state_{std::move(state)},
          context_{std::move(context)},
          required_app_id_{required_app_id},
          timeout_{timeout},
          initialization_timeout_{initialization_timeout},
          now_{std::move(now)},
          started_at_{now_()}
    {
    }

    ~SteamAuthenticationOperation() override
    {
        if (!complete_) {
            cancel();
        }
    }

    [[nodiscard]] auth::AuthenticationUpdateResult update() override
    {
        if (complete_) {
            return auth::AuthenticationUpdateResult::failed(error(
                auth::AuthenticationErrorCode::provider_error,
                "Steam authentication operation is already complete"));
        }
        if (cancelled_) {
            complete_ = true;
            trace(state_->trace_, SteamAuthenticationTraceStatus::cancelled);
            return auth::AuthenticationUpdateResult::failed(error(
                auth::AuthenticationErrorCode::cancelled,
                "Steam authentication operation was cancelled"));
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now_() - (material_started_at_ ? *material_started_at_ : started_at_));
        if (elapsed >= (material_started_at_ ? timeout_ : initialization_timeout_)) {
            return fail(
                auth::AuthenticationErrorCode::timed_out,
                material_started_at_
                    ? "Steam authentication material acquisition timed out"
                    : "Steam authentication API initialization timed out",
                material_started_at_ ? SteamAuthenticationPhase::material_acquisition
                                     : SteamAuthenticationPhase::initialization,
                elapsed);
        }

        if (!state_->initialized_) {
            trace(
                state_->trace_,
                SteamAuthenticationTraceStatus::api_initializing);
            const bool initialized = state_->api_ && state_->api_->initialize();
            if (initialized) {
                state_->initialized_ = true;
                trace(
                    state_->trace_,
                    SteamAuthenticationTraceStatus::api_initialized,
                    std::nullopt, 0U, SteamAuthenticationPhase::initialization,
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now_() - started_at_));
            }
            const auto initialization_elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now_() - started_at_);
            if (initialization_elapsed >= initialization_timeout_) {
                return fail(
                    auth::AuthenticationErrorCode::timed_out,
                    "Steam authentication API initialization timed out",
                    SteamAuthenticationPhase::initialization,
                    initialization_elapsed);
            }
            if (!initialized) {
                return fail(
                    auth::AuthenticationErrorCode::unavailable,
                    "Steam API runtime or required legacy interface is unavailable");
            }
        }
        if (!readiness_reported_) {
            if (state_->api_->app_id() != required_app_id_) {
                return fail(
                    auth::AuthenticationErrorCode::configuration_error,
                    "Steam API context does not match Half-Life App ID 70");
            }
            if (!state_->api_->logged_on()) {
                return fail(
                    auth::AuthenticationErrorCode::unavailable,
                    "Steam user session is not logged on");
            }
            const auto ready_at = now_();
            const auto initialization_elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    ready_at - started_at_);
            if (initialization_elapsed >= initialization_timeout_) {
                return fail(
                    auth::AuthenticationErrorCode::timed_out,
                    "Steam authentication API initialization timed out",
                    SteamAuthenticationPhase::initialization,
                    initialization_elapsed);
            }
            readiness_reported_ = true;
            material_started_at_ = ready_at;
            trace(
                state_->trace_,
                SteamAuthenticationTraceStatus::material_pending,
                std::nullopt, 0U, SteamAuthenticationPhase::initialization,
                initialization_elapsed);
            return auth::AuthenticationUpdateResult::pending();
        }

        state_->api_->run_callbacks();
        if (!state_->api_->logged_on()) {
            return fail(
                auth::AuthenticationErrorCode::unavailable,
                "Steam user session became unavailable");
        }
        const auto callback_elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now_() - *material_started_at_);
        if (callback_elapsed >= timeout_) {
            return fail(
                auth::AuthenticationErrorCode::timed_out,
                "Steam authentication material acquisition timed out",
                SteamAuthenticationPhase::material_acquisition,
                callback_elapsed);
        }

        std::array<std::byte, kSteamInitiateGameConnectionBufferSize> output{};
        const int length = state_->api_->initiate_game_connection(
            output,
            *context_.game_server_steam_id,
            context_.remote_endpoint.ipv4_host_order(),
            context_.remote_endpoint.port(),
            *context_.game_server_secure);
        const auto material_elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now_() - *material_started_at_);
        if (material_elapsed >= timeout_) {
            if (length > 0) {
                state_->api_->terminate_game_connection(
                    context_.remote_endpoint.ipv4_host_order(),
                    context_.remote_endpoint.port());
            }
            return fail(
                auth::AuthenticationErrorCode::timed_out,
                "Steam authentication material acquisition timed out",
                SteamAuthenticationPhase::material_acquisition,
                material_elapsed);
        }
        if (length <= 0) {
            return fail(
                auth::AuthenticationErrorCode::provider_error,
                "Steam InitiateGameConnection did not return authentication material");
        }

        const auto material_size = static_cast<std::size_t>(length);
        if (material_size > output.size() ||
            material_size > context_.compatibility_profile.required_binary_authentication_size) {
            state_->api_->terminate_game_connection(
                context_.remote_endpoint.ipv4_host_order(),
                context_.remote_endpoint.port());
            return fail(
                auth::AuthenticationErrorCode::material_too_large,
                "Steam authentication material exceeds the verified GoldSrc bound");
        }

        const auto protected_value = make_fresh_cdkey();
        if (!protected_value) {
            state_->api_->terminate_game_connection(
                context_.remote_endpoint.ipv4_host_order(),
                context_.remote_endpoint.port());
            return fail(
                auth::AuthenticationErrorCode::provider_error,
                "Fresh GoldSrc protocol metadata could not be generated");
        }
        auto material = goldsrc::AuthenticationMaterial::create(
            *protected_value,
            std::span<const std::byte>{output}.first(material_size));
        if (!material) {
            state_->api_->terminate_game_connection(
                context_.remote_endpoint.ipv4_host_order(),
                context_.remote_endpoint.port());
            return fail(
                auth::AuthenticationErrorCode::invalid_material,
                "Steam authentication material is invalid for the GoldSrc serializer");
        }

        complete_ = true;
        trace(
            state_->trace_,
            SteamAuthenticationTraceStatus::material_acquired,
            std::nullopt,
            material_size, SteamAuthenticationPhase::material_acquisition,
            material_elapsed);
        return auth::AuthenticationUpdateResult::succeeded(
            auth::AuthenticationSession{
                std::move(*material.value),
                std::make_unique<SteamSessionLifetime>(
                    state_, context_.remote_endpoint)});
    }

    void cancel() noexcept override
    {
        if (!complete_) {
            cancelled_ = true;
        }
    }

private:
    [[nodiscard]] auth::AuthenticationUpdateResult fail(
        const auth::AuthenticationErrorCode code,
        std::string context,
        const std::optional<SteamAuthenticationPhase> phase = std::nullopt,
        const std::chrono::milliseconds elapsed = std::chrono::milliseconds::zero())
    {
        complete_ = true;
        trace(state_->trace_, SteamAuthenticationTraceStatus::failed,
              code, 0U, phase, elapsed);
        return auth::AuthenticationUpdateResult::failed(
            error(code, std::move(context)));
    }

    std::shared_ptr<SteamAuthenticationProvider::State> state_;
    auth::AuthenticationRequestContext context_;
    std::uint32_t required_app_id_{kHalfLifeSteamAppId};
    std::chrono::milliseconds timeout_{};
    std::chrono::milliseconds initialization_timeout_{};
    SteamAuthenticationNow now_;
    SteamAuthenticationClock::time_point started_at_{};
    std::optional<SteamAuthenticationClock::time_point> material_started_at_;
    bool complete_{false};
    bool cancelled_{false};
    bool readiness_reported_{false};
};

} // namespace

std::unique_ptr<ISteamLegacyClientApi> create_dynamic_steam_legacy_client_api(
    const std::filesystem::path& runtime_library)
{
#ifdef _WIN32
    return std::make_unique<DynamicSteamLegacyClientApi>(runtime_library);
#else
    static_cast<void>(runtime_library);
    return {};
#endif
}

SteamAuthenticationProvider::SteamAuthenticationProvider(
    SteamAuthenticationProviderConfig config)
    : config_{std::move(config)}
{
    if (!config_.api_factory) {
        config_.api_factory = &create_dynamic_steam_legacy_client_api;
    }
    if (!config_.now) {
        config_.now = [] { return SteamAuthenticationClock::now(); };
    }
    state_ = std::make_shared<State>(
        config_.api_factory(config_.runtime_library), config_.trace);
}

SteamAuthenticationProvider::~SteamAuthenticationProvider() = default;

auth::AuthenticationBeginResult SteamAuthenticationProvider::begin(
    const auth::AuthenticationRequestContext& context)
{
    if (!state_ || !state_->api_) {
        return auth::AuthenticationBeginResult::failed(error(
            auth::AuthenticationErrorCode::unavailable,
            "Steam legacy client API is unavailable on this platform"));
    }
    if (config_.timeout <= std::chrono::milliseconds::zero() ||
        config_.initialization_timeout <= std::chrono::milliseconds::zero() ||
        config_.required_app_id != kHalfLifeSteamAppId ||
        context.protocol != goldsrc::ProtocolVersion::goldsrc_48 ||
        !context.challenge || !context.game_server_steam_id ||
        *context.game_server_steam_id == 0U || !context.game_server_secure ||
        context.remote_endpoint.ipv4_host_order() == 0U ||
        context.remote_endpoint.port() == 0U ||
        !context.compatibility_profile.variable_binary_authentication_size) {
        return auth::AuthenticationBeginResult::failed(error(
            auth::AuthenticationErrorCode::configuration_error,
            "Steam authentication requires a fresh Protocol 48 server challenge and Steam profile"));
    }
    trace(state_->trace_, SteamAuthenticationTraceStatus::operation_started);
    return auth::AuthenticationBeginResult::started(
        std::make_unique<SteamAuthenticationOperation>(
            state_,
            context,
            config_.required_app_id,
            config_.timeout,
            config_.initialization_timeout,
            config_.now));
}

} // namespace hlclient::app
