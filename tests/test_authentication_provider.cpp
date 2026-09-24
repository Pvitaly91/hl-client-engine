#include <hlclient/app/authentication_material_file.hpp>
#include <hlclient/app/explicit_file_authentication_provider.hpp>
#include <hlclient/app/steam_authentication_provider.hpp>
#include <hlclient/auth/authentication_provider.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace app = hlclient::app;
namespace auth = hlclient::auth;
namespace goldsrc = hlclient::goldsrc;
namespace network = hlclient::network;

inline constexpr std::string_view kSensitiveSentinel = "TEST_AUTH_PROVIDER_SECRET";

static_assert(!std::is_copy_constructible_v<auth::AuthenticationSession>);
static_assert(!std::is_copy_assignable_v<auth::AuthenticationSession>);
static_assert(std::is_nothrow_move_constructible_v<auth::AuthenticationSession>);
static_assert(std::is_nothrow_move_assignable_v<auth::AuthenticationSession>);
static_assert(!std::is_convertible_v<auth::AuthenticationSession, std::string>);
static_assert(!std::is_convertible_v<auth::AuthenticationSession, std::string_view>);
static_assert(!std::is_copy_constructible_v<auth::AuthenticationUpdateResult>);
static_assert(!std::is_copy_assignable_v<auth::AuthenticationUpdateResult>);
static_assert(!std::is_copy_constructible_v<auth::AuthenticationBeginResult>);
static_assert(!std::is_copy_assignable_v<auth::AuthenticationBeginResult>);

class ScopedTemporaryDirectory final {
public:
    ScopedTemporaryDirectory()
        : temporary_root_{std::filesystem::temp_directory_path().lexically_normal()}
    {
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (std::size_t attempt = 0; attempt < 100U; ++attempt) {
            const auto name = std::string{"hlclient-auth-provider-"} + std::to_string(timestamp) +
                              '-' + std::to_string(attempt);
            auto candidate = temporary_root_ / name;
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                path_ = std::move(candidate);
                return;
            }
            if (error) {
                throw std::runtime_error{"Unable to create authentication-provider test input"};
            }
        }
        throw std::runtime_error{"Unable to allocate authentication-provider test directory"};
    }

    ~ScopedTemporaryDirectory()
    {
        const auto normalized = path_.lexically_normal();
        if (!normalized.empty() && normalized.parent_path() == temporary_root_ &&
            normalized.filename().string().starts_with("hlclient-auth-provider-")) {
            std::error_code ignored;
            std::filesystem::remove_all(normalized, ignored);
        }
    }

    ScopedTemporaryDirectory(const ScopedTemporaryDirectory&) = delete;
    ScopedTemporaryDirectory& operator=(const ScopedTemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path temporary_root_;
    std::filesystem::path path_;
};

[[nodiscard]] std::vector<std::byte> exact_file_record()
{
    std::vector<std::byte> bytes(app::kAuthenticationMaterialFileSize, std::byte{'a'});
    for (std::size_t index = app::kAuthenticationMaterialProtectedFileSize; index < bytes.size();
         ++index) {
        bytes[index] = static_cast<std::byte>(
            static_cast<unsigned char>(kSensitiveSentinel[index % kSensitiveSentinel.size()]));
    }
    return bytes;
}

void write_bytes(const std::filesystem::path& path, const std::span<const std::byte> bytes)
{
    std::ofstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error{"Unable to create authentication-provider test input"};
    }
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw std::runtime_error{"Unable to write authentication-provider test input"};
    }
}

[[nodiscard]] goldsrc::AuthenticationMaterial make_material()
{
    std::array<std::byte, app::kAuthenticationMaterialProtectedFileSize> protected_bytes{};
    protected_bytes.fill(std::byte{'a'});
    std::vector<std::byte> suffix(goldsrc::kObservedConnectAuthenticationSuffixSize);
    for (std::size_t index = 0; index < suffix.size(); ++index) {
        suffix[index] = static_cast<std::byte>(
            static_cast<unsigned char>(kSensitiveSentinel[index % kSensitiveSentinel.size()]));
    }

    auto result = goldsrc::AuthenticationMaterial::create(protected_bytes, suffix);
    if (!result) {
        throw std::runtime_error{"Unable to create synthetic authentication material"};
    }
    return std::move(*result.value);
}

