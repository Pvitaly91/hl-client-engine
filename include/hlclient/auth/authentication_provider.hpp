#pragma once

#include <hlclient/goldsrc/connect_request.hpp>
#include <hlclient/network/network_address.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::auth {

struct AuthenticationRequestContext {
    network::NetworkAddress remote_endpoint;
    goldsrc::ProtocolVersion protocol{goldsrc::ProtocolVersion::goldsrc_48};
    goldsrc::ConnectCompatibilityProfile compatibility_profile;
    std::optional<goldsrc::ChallengeToken> challenge;
};

enum class AuthenticationErrorCode {
    unavailable,
    configuration_error,
    provider_error,
    invalid_material,
    material_too_large,
    cancelled,
};

struct AuthenticationError {
    AuthenticationErrorCode code{AuthenticationErrorCode::provider_error};
    std::string context;
};

// An asynchronous provider may use this object to retain and eventually
// release an external ticket or session handle. Its destructor must not log
// ticket bytes or other authentication secrets.
class IAuthenticationSessionLifetime {
public:
    virtual ~IAuthenticationSessionLifetime() = default;

    IAuthenticationSessionLifetime(const IAuthenticationSessionLifetime&) = delete;
    IAuthenticationSessionLifetime& operator=(const IAuthenticationSessionLifetime&) = delete;
    IAuthenticationSessionLifetime(IAuthenticationSessionLifetime&&) = delete;
    IAuthenticationSessionLifetime& operator=(IAuthenticationSessionLifetime&&) = delete;

protected:
    IAuthenticationSessionLifetime() = default;
};

// Owns both the wire material and the provider-specific lifetime guard. The
// material can be transferred exactly once into a PreparedConnectRequest. The
// session itself must then remain alive through the terminal handshake state so
// a future provider's ticket is not released prematurely.
class AuthenticationSession final {
public:
    explicit AuthenticationSession(
        goldsrc::AuthenticationMaterial material,
        std::unique_ptr<IAuthenticationSessionLifetime> lifetime = {}) noexcept;

    ~AuthenticationSession() = default;
    AuthenticationSession(AuthenticationSession&& other) noexcept;
    AuthenticationSession& operator=(AuthenticationSession&& other) noexcept;
    AuthenticationSession(const AuthenticationSession&) = delete;
    AuthenticationSession& operator=(const AuthenticationSession&) = delete;

    [[nodiscard]] bool has_material() const noexcept;
    [[nodiscard]] std::size_t material_size() const noexcept;
    [[nodiscard]] std::size_t material_suffix_size() const noexcept;

    // Moving the material out does not release the provider lifetime guard.
    [[nodiscard]] std::optional<goldsrc::AuthenticationMaterial> take_material() noexcept;

private:
    // Declared first so material_ is destroyed before the lifetime guard.
    std::unique_ptr<IAuthenticationSessionLifetime> lifetime_;
    std::optional<goldsrc::AuthenticationMaterial> material_;
};

enum class AuthenticationUpdateState {
    pending,
    succeeded,
    failed,
};

struct AuthenticationUpdateResult {
    AuthenticationUpdateState state{AuthenticationUpdateState::pending};
    std::optional<AuthenticationSession> session;
    std::optional<AuthenticationError> error;

    [[nodiscard]] static AuthenticationUpdateResult pending();
    [[nodiscard]] static AuthenticationUpdateResult succeeded(AuthenticationSession session);
    [[nodiscard]] static AuthenticationUpdateResult failed(AuthenticationError error);
};

class IAuthenticationOperation {
public:
    virtual ~IAuthenticationOperation() = default;

    IAuthenticationOperation(const IAuthenticationOperation&) = delete;
    IAuthenticationOperation& operator=(const IAuthenticationOperation&) = delete;
    IAuthenticationOperation(IAuthenticationOperation&&) = delete;
    IAuthenticationOperation& operator=(IAuthenticationOperation&&) = delete;

    // update() is a polling boundary: an asynchronous provider can report
    // pending, while a bounded synchronous provider may complete on its first
    // call.
    [[nodiscard]] virtual AuthenticationUpdateResult update() = 0;
    virtual void cancel() noexcept = 0;

protected:
    IAuthenticationOperation() = default;
};

struct AuthenticationBeginResult {
    std::unique_ptr<IAuthenticationOperation> operation;
    std::optional<AuthenticationError> error;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] static AuthenticationBeginResult
    started(std::unique_ptr<IAuthenticationOperation> operation);
    [[nodiscard]] static AuthenticationBeginResult failed(AuthenticationError error);
};

class IAuthenticationProvider {
public:
    virtual ~IAuthenticationProvider() = default;

    IAuthenticationProvider(const IAuthenticationProvider&) = delete;
    IAuthenticationProvider& operator=(const IAuthenticationProvider&) = delete;
    IAuthenticationProvider(IAuthenticationProvider&&) = delete;
    IAuthenticationProvider& operator=(IAuthenticationProvider&&) = delete;

    // The returned operation owns any in-progress provider work and permits a
    // future platform provider to complete asynchronously without receiving
    // access to the engine, renderer, filesystem, or network socket.
    [[nodiscard]] virtual AuthenticationBeginResult
    begin(const AuthenticationRequestContext& context) = 0;

protected:
    IAuthenticationProvider() = default;
};

[[nodiscard]] constexpr std::string_view to_string(const AuthenticationErrorCode code) noexcept
{
    switch (code) {
    case AuthenticationErrorCode::unavailable:
        return "unavailable";
    case AuthenticationErrorCode::configuration_error:
        return "configuration_error";
    case AuthenticationErrorCode::provider_error:
        return "provider_error";
    case AuthenticationErrorCode::invalid_material:
        return "invalid_material";
    case AuthenticationErrorCode::material_too_large:
        return "material_too_large";
    case AuthenticationErrorCode::cancelled:
        return "cancelled";
    }
    return "unknown";
}

} // namespace hlclient::auth
