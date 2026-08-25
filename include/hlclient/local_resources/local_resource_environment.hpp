#pragma once

#include <hlclient/local_resources/local_resource_locator.hpp>
#include <hlclient/local_resources/local_resource_resolver.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::local_resources {

inline constexpr std::size_t kLocalResourceEnvironmentDiagnosticTextLimit =
    256U;

enum class LocalResourceEnvironmentErrorCode {
    invalid_configuration,
    unable_to_retain_environment,
};

struct LocalResourceEnvironmentError {
    LocalResourceEnvironmentErrorCode code{
        LocalResourceEnvironmentErrorCode::invalid_configuration};
    // Sanitized metadata-only context. Native paths are never included.
    std::string context;
};

enum class LocalResourceLocatorCreateErrorCode {
    invalid_configuration,
    root_not_in_environment,
    invalid_identity,
    file_size_out_of_bounds,
    unsupported_profile,
    unable_to_retain_locator,
};

struct LocalResourceLocatorCreateError {
    LocalResourceLocatorCreateErrorCode code{
        LocalResourceLocatorCreateErrorCode::invalid_configuration};
    std::string context;
};

struct LocalResourceLocatorCreateResult {
    std::optional<LocalResourceLocator> locator;
    std::optional<LocalResourceLocatorCreateError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return locator.has_value();
    }
};

enum class LocalResourceLocatorReopenErrorCode {
    invalid_locator,
    locator_environment_mismatch,
    locator_target_missing,
    stale_locator,
    ambiguous_case,
    not_regular_file,
    reparse_escape,
    remote_volume_unsupported,
    io_error,
};

struct LocalResourceLocatorReopenError {
    LocalResourceLocatorReopenErrorCode code{
        LocalResourceLocatorReopenErrorCode::io_error};
    std::string context;
};

struct LocalResourceLocatorReopenResult {
    std::optional<LocalReadOnlyFile> file;
    std::optional<LocalResourceLocatorReopenError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return file.has_value();
    }
};

struct LocalResourceEnvironmentCreateResult;

// Owns one validated root set and its resolver. Locators produced by this
// environment carry opaque provenance, so the same numeric root ordinal from
// an independently validated environment is not interchangeable.
class LocalResourceEnvironment final {
public:
    [[nodiscard]] static LocalResourceEnvironmentCreateResult create(
        LocalResourceSearchRoots roots,
        LocalResourceResolverLimits limits = {});

    ~LocalResourceEnvironment();
    LocalResourceEnvironment(LocalResourceEnvironment&&) noexcept;
    LocalResourceEnvironment& operator=(LocalResourceEnvironment&&) noexcept;
    LocalResourceEnvironment(const LocalResourceEnvironment&) = delete;
    LocalResourceEnvironment& operator=(const LocalResourceEnvironment&) =
        delete;

    [[nodiscard]] const LocalResourceResolver& resolver() const noexcept;
    [[nodiscard]] std::size_t root_count() const noexcept;
    [[nodiscard]] const LocalResourceResolverLimits& limits() const noexcept;
    [[nodiscard]] std::optional<LocalResourceSearchRootMetadata> root_metadata(
        LocalResourceRootId root_id) const noexcept;

    // Resolves one safe virtual name only within a root that belongs to this
    // environment. A miss never falls through to another configured root.
    // This is the discovery step for derived companion resources; callers
    // should close the returned handle and retain a locator before reading.
    [[nodiscard]] LocalResourceResolutionResult resolve_exact_root(
        const LocalVirtualResourceName& virtual_name,
        LocalResourceRootId root_id) const;

    // Creates metadata only. No filesystem lookup or file-content read occurs.
    [[nodiscard]] LocalResourceLocatorCreateResult make_locator(
        LocalResourceRootId root_id,
        LocalVirtualResourceName virtual_name,
        LocalStableFileIdentity expected_identity,
        std::uint64_t expected_file_size,
        LocalResourceLocatorCompatibilityProfile compatibility_profile =
            LocalResourceLocatorCompatibilityProfile::
                validated_fixed_local_volume_v1) const;

    // Reopens only locator.root_id(), applies the M3.2.1 sandbox rules, and
    // checks identity and size on the returned handle. It reads no file bytes.
    [[nodiscard]] LocalResourceLocatorReopenResult reopen_verified(
        const LocalResourceLocator& locator) const;

private:
    LocalResourceEnvironment(
        std::unique_ptr<LocalResourceResolver> resolver,
        std::vector<LocalResourceSearchRootMetadata> root_metadata,
        std::shared_ptr<const detail::LocalResourceEnvironmentToken>
            token) noexcept;

    std::unique_ptr<LocalResourceResolver> resolver_;
    std::vector<LocalResourceSearchRootMetadata> root_metadata_;
    std::shared_ptr<const detail::LocalResourceEnvironmentToken> token_;
};

struct LocalResourceEnvironmentCreateResult {
    std::unique_ptr<LocalResourceEnvironment> environment;
    std::optional<LocalResourceEnvironmentError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return environment != nullptr;
    }
};

[[nodiscard]] constexpr std::string_view to_string(
    const LocalResourceEnvironmentErrorCode code) noexcept
{
    switch (code) {
    case LocalResourceEnvironmentErrorCode::invalid_configuration:
        return "invalid_configuration";
    case LocalResourceEnvironmentErrorCode::unable_to_retain_environment:
        return "unable_to_retain_environment";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const LocalResourceLocatorCreateErrorCode code) noexcept
{
    switch (code) {
    case LocalResourceLocatorCreateErrorCode::invalid_configuration:
        return "invalid_configuration";
    case LocalResourceLocatorCreateErrorCode::root_not_in_environment:
        return "root_not_in_environment";
    case LocalResourceLocatorCreateErrorCode::invalid_identity:
        return "invalid_identity";
    case LocalResourceLocatorCreateErrorCode::file_size_out_of_bounds:
        return "file_size_out_of_bounds";
    case LocalResourceLocatorCreateErrorCode::unsupported_profile:
        return "unsupported_profile";
    case LocalResourceLocatorCreateErrorCode::unable_to_retain_locator:
        return "unable_to_retain_locator";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const LocalResourceLocatorReopenErrorCode code) noexcept
{
    switch (code) {
    case LocalResourceLocatorReopenErrorCode::invalid_locator:
        return "invalid_locator";
    case LocalResourceLocatorReopenErrorCode::locator_environment_mismatch:
        return "locator_environment_mismatch";
    case LocalResourceLocatorReopenErrorCode::locator_target_missing:
        return "locator_target_missing";
    case LocalResourceLocatorReopenErrorCode::stale_locator:
        return "stale_locator";
    case LocalResourceLocatorReopenErrorCode::ambiguous_case:
        return "ambiguous_case";
    case LocalResourceLocatorReopenErrorCode::not_regular_file:
        return "not_regular_file";
    case LocalResourceLocatorReopenErrorCode::reparse_escape:
        return "reparse_escape";
    case LocalResourceLocatorReopenErrorCode::remote_volume_unsupported:
        return "remote_volume_unsupported";
    case LocalResourceLocatorReopenErrorCode::io_error: return "io_error";
    }
    return "unknown";
}

} // namespace hlclient::local_resources
