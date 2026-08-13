#pragma once

#include <hlclient/goldsrc/connect_request.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace hlclient::app {

inline constexpr std::size_t kAuthenticationMaterialProtectedFileSize = 32U;
inline constexpr std::size_t kAuthenticationMaterialFileSize =
    kAuthenticationMaterialProtectedFileSize +
    goldsrc::kObservedConnectAuthenticationSuffixSize;

enum class AuthenticationMaterialFileErrorCode {
    not_regular_file,
    open_failed,
    invalid_size,
    read_failed,
    invalid_material,
};

struct AuthenticationMaterialFileError {
    AuthenticationMaterialFileErrorCode code{
        AuthenticationMaterialFileErrorCode::read_failed};
    std::string context;
    bool exceeds_maximum_size{false};
};

struct AuthenticationMaterialFileLoadResult {
    std::optional<goldsrc::AuthenticationMaterial> material;
    std::optional<AuthenticationMaterialFileError> error;

    [[nodiscard]] explicit operator bool() const noexcept;
};

// Reads one exact, bounded authentication-material record from an explicitly
// supplied local file. Diagnostics never contain the path or file contents.
[[nodiscard]] AuthenticationMaterialFileLoadResult load_authentication_material_file(
    const std::filesystem::path& path);

} // namespace hlclient::app
