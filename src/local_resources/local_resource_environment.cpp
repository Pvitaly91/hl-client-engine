#include <hlclient/local_resources/local_resource_environment.hpp>

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace hlclient::local_resources {

namespace detail {
struct LocalResourceEnvironmentToken final {};
} // namespace detail

namespace {

[[nodiscard]] std::string bounded_context(const std::string_view context)
{
    const auto size = (std::min)(
        context.size(), kLocalResourceEnvironmentDiagnosticTextLimit);
    return std::string{context.data(), size};
}

[[nodiscard]] LocalResourceEnvironmentCreateResult environment_failure(
    const LocalResourceEnvironmentErrorCode code,
    const std::string_view context)
{
    return LocalResourceEnvironmentCreateResult{
        nullptr,
        LocalResourceEnvironmentError{code, bounded_context(context)},
    };
}

[[nodiscard]] LocalResourceLocatorCreateResult locator_failure(
    const LocalResourceLocatorCreateErrorCode code,
    const std::string_view context)
{
    return LocalResourceLocatorCreateResult{
        std::nullopt,
        LocalResourceLocatorCreateError{code, bounded_context(context)},
    };
}

[[nodiscard]] LocalResourceLocatorReopenResult reopen_failure(
    const LocalResourceLocatorReopenErrorCode code,
    const std::string_view context)
{
    return LocalResourceLocatorReopenResult{
        std::nullopt,
        LocalResourceLocatorReopenError{code, bounded_context(context)},
    };
}

[[nodiscard]] bool supported_locator_profile(
    const LocalResourceLocatorCompatibilityProfile profile) noexcept
{
    return profile == LocalResourceLocatorCompatibilityProfile::
                          validated_fixed_local_volume_v1;
}

} // namespace

LocalResourceEnvironment::LocalResourceEnvironment(
    std::unique_ptr<LocalResourceResolver> resolver,
    std::vector<LocalResourceSearchRootMetadata> root_metadata,
    std::shared_ptr<const detail::LocalResourceEnvironmentToken> token) noexcept
    : resolver_{std::move(resolver)},
      root_metadata_{std::move(root_metadata)},
      token_{std::move(token)}
{
}

LocalResourceEnvironment::~LocalResourceEnvironment() = default;
LocalResourceEnvironment::LocalResourceEnvironment(
    LocalResourceEnvironment&&) noexcept = default;
LocalResourceEnvironment& LocalResourceEnvironment::operator=(
    LocalResourceEnvironment&&) noexcept = default;

LocalResourceEnvironmentCreateResult LocalResourceEnvironment::create(
    LocalResourceSearchRoots roots,
    const LocalResourceResolverLimits limits)
{
    if (roots.empty() || !valid_local_resource_resolver_limits(limits)) {
        return environment_failure(
            LocalResourceEnvironmentErrorCode::invalid_configuration,
            "Local resource environment roots or limits are invalid");
    }

    try {
        std::vector<LocalResourceSearchRootMetadata> metadata;
        metadata.reserve(roots.size());
        for (std::size_t index = 0U; index < roots.size(); ++index) {
            const auto item = roots.metadata(index);
            if (!item || !item->identity.valid()) {
                return environment_failure(
                    LocalResourceEnvironmentErrorCode::invalid_configuration,
                    "Validated local resource root metadata is invalid");
            }
            metadata.push_back(*item);
        }

        auto resolver = LocalResourceResolver::create(std::move(roots), limits);
        if (!resolver || !resolver.resolver) {
            return environment_failure(
                LocalResourceEnvironmentErrorCode::invalid_configuration,
                resolver.error
                    ? resolver.error->context
                    : "Unable to create the local resource resolver");
        }

        auto token =
            std::make_shared<const detail::LocalResourceEnvironmentToken>();
        return LocalResourceEnvironmentCreateResult{
            std::unique_ptr<LocalResourceEnvironment>{
                new LocalResourceEnvironment{
                    std::move(resolver.resolver),
                    std::move(metadata),
                    std::move(token)}},
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return environment_failure(
            LocalResourceEnvironmentErrorCode::unable_to_retain_environment,
            "Unable to allocate bounded local resource environment state");
    } catch (...) {
        return environment_failure(
            LocalResourceEnvironmentErrorCode::unable_to_retain_environment,
            "Unable to retain local resource environment state");
    }
}

const LocalResourceResolver& LocalResourceEnvironment::resolver() const noexcept
{
    return *resolver_;
}

std::size_t LocalResourceEnvironment::root_count() const noexcept
{
    return root_metadata_.size();
}

const LocalResourceResolverLimits& LocalResourceEnvironment::limits()
    const noexcept
{
    return resolver_->limits();
}

std::optional<LocalResourceSearchRootMetadata>
LocalResourceEnvironment::root_metadata(
    const LocalResourceRootId root_id) const noexcept
{
    const auto found = std::ranges::find_if(
        root_metadata_,
        [&](const LocalResourceSearchRootMetadata& metadata) {
            return metadata.id == root_id;
        });
    if (found == root_metadata_.end()) {
        return std::nullopt;
    }
    return *found;
}

LocalResourceResolutionResult LocalResourceEnvironment::resolve_exact_root(
    const LocalVirtualResourceName& virtual_name,
    const LocalResourceRootId root_id) const
{
    if (!resolver_ || !root_id.valid() || !root_metadata(root_id)) {
        return LocalResourceResolutionResult{
            LocalResourceResolutionCode::io_error,
            std::nullopt,
            "Selected local resource root does not belong to this environment",
        };
    }
    return resolver_->resolve_exact_root(virtual_name, root_id);
}

LocalResourceLocatorCreateResult LocalResourceEnvironment::make_locator(
    const LocalResourceRootId root_id,
    LocalVirtualResourceName virtual_name,
    const LocalStableFileIdentity expected_identity,
    const std::uint64_t expected_file_size,
    const LocalResourceLocatorCompatibilityProfile compatibility_profile) const
{
    if (!resolver_ || !token_ ||
        !valid_local_resource_resolver_limits(resolver_->limits())) {
        return locator_failure(
            LocalResourceLocatorCreateErrorCode::invalid_configuration,
            "Local resource environment is invalid");
    }
    if (!supported_locator_profile(compatibility_profile)) {
        return locator_failure(
            LocalResourceLocatorCreateErrorCode::unsupported_profile,
            "Local resource locator compatibility profile is unsupported");
    }
    if (!root_id.valid() || !root_metadata(root_id)) {
        return locator_failure(
            LocalResourceLocatorCreateErrorCode::root_not_in_environment,
            "Local resource locator root does not belong to this environment");
    }
    if (!expected_identity.valid()) {
        return locator_failure(
            LocalResourceLocatorCreateErrorCode::invalid_identity,
            "Local resource locator identity is invalid");
    }
    if (expected_file_size > resolver_->limits().maximum_file_size ||
        expected_file_size >
            (std::numeric_limits<std::uint32_t>::max)()) {
        return locator_failure(
            LocalResourceLocatorCreateErrorCode::file_size_out_of_bounds,
            "Local resource locator size exceeds the environment bound");
    }
    if (virtual_name.value().empty() ||
        virtual_name.value().size() > kMaximumLocalVirtualResourcePathBytes ||
        virtual_name.component_count() == 0U ||
        virtual_name.component_count() >
            kMaximumLocalVirtualResourceComponents) {
        return locator_failure(
            LocalResourceLocatorCreateErrorCode::invalid_configuration,
            "Local resource locator virtual name is invalid");
    }

    try {
        return LocalResourceLocatorCreateResult{
            LocalResourceLocator{
                root_id,
                std::move(virtual_name),
                expected_identity,
                expected_file_size,
                compatibility_profile,
                token_},
            std::nullopt,
        };
    } catch (...) {
        return locator_failure(
            LocalResourceLocatorCreateErrorCode::unable_to_retain_locator,
            "Unable to retain bounded local resource locator metadata");
    }
}

LocalResourceLocatorReopenResult LocalResourceEnvironment::reopen_verified(
    const LocalResourceLocator& locator) const
{
    const auto locator_token = locator.environment_token_.lock();
    if (!locator_token || !token_ || locator_token.get() != token_.get()) {
        return reopen_failure(
            LocalResourceLocatorReopenErrorCode::
                locator_environment_mismatch,
            "Local resource locator belongs to a different or expired environment");
    }
    if (!resolver_ || !supported_locator_profile(
                          locator.compatibility_profile_) ||
        !locator.expected_identity_.valid() ||
        locator.expected_file_size_ > resolver_->limits().maximum_file_size ||
        locator.expected_file_size_ >
            (std::numeric_limits<std::uint32_t>::max)() ||
        !locator.root_id_.valid() || !root_metadata(locator.root_id_)) {
        return reopen_failure(
            LocalResourceLocatorReopenErrorCode::invalid_locator,
            "Local resource locator metadata is invalid");
    }

    auto resolution = resolver_->resolve_exact_root(
        locator.virtual_name_, locator.root_id_);
    switch (resolution.code) {
    case LocalResourceResolutionCode::resolved: break;
    case LocalResourceResolutionCode::not_found:
        return reopen_failure(
            LocalResourceLocatorReopenErrorCode::locator_target_missing,
            "Local resource locator target is missing from its selected root");
    case LocalResourceResolutionCode::ambiguous_case:
        return reopen_failure(
            LocalResourceLocatorReopenErrorCode::ambiguous_case,
            resolution.context);
    case LocalResourceResolutionCode::not_regular_file:
        return reopen_failure(
            LocalResourceLocatorReopenErrorCode::not_regular_file,
            resolution.context);
    case LocalResourceResolutionCode::reparse_escape:
        return reopen_failure(
            LocalResourceLocatorReopenErrorCode::reparse_escape,
            resolution.context);
    case LocalResourceResolutionCode::remote_volume_unsupported:
        return reopen_failure(
            LocalResourceLocatorReopenErrorCode::remote_volume_unsupported,
            resolution.context);
    case LocalResourceResolutionCode::too_large:
        return reopen_failure(
            LocalResourceLocatorReopenErrorCode::stale_locator,
            "Local resource locator target size changed beyond its bound");
    case LocalResourceResolutionCode::unsafe_name:
    case LocalResourceResolutionCode::unsupported_name_encoding:
    case LocalResourceResolutionCode::unsupported_resource_mapping:
        return reopen_failure(
            LocalResourceLocatorReopenErrorCode::invalid_locator,
            "Local resource locator contains unsupported metadata");
    case LocalResourceResolutionCode::io_error:
        return reopen_failure(
            LocalResourceLocatorReopenErrorCode::io_error,
            resolution.context);
    }

    if (!resolution.file || !resolution.file->is_open() ||
        !resolution.file->is_regular_file() ||
        resolution.file->bytes_consumed() != 0U) {
        return reopen_failure(
            LocalResourceLocatorReopenErrorCode::io_error,
            "Verified local resource reopen produced an invalid handle state");
    }
    if (resolution.file->root_id() != locator.root_id_ ||
        resolution.file->virtual_resource_id() != locator.virtual_name_.id() ||
        resolution.file->identity() != locator.expected_identity_ ||
        resolution.file->file_size() != locator.expected_file_size_) {
        return reopen_failure(
            LocalResourceLocatorReopenErrorCode::stale_locator,
            "Local resource locator target identity or size changed");
    }

    return LocalResourceLocatorReopenResult{
        std::move(resolution.file), std::nullopt};
}

} // namespace hlclient::local_resources
