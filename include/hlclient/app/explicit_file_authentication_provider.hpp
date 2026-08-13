#pragma once

#include <hlclient/auth/authentication_provider.hpp>

#include <filesystem>

namespace hlclient::app {

// Development/manual provider backed only by a caller-supplied local path. It
// performs no discovery, caching, Steam integration, or fallback search.
class ExplicitFileAuthenticationProvider final : public auth::IAuthenticationProvider {
public:
    explicit ExplicitFileAuthenticationProvider(std::filesystem::path path);

    [[nodiscard]] auth::AuthenticationBeginResult
    begin(const auth::AuthenticationRequestContext& context) override;

private:
    std::filesystem::path path_;
};

} // namespace hlclient::app