[[nodiscard]] auth::AuthenticationRequestContext request_context()
{
    return auth::AuthenticationRequestContext{
        network::NetworkAddress::loopback(27'015U),
        goldsrc::ProtocolVersion::goldsrc_48,
        goldsrc::ConnectCompatibilityProfile{},
        goldsrc::ChallengeToken{123'456'789U},
    };
}

void check_sanitized(const auth::AuthenticationError& error,
                     const std::filesystem::path& sensitive_path = {})
{
    CHECK(error.context.find(kSensitiveSentinel) == std::string::npos);
    if (!sensitive_path.empty()) {
        CHECK(error.context.find(sensitive_path.string()) == std::string::npos);
    }
}

class ReleaseProbe final : public auth::IAuthenticationSessionLifetime {
public:
    explicit ReleaseProbe(std::size_t& releases) noexcept : releases_{releases} {}
    ~ReleaseProbe() override
    {
        ++releases_;
    }

private:
    std::size_t& releases_;
};

class DeferredSuccessOperation final : public auth::IAuthenticationOperation {
public:
    DeferredSuccessOperation(goldsrc::AuthenticationMaterial material,
                             std::unique_ptr<auth::IAuthenticationSessionLifetime> lifetime)
        : session_{auth::AuthenticationSession{std::move(material), std::move(lifetime)}}
    {
    }

    [[nodiscard]] auth::AuthenticationUpdateResult update() override
    {
        if (cancelled_) {
            complete_ = true;
            return auth::AuthenticationUpdateResult::failed(auth::AuthenticationError{
                auth::AuthenticationErrorCode::cancelled,
                "Synthetic authentication operation was cancelled",
            });
        }
        if (complete_) {
            return auth::AuthenticationUpdateResult::failed(auth::AuthenticationError{
                auth::AuthenticationErrorCode::provider_error,
                "Synthetic authentication operation is complete",
            });
        }
        if (!reported_pending_) {
            reported_pending_ = true;
            return auth::AuthenticationUpdateResult::pending();
        }

        complete_ = true;
        auto result = auth::AuthenticationUpdateResult::succeeded(std::move(*session_));
        session_.reset();
        return result;
    }

    void cancel() noexcept override
    {
        if (!complete_) {
            cancelled_ = true;
        }
    }

private:
    std::optional<auth::AuthenticationSession> session_;
    bool reported_pending_{false};
    bool complete_{false};
    bool cancelled_{false};
};

class DeferredSuccessProvider final : public auth::IAuthenticationProvider {
public:
    DeferredSuccessProvider(goldsrc::AuthenticationMaterial material,
                            std::unique_ptr<auth::IAuthenticationSessionLifetime> lifetime)
        : material_{std::move(material)}, lifetime_{std::move(lifetime)}
    {
    }

    [[nodiscard]] auth::AuthenticationBeginResult
    begin(const auth::AuthenticationRequestContext& context) override
    {
        began_ = true;
        observed_endpoint_ = context.remote_endpoint;
        observed_challenge_ = context.challenge;
        if (!material_) {
            return auth::AuthenticationBeginResult::failed(auth::AuthenticationError{
                auth::AuthenticationErrorCode::provider_error,
                "Synthetic authentication provider was already used",
            });
        }

        auto operation =
            std::make_unique<DeferredSuccessOperation>(std::move(*material_), std::move(lifetime_));
        material_.reset();
        return auth::AuthenticationBeginResult::started(std::move(operation));
    }

    [[nodiscard]] bool began() const noexcept
    {
        return began_;
    }
    [[nodiscard]] const network::NetworkAddress& observed_endpoint() const noexcept
    {
        return observed_endpoint_;
    }
    [[nodiscard]] const std::optional<goldsrc::ChallengeToken>& observed_challenge() const noexcept
    {
        return observed_challenge_;
    }

private:
    std::optional<goldsrc::AuthenticationMaterial> material_;
    std::unique_ptr<auth::IAuthenticationSessionLifetime> lifetime_;
    network::NetworkAddress observed_endpoint_;
    std::optional<goldsrc::ChallengeToken> observed_challenge_;
    bool began_{false};
};

class ProviderErrorOperation final : public auth::IAuthenticationOperation {
public:
    [[nodiscard]] auth::AuthenticationUpdateResult update() override
    {
        return auth::AuthenticationUpdateResult::failed(auth::AuthenticationError{
            auth::AuthenticationErrorCode::provider_error,
            "Synthetic provider failed without exposing protected data",
        });
    }

    void cancel() noexcept override {}
};

struct MockSteamApiState {
    bool initialize_result{true};
    bool logged_on{true};
    std::uint32_t app_id{app::kHalfLifeSteamAppId};
    int material_size{333};
    std::size_t initialize_calls{0U};
    std::size_t shutdown_calls{0U};
    std::size_t callback_calls{0U};
    std::size_t initiate_calls{0U};
    std::size_t terminate_calls{0U};
    std::uint64_t server_steam_id{0U};
    network::NetworkAddress endpoint;
    bool secure{false};
};

class MockSteamApi final : public app::ISteamLegacyClientApi {
public:
    explicit MockSteamApi(std::shared_ptr<MockSteamApiState> state)
        : state_{std::move(state)}
    {
    }

    [[nodiscard]] bool initialize() noexcept override
    {
        ++state_->initialize_calls;
        return state_->initialize_result;
    }
    void shutdown() noexcept override { ++state_->shutdown_calls; }
    void run_callbacks() noexcept override { ++state_->callback_calls; }
    [[nodiscard]] std::uint32_t app_id() const noexcept override
    {
        return state_->app_id;
    }
    [[nodiscard]] bool logged_on() const noexcept override
    {
        return state_->logged_on;
    }
    [[nodiscard]] int initiate_game_connection(
        const std::span<std::byte> output,
        const std::uint64_t server_steam_id,
        const std::uint32_t ipv4,
        const std::uint16_t port,
        const bool secure) noexcept override
    {
        ++state_->initiate_calls;
        state_->server_steam_id = server_steam_id;
        state_->endpoint = network::NetworkAddress{ipv4, port};
        state_->secure = secure;
        if (state_->material_size > 0 &&
            static_cast<std::size_t>(state_->material_size) <= output.size()) {
            std::ranges::fill(
                output.first(static_cast<std::size_t>(state_->material_size)),
                std::byte{0x5a});
        }
        return state_->material_size;
    }
    void terminate_game_connection(
        const std::uint32_t ipv4,
        const std::uint16_t port) noexcept override
    {
        ++state_->terminate_calls;
        state_->endpoint = network::NetworkAddress{ipv4, port};
    }

private:
    std::shared_ptr<MockSteamApiState> state_;
};

[[nodiscard]] auth::AuthenticationRequestContext steam_request_context()
{
    auth::AuthenticationRequestContext context;
    context.remote_endpoint = network::NetworkAddress::loopback(27'015U);
    context.protocol = goldsrc::ProtocolVersion::goldsrc_48;
    context.compatibility_profile = goldsrc::steam_legacy_connect_profile();
    context.challenge = goldsrc::ChallengeToken{123'456'789U};
    context.game_server_steam_id = 90'071'992'547'410'001ULL;
    context.game_server_secure = true;
    return context;
}

[[nodiscard]] app::SteamAuthenticationProviderConfig mock_steam_config(
    const std::shared_ptr<MockSteamApiState>& state)
{
    app::SteamAuthenticationProviderConfig config;
    config.api_factory = [state](const std::filesystem::path&) {
        return std::make_unique<MockSteamApi>(state);
    };
    return config;
}

TEST_CASE("Authentication provider supports deferred success and a minimal context",
          "[auth][provider]")
{
    std::size_t releases = 0U;
    DeferredSuccessProvider provider{
        make_material(),
        std::make_unique<ReleaseProbe>(releases),
    };
    const auto context = request_context();

    auto begun = provider.begin(context);
    REQUIRE(begun);
    REQUIRE(begun.operation);
    CHECK(provider.began());
    CHECK(provider.observed_endpoint() == context.remote_endpoint);
    CHECK(provider.observed_challenge() == context.challenge);

    const auto pending = begun.operation->update();
    CHECK(pending.state == auth::AuthenticationUpdateState::pending);
    CHECK_FALSE(pending.session);
    CHECK_FALSE(pending.error);

    auto completed = begun.operation->update();
    REQUIRE(completed.state == auth::AuthenticationUpdateState::succeeded);
    REQUIRE(completed.session);
    CHECK_FALSE(completed.error);
    CHECK(completed.session->has_material());
    CHECK(completed.session->material_size() == app::kAuthenticationMaterialFileSize);
    CHECK(releases == 0U);

    auto material = completed.session->take_material();
    REQUIRE(material);
    CHECK_FALSE(completed.session->has_material());
    CHECK(completed.session->material_size() == 0U);
    CHECK_FALSE(completed.session->take_material());
    CHECK(releases == 0U);

    material.reset();
    CHECK(releases == 0U);
    completed.session.reset();
    CHECK(releases == 1U);
}

TEST_CASE("Authentication session move operations transfer one release guard",
          "[auth][provider][lifetime]")
{
    std::size_t first_releases = 0U;
    std::size_t second_releases = 0U;
    {
        auth::AuthenticationSession first{
            make_material(),
            std::make_unique<ReleaseProbe>(first_releases),
        };
        auth::AuthenticationSession second{
            make_material(),
            std::make_unique<ReleaseProbe>(second_releases),
        };

        auth::AuthenticationSession moved{std::move(first)};
        CHECK_FALSE(first.has_material());
        CHECK(moved.has_material());
        CHECK(first_releases == 0U);

        second = std::move(moved);
        CHECK(second_releases == 1U);
        CHECK(first_releases == 0U);
        CHECK_FALSE(moved.has_material());
        CHECK(second.has_material());
    }
    CHECK(first_releases == 1U);
    CHECK(second_releases == 1U);
}

TEST_CASE("Authentication operation cancellation releases lifetime exactly once",
          "[auth][provider][lifetime]")
{
    std::size_t releases = 0U;
    {
        DeferredSuccessProvider provider{
            make_material(),
            std::make_unique<ReleaseProbe>(releases),
        };
        auto begun = provider.begin(request_context());
        REQUIRE(begun.operation);
        begun.operation->cancel();

        const auto result = begun.operation->update();
        REQUIRE(result.state == auth::AuthenticationUpdateState::failed);
        REQUIRE(result.error);
        CHECK(result.error->code == auth::AuthenticationErrorCode::cancelled);
        CHECK(releases == 0U);
    }
    CHECK(releases == 1U);
}

TEST_CASE("Authentication provider reports typed provider failures", "[auth][provider]")
{
    auto begun =
        auth::AuthenticationBeginResult::started(std::make_unique<ProviderErrorOperation>());
    REQUIRE(begun.operation);

    const auto result = begun.operation->update();
    REQUIRE(result.state == auth::AuthenticationUpdateState::failed);
    REQUIRE(result.error);
    CHECK(result.error->code == auth::AuthenticationErrorCode::provider_error);
    check_sanitized(*result.error);
}

TEST_CASE("Steam provider polls fresh variable-length legacy material and owns its session",
          "[auth][provider][steam]")
{
    auto state = std::make_shared<MockSteamApiState>();
    std::vector<app::SteamAuthenticationTraceStatus> trace_statuses;
    {
        auto config = mock_steam_config(state);
        config.trace = [&trace_statuses](
            const app::SteamAuthenticationTraceEvent& event) {
            trace_statuses.push_back(event.status);
        };
        app::SteamAuthenticationProvider provider{std::move(config)};
        const auto context = steam_request_context();
        auto begun = provider.begin(context);
        REQUIRE(begun.operation);
        REQUIRE(trace_statuses.size() == 1U);
        CHECK(trace_statuses[0] ==
              app::SteamAuthenticationTraceStatus::operation_started);

        const auto pending = begun.operation->update();
        CHECK(pending.state == auth::AuthenticationUpdateState::pending);
        CHECK(state->initialize_calls == 1U);
        CHECK(state->initiate_calls == 0U);
        REQUIRE(trace_statuses.size() == 4U);
        CHECK(trace_statuses[1] ==
              app::SteamAuthenticationTraceStatus::api_initializing);
        CHECK(trace_statuses[2] ==
              app::SteamAuthenticationTraceStatus::api_initialized);
        CHECK(trace_statuses[3] ==
              app::SteamAuthenticationTraceStatus::material_pending);

        auto completed = begun.operation->update();
        REQUIRE(completed.state == auth::AuthenticationUpdateState::succeeded);
        REQUIRE(completed.session);
        REQUIRE(trace_statuses.size() == 5U);
        CHECK(trace_statuses[4] ==
              app::SteamAuthenticationTraceStatus::material_acquired);
        CHECK(completed.session->material_suffix_size() == 333U);
        CHECK(completed.session->material_size() == 365U);
        CHECK(state->callback_calls == 1U);
        CHECK(state->initiate_calls == 1U);
        CHECK(state->server_steam_id == *context.game_server_steam_id);
        CHECK(state->endpoint == context.remote_endpoint);
        CHECK(state->secure);
        CHECK(state->terminate_calls == 0U);

        auto material = completed.session->take_material();
        REQUIRE(material);
        material.reset();
        CHECK(state->terminate_calls == 0U);
        completed.session.reset();
        CHECK(state->terminate_calls == 1U);

        auto second = provider.begin(context);
        REQUIRE(second.operation);
        CHECK(second.operation->update().state ==
              auth::AuthenticationUpdateState::pending);
        auto second_completed = second.operation->update();
        REQUIRE(second_completed.state ==
                auth::AuthenticationUpdateState::succeeded);
        CHECK(state->initiate_calls == 2U);
        second_completed.session.reset();
        CHECK(state->terminate_calls == 2U);
        CHECK(state->shutdown_calls == 0U);
    }
    CHECK(state->shutdown_calls == 1U);
}

TEST_CASE("Steam provider reports context, runtime, callback, size, cancel, and timeout failures",
          "[auth][provider][steam]")
{
    SECTION("fresh server identity is required")
    {
        auto state = std::make_shared<MockSteamApiState>();
        app::SteamAuthenticationProvider provider{mock_steam_config(state)};
        auto context = steam_request_context();
        context.game_server_steam_id.reset();
        const auto begun = provider.begin(context);
        REQUIRE_FALSE(begun);
        REQUIRE(begun.error);
        CHECK(begun.error->code ==
              auth::AuthenticationErrorCode::configuration_error);
        CHECK(state->initialize_calls == 0U);
    }

    SECTION("runtime initialization failure is typed unavailable")
    {
        auto state = std::make_shared<MockSteamApiState>();
        state->initialize_result = false;
        app::SteamAuthenticationProvider provider{mock_steam_config(state)};
        auto begun = provider.begin(steam_request_context());
        REQUIRE(begun.operation);
        const auto result = begun.operation->update();
        REQUIRE(result.error);
        CHECK(result.error->code == auth::AuthenticationErrorCode::unavailable);
        CHECK(state->initiate_calls == 0U);
    }

    SECTION("wrong app context does not acquire material")
    {
        auto state = std::make_shared<MockSteamApiState>();
        state->app_id = 480U;
        app::SteamAuthenticationProvider provider{mock_steam_config(state)};
        auto begun = provider.begin(steam_request_context());
        REQUIRE(begun.operation);
        const auto result = begun.operation->update();
        REQUIRE(result.error);
        CHECK(result.error->code ==
              auth::AuthenticationErrorCode::configuration_error);
        CHECK(state->initiate_calls == 0U);
    }

    SECTION("callback-observed logout after pending is unavailable")
    {
        auto state = std::make_shared<MockSteamApiState>();
        app::SteamAuthenticationProvider provider{mock_steam_config(state)};
        auto begun = provider.begin(steam_request_context());
        REQUIRE(begun.operation);
        REQUIRE(begun.operation->update().state ==
                auth::AuthenticationUpdateState::pending);
        state->logged_on = false;
        const auto result = begun.operation->update();
        REQUIRE(result.error);
        CHECK(result.error->code == auth::AuthenticationErrorCode::unavailable);
        CHECK(state->initiate_calls == 0U);
    }

    SECTION("oversized API output is terminated once")
    {
        auto state = std::make_shared<MockSteamApiState>();
        state->material_size = 1'024;
        app::SteamAuthenticationProvider provider{mock_steam_config(state)};
        auto begun = provider.begin(steam_request_context());
        REQUIRE(begun.operation);
        REQUIRE(begun.operation->update().state ==
                auth::AuthenticationUpdateState::pending);
        const auto result = begun.operation->update();
        REQUIRE(result.error);
        CHECK(result.error->code ==
              auth::AuthenticationErrorCode::material_too_large);
        CHECK(state->terminate_calls == 1U);
    }

    SECTION("cancel before initialization does not create a session")
    {
        auto state = std::make_shared<MockSteamApiState>();
        app::SteamAuthenticationProvider provider{mock_steam_config(state)};
        auto begun = provider.begin(steam_request_context());
        REQUIRE(begun.operation);
        begun.operation->cancel();
        const auto result = begun.operation->update();
        REQUIRE(result.error);
        CHECK(result.error->code == auth::AuthenticationErrorCode::cancelled);
        CHECK(state->initialize_calls == 0U);
        CHECK(state->terminate_calls == 0U);
    }

    SECTION("bounded timeout happens before API work")
    {
        auto state = std::make_shared<MockSteamApiState>();
        auto now = app::SteamAuthenticationClock::time_point{};
        auto config = mock_steam_config(state);
        config.timeout = std::chrono::milliseconds{25};
        config.now = [&now] { return now; };
        app::SteamAuthenticationProvider provider{std::move(config)};
        auto begun = provider.begin(steam_request_context());
        REQUIRE(begun.operation);
        now += std::chrono::milliseconds{25};
        const auto result = begun.operation->update();
        REQUIRE(result.error);
        CHECK(result.error->code == auth::AuthenticationErrorCode::timed_out);
        CHECK(state->initialize_calls == 0U);
    }
}

TEST_CASE("Explicit file authentication provider requires a configured path",
          "[auth][provider][file]")
{
    app::ExplicitFileAuthenticationProvider provider{std::filesystem::path{}};

    const auto begun = provider.begin(request_context());

    REQUIRE_FALSE(begun);
    REQUIRE(begun.error);
    CHECK(begun.error->code == auth::AuthenticationErrorCode::configuration_error);
    check_sanitized(*begun.error);
}

TEST_CASE("Explicit file authentication provider reports unavailable input safely",
          "[auth][provider][file]")
{
    ScopedTemporaryDirectory temporary;
    const auto path = temporary.path() / std::string{kSensitiveSentinel};
    app::ExplicitFileAuthenticationProvider provider{path};

    auto begun = provider.begin(request_context());
    REQUIRE(begun.operation);
    const auto result = begun.operation->update();

    REQUIRE(result.state == auth::AuthenticationUpdateState::failed);
    REQUIRE(result.error);
    CHECK(result.error->code == auth::AuthenticationErrorCode::unavailable);
    check_sanitized(*result.error, path);
}

TEST_CASE("Explicit file authentication provider produces one uncached session",
          "[auth][provider][file]")
{
    ScopedTemporaryDirectory temporary;
    const auto path = temporary.path() / "material.bin";
    const auto bytes = exact_file_record();
    write_bytes(path, bytes);
    app::ExplicitFileAuthenticationProvider provider{path};

    auto first_begin = provider.begin(request_context());
    REQUIRE(first_begin.operation);
    auto first = first_begin.operation->update();
    REQUIRE(first.state == auth::AuthenticationUpdateState::succeeded);
    REQUIRE(first.session);
    CHECK(first.session->material_size() == bytes.size());

    std::error_code remove_error;
    const bool removed = std::filesystem::remove(path, remove_error);
    REQUIRE(removed);
    REQUIRE_FALSE(remove_error);

    auto second_begin = provider.begin(request_context());
    REQUIRE(second_begin.operation);
    const auto second = second_begin.operation->update();
    REQUIRE(second.state == auth::AuthenticationUpdateState::failed);
    REQUIRE(second.error);
    CHECK(second.error->code == auth::AuthenticationErrorCode::unavailable);
}

TEST_CASE("Explicit file authentication provider rejects malformed and "
          "oversized material",
          "[auth][provider][file]")
{
    ScopedTemporaryDirectory temporary;

    SECTION("malformed protected region")
    {
        const auto path = temporary.path() / std::string{kSensitiveSentinel};
        auto bytes = exact_file_record();
        bytes[3] = std::byte{0};
        write_bytes(path, bytes);
        app::ExplicitFileAuthenticationProvider provider{path};

        auto begun = provider.begin(request_context());
        REQUIRE(begun.operation);
        const auto result = begun.operation->update();

        REQUIRE(result.state == auth::AuthenticationUpdateState::failed);
        REQUIRE(result.error);
        CHECK(result.error->code == auth::AuthenticationErrorCode::invalid_material);
        check_sanitized(*result.error, path);
    }

    SECTION("oversized record")
    {
        const auto path = temporary.path() / std::string{kSensitiveSentinel};
        auto bytes = exact_file_record();
        bytes.push_back(std::byte{'x'});
        write_bytes(path, bytes);
        app::ExplicitFileAuthenticationProvider provider{path};

        auto begun = provider.begin(request_context());
        REQUIRE(begun.operation);
        const auto result = begun.operation->update();

        REQUIRE(result.state == auth::AuthenticationUpdateState::failed);
        REQUIRE(result.error);
        CHECK(result.error->code == auth::AuthenticationErrorCode::material_too_large);
        check_sanitized(*result.error, path);
    }
}

TEST_CASE("Explicit file authentication operation completes once and honors "
          "cancellation",
          "[auth][provider][file]")
{
    ScopedTemporaryDirectory temporary;
    const auto path = temporary.path() / "material.bin";
    const auto bytes = exact_file_record();
    write_bytes(path, bytes);
    app::ExplicitFileAuthenticationProvider provider{path};

    SECTION("second update is a typed provider error")
    {
        auto begun = provider.begin(request_context());
        REQUIRE(begun.operation);
        auto first = begun.operation->update();
        REQUIRE(first.state == auth::AuthenticationUpdateState::succeeded);

        const auto second = begun.operation->update();
        REQUIRE(second.state == auth::AuthenticationUpdateState::failed);
        REQUIRE(second.error);
        CHECK(second.error->code == auth::AuthenticationErrorCode::provider_error);
        check_sanitized(*second.error, path);
    }

    SECTION("cancel before update")
    {
        auto begun = provider.begin(request_context());
        REQUIRE(begun.operation);
        begun.operation->cancel();

        const auto cancelled = begun.operation->update();
        REQUIRE(cancelled.state == auth::AuthenticationUpdateState::failed);
        REQUIRE(cancelled.error);
        CHECK(cancelled.error->code == auth::AuthenticationErrorCode::cancelled);
        check_sanitized(*cancelled.error, path);
    }
}

} // namespace
