#include <hlclient/app/explicit_file_authentication_provider.hpp>

#include <hlclient/app/authentication_material_file.hpp>

#include <memory>
#include <utility>

namespace hlclient::app {
namespace {

[[nodiscard]] auth::AuthenticationError map_file_error(const AuthenticationMaterialFileError& error)
{
    switch (error.code) {
    case AuthenticationMaterialFileErrorCode::open_failed:
        return auth::AuthenticationError{
            auth::AuthenticationErrorCode::unavailable,
            "Explicit authentication material is unavailable",
        };
    case AuthenticationMaterialFileErrorCode::not_regular_file:
        return auth::AuthenticationError{
            auth::AuthenticationErrorCode::configuration_error,
            "Explicit authentication material must be a regular local file",
        };
    case AuthenticationMaterialFileErrorCode::invalid_size:
        if (error.exceeds_maximum_size) {
            return auth::AuthenticationError{
                auth::AuthenticationErrorCode::material_too_large,
                "Explicit authentication material exceeds the supported size",
            };
        }
        return auth::AuthenticationError{
            auth::AuthenticationErrorCode::invalid_material,
            "Explicit authentication material is malformed",
        };
    case AuthenticationMaterialFileErrorCode::invalid_material:
        return auth::AuthenticationError{
            auth::AuthenticationErrorCode::invalid_material,
            "Explicit authentication material is malformed",
        };
    case AuthenticationMaterialFileErrorCode::read_failed:
        return auth::AuthenticationError{
            auth::AuthenticationErrorCode::provider_error,
            "Explicit authentication material could not be read",
        };
    }
    return auth::AuthenticationError{
        auth::AuthenticationErrorCode::provider_error,
        "Explicit authentication provider failed",
    };
}

class ExplicitFileAuthenticationOperation final : public auth::IAuthenticationOperation {
public:
    explicit ExplicitFileAuthenticationOperation(std::filesystem::path path)
        : path_{std::move(path)}
    {
    }

    [[nodiscard]] auth::AuthenticationUpdateResult update() override
    {
        if (complete_) {
            return auth::AuthenticationUpdateResult::failed(auth::AuthenticationError{
                auth::AuthenticationErrorCode::provider_error,
                "Authentication operation is already complete",
            });
        }
        complete_ = true;

        if (cancelled_) {
            return auth::AuthenticationUpdateResult::failed(auth::AuthenticationError{
                auth::AuthenticationErrorCode::cancelled,
                "Authentication operation was cancelled",
            });
        }

        auto loaded = load_authentication_material_file(path_);
        if (!loaded) {
            return auth::AuthenticationUpdateResult::failed(map_file_error(*loaded.error));
        }

        return auth::AuthenticationUpdateResult::succeeded(
            auth::AuthenticationSession{std::move(*loaded.material)});
    }

    void cancel() noexcept override
    {
        if (!complete_) {
            cancelled_ = true;
        }
    }

private:
    std::filesystem::path path_;
    bool complete_{false};
    bool cancelled_{false};
};

} // namespace

ExplicitFileAuthenticationProvider::ExplicitFileAuthenticationProvider(std::filesystem::path path)
    : path_{std::move(path)}
{
}

auth::AuthenticationBeginResult
ExplicitFileAuthenticationProvider::begin(const auth::AuthenticationRequestContext& context)
{
    static_cast<void>(context);
    if (path_.empty()) {
        return auth::AuthenticationBeginResult::failed(auth::AuthenticationError{
            auth::AuthenticationErrorCode::configuration_error,
            "An explicit authentication material path is required",
        });
    }

    return auth::AuthenticationBeginResult::started(
        std::make_unique<ExplicitFileAuthenticationOperation>(path_));
}

} // namespace hlclient::app
