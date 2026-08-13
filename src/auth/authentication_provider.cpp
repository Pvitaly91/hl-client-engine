#include <hlclient/auth/authentication_provider.hpp>

#include <utility>

namespace hlclient::auth {

AuthenticationSession::AuthenticationSession(
    goldsrc::AuthenticationMaterial material,
    std::unique_ptr<IAuthenticationSessionLifetime> lifetime) noexcept
    : lifetime_{std::move(lifetime)}, material_{std::move(material)}
{
}

AuthenticationSession::AuthenticationSession(AuthenticationSession&& other) noexcept
    : lifetime_{std::move(other.lifetime_)}, material_{std::move(other.material_)}
{
    other.material_.reset();
}

AuthenticationSession& AuthenticationSession::operator=(AuthenticationSession&& other) noexcept
{
    if (this != &other) {
        // Release the old material before its provider-specific lifetime state.
        material_.reset();
        lifetime_.reset();

        lifetime_ = std::move(other.lifetime_);
        material_ = std::move(other.material_);
        other.material_.reset();
    }
    return *this;
}

bool AuthenticationSession::has_material() const noexcept
{
    return material_.has_value();
}

std::size_t AuthenticationSession::material_size() const noexcept
{
    return material_ ? material_->total_size() : 0U;
}

std::size_t AuthenticationSession::material_suffix_size() const noexcept
{
    return material_ ? material_->binary_suffix_size() : 0U;
}

std::optional<goldsrc::AuthenticationMaterial> AuthenticationSession::take_material() noexcept
{
    if (!material_) {
        return std::nullopt;
    }

    std::optional<goldsrc::AuthenticationMaterial> result{std::move(*material_)};
    material_.reset();
    return result;
}

AuthenticationUpdateResult AuthenticationUpdateResult::pending()
{
    return AuthenticationUpdateResult{
        AuthenticationUpdateState::pending,
        std::nullopt,
        std::nullopt,
    };
}

AuthenticationUpdateResult AuthenticationUpdateResult::succeeded(AuthenticationSession session)
{
    return AuthenticationUpdateResult{
        AuthenticationUpdateState::succeeded,
        std::move(session),
        std::nullopt,
    };
}

AuthenticationUpdateResult AuthenticationUpdateResult::failed(AuthenticationError error)
{
    return AuthenticationUpdateResult{
        AuthenticationUpdateState::failed,
        std::nullopt,
        std::move(error),
    };
}

AuthenticationBeginResult::operator bool() const noexcept
{
    return operation != nullptr;
}

AuthenticationBeginResult
AuthenticationBeginResult::started(std::unique_ptr<IAuthenticationOperation> operation)
{
    if (!operation) {
        return failed(AuthenticationError{
            AuthenticationErrorCode::provider_error,
            "Authentication provider did not create an operation",
        });
    }
    return AuthenticationBeginResult{std::move(operation), std::nullopt};
}

AuthenticationBeginResult AuthenticationBeginResult::failed(AuthenticationError error)
{
    return AuthenticationBeginResult{nullptr, std::move(error)};
}

} // namespace hlclient::auth
